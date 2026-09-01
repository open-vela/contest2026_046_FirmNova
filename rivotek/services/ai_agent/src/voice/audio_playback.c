/*
 * Copyright (C) 2026 Xiaomi Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* audio_playback.c — Streaming PCM playback via NxPlayer and a FIFO.
 *
 * NxPlayer owns the NuttX AUDIOIOC buffer queue and writes directly to the
 * configured PCM device.  TTS chunks are fed through a FIFO, avoiding the
 * media server and FFmpeg audio graph.
 *
 * Sample rate conversion:
 * The TTS WS server returns PCM at 24000Hz (despite requesting 48000Hz).
 * The sunxi audio driver does not support 24000Hz natively, so we use
 * 48000Hz for the player pipeline and up-sample 24000→48000 using
 * linear interpolation (exact 2:1 ratio).
 */

#include "voice/audio_playback.h"
#include "agent_config.h"

#include <errno.h>
#include <fcntl.h>
#include <nuttx/audio/audio.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>

#include <system/nxplayer.h>

static const char* TAG = "audio_pb";

#define PB_FIFO_DIR "/var/pipe"
#define PB_FIFO_PATH PB_FIFO_DIR "/ai_agent_tts"
#define PB_WRITE_CHUNK 4096

/* Resampler fixed-point: Q32 format (1.0 = 1 << 32) */
#define RS_PHASE_BITS 32
#define RS_PHASE_UNIT (1ULL << RS_PHASE_BITS)
#define RS_FRAC_MASK (RS_PHASE_UNIT - 1)

/* ── Resampler state for S16 mono ────────────────────────────── */

struct resampler {
    unsigned int src_rate;   /* actual sample rate of incoming data (24000) */
    unsigned int dst_rate;   /* pipeline sample rate (44100) */
    uint64_t phase;          /* Q32 fixed-point position in input stream */
    int16_t prev_sample;     /* last sample from previous chunk */
    int have_prev;           /* 1 after first chunk */
};

static void resampler_init(struct resampler* rs, unsigned int src_rate,
                           unsigned int dst_rate)
{
    rs->src_rate = src_rate;
    rs->dst_rate = dst_rate;
    rs->phase = 0;
    rs->prev_sample = 0;
    rs->have_prev = 0;
}

/* Resample one chunk of S16 mono PCM.  Returns newly allocated buffer
 * (caller must free), writes output sample count to *out_samples.
 * Falls back to original buffer if no conversion needed. */
static int16_t* resampler_process(struct resampler* rs,
    const int16_t* in, int in_samples,
    int* out_samples)
{
    /* No conversion needed if rates match */
    if (rs->src_rate == rs->dst_rate || in_samples < 1) {
        *out_samples = in_samples;
        return NULL; /* caller uses original buffer */
    }

    /* step = src_rate / dst_rate in Q32.
     * For 24000→44100: 24000/44100 ≈ 0.54421768 → 0x8B4E81BB */
    uint64_t step = ((uint64_t)rs->src_rate << RS_PHASE_BITS) / rs->dst_rate;

    /* upper bound on output samples: ceil(in_samples / step) */
    int max_out = (int)(((int64_t)in_samples << RS_PHASE_BITS) / step) + 4;
    int16_t* out = malloc((size_t)max_out * sizeof(int16_t));

    if (!out) {
        *out_samples = 0;
        return NULL;
    }

    int oi = 0;
    uint64_t phase = rs->phase;
    int have_prev = rs->have_prev;
    int16_t prev = rs->prev_sample;

    while (oi < max_out) {
        uint64_t ipos = phase >> RS_PHASE_BITS;
        if (ipos >= (uint64_t)(in_samples - 1))
            break;

        int pi = (int)ipos;
        uint32_t frac = (uint32_t)(phase & RS_FRAC_MASK);

        /* Get s0 (use prev sample for position 0 interpolation) */
        int32_t s0 = (pi == 0 && have_prev) ? (int32_t)prev : (int32_t)in[pi];
        int32_t s1 = (int32_t)in[pi + 1];

        /* Linear interpolation: s0 + (s1 - s0) * frac
         * Use top 15 bits of frac for 15-bit precision multiply */
        int32_t sample = s0 + (((s1 - s0) * (int32_t)(frac >> 17)) >> 15);

        /* Clamp to S16 range */
        if (sample > 32767) sample = 32767;
        if (sample < -32768) sample = -32768;

        out[oi++] = (int16_t)sample;
        phase += step;
    }

    /* Save trailing state for next call */
    rs->phase = phase & RS_FRAC_MASK;   /* keep fractional part only */
    rs->prev_sample = in[in_samples - 1];
    rs->have_prev = 1;

    *out_samples = oi;
    return out;
}

struct audio_playback {
    struct nxplayer_s* player;
    int fifo_fd;
    int fifo_guard_fd;
    pthread_t init_thread;
    int init_ret;
    size_t total_written;
    unsigned int sample_rate;
    unsigned int channels;
    unsigned int bits_per_sample;
    volatile int stopped; /* set by audio_playback_stop() from another thread */
    struct resampler rs;  /* sample rate converter (only for mono S16) */
};

static void* audio_playback_init_thread(void* arg)
{
    audio_playback_t* pb = arg;

    pb->init_ret = nxplayer_playraw(pb->player, PB_FIFO_PATH,
        AUDIO_FMT_PCM, AUDIO_FMT_UNDEF, (uint8_t)pb->channels,
        (uint8_t)pb->bits_per_sample, pb->sample_rate, 0);
    return NULL;
}

static void audio_playback_cleanup(audio_playback_t* pb)
{
    if (pb->fifo_fd >= 0) {
        close(pb->fifo_fd);
        pb->fifo_fd = -1;
    }

    if (pb->fifo_guard_fd >= 0) {
        close(pb->fifo_guard_fd);
        pb->fifo_guard_fd = -1;
    }

    if (pb->player) {
#ifndef CONFIG_AUDIO_EXCLUDE_STOP
        nxplayer_stop(pb->player);
#endif
        nxplayer_release(pb->player);
        pb->player = NULL;
    }

    unlink(PB_FIFO_PATH);
}

audio_playback_t* audio_playback_open(const char* dev_path,
    unsigned int sample_rate, unsigned int channels,
    unsigned int bits_per_sample, unsigned int src_sample_rate)
{
    syslog(LOG_INFO, "[%s] audio_playback_open: %uHz %uch %ubit (src=%uHz)\n",
        TAG, sample_rate, channels, bits_per_sample, src_sample_rate);

    if (!dev_path || channels == 0 || channels > UINT8_MAX
        || bits_per_sample == 0 || bits_per_sample > UINT8_MAX) {
        return NULL;
    }

    audio_playback_t* pb = calloc(1, sizeof(*pb));

    if (!pb) {
        return NULL;
    }

    pb->fifo_fd = -1;
    pb->fifo_guard_fd = -1;
    pb->sample_rate = sample_rate;
    pb->channels = channels;
    pb->bits_per_sample = bits_per_sample;
    pb->init_ret = -EINPROGRESS;

    if (mkdir(PB_FIFO_DIR, 0777) < 0 && errno != EEXIST) {
        syslog(LOG_ERR, "[%s] mkdir %s failed: %d\n",
            TAG, PB_FIFO_DIR, errno);
        free(pb);
        return NULL;
    }

    unlink(PB_FIFO_PATH);
    if (mkfifo(PB_FIFO_PATH, 0666) < 0) {
        syslog(LOG_ERR, "[%s] mkfifo %s failed: %d\n",
            TAG, PB_FIFO_PATH, errno);
        free(pb);
        return NULL;
    }

    /* Keep one reader open so a device-side playback failure is reported
     * through the normal write path instead of raising SIGPIPE. */
    pb->fifo_guard_fd = open(PB_FIFO_PATH, O_RDONLY | O_NONBLOCK);
    if (pb->fifo_guard_fd < 0) {
        syslog(LOG_ERR, "[%s] open FIFO guard failed: %d\n", TAG, errno);
        audio_playback_cleanup(pb);
        free(pb);
        return NULL;
    }

    pb->player = nxplayer_create();
    if (!pb->player) {
        audio_playback_cleanup(pb);
        free(pb);
        return NULL;
    }

    int ret = nxplayer_setvolume(pb->player, 1600);
    if (ret < 0) {
        syslog(LOG_ERR, "[%s] set playback volume failed: %d\n", TAG, ret);
        audio_playback_cleanup(pb);
        free(pb);
        return NULL;
    }

    ret = nxplayer_setdevice(pb->player, dev_path);
    if (ret < 0) {
        syslog(LOG_ERR, "[%s] set device %s failed: %d\n",
            TAG, dev_path, ret);
        audio_playback_cleanup(pb);
        free(pb);
        return NULL;
    }

    ret = pthread_create(&pb->init_thread, NULL,
        audio_playback_init_thread, pb);
    if (ret != 0) {
        syslog(LOG_ERR, "[%s] create NxPlayer init thread failed: %d\n",
            TAG, ret);
        audio_playback_cleanup(pb);
        free(pb);
        return NULL;
    }

    pb->fifo_fd = open(PB_FIFO_PATH, O_WRONLY);
    pthread_join(pb->init_thread, NULL);

    if (pb->fifo_fd < 0 || pb->init_ret < 0) {
        syslog(LOG_ERR, "[%s] NxPlayer FIFO open failed: fd=%d ret=%d errno=%d\n",
            TAG, pb->fifo_fd, pb->init_ret, errno);
        audio_playback_cleanup(pb);
        free(pb);
        return NULL;
    }

    int flags = fcntl(pb->fifo_fd, F_GETFL);
    ret = flags < 0 ? -1
                    : fcntl(pb->fifo_fd, F_SETFL, flags | O_NONBLOCK);
    if (ret < 0) {
        syslog(LOG_ERR, "[%s] set FIFO nonblocking failed: %d\n", TAG, errno);
        audio_playback_cleanup(pb);
        free(pb);
        return NULL;
    }

    pb->stopped = 0;
    resampler_init(&pb->rs, src_sample_rate, sample_rate);

    syslog(LOG_INFO, "[%s] opened %s (%uHz %uch %ubit) NxPlayer FIFO"
        " src=%uHz%s\n",
        TAG, dev_path, sample_rate, channels, bits_per_sample,
        src_sample_rate,
        (src_sample_rate != sample_rate) ? " (resampling)" : "");
    return pb;
}

int audio_playback_write(audio_playback_t* pb,
    const void* buf, size_t len)
{
    if (!pb || !pb->player || pb->fifo_fd < 0 || !buf || len == 0) {
        return -EINVAL;
    }

    /* AEC workaround: voice_channel_start() sets stopped to skip
     * remaining TTS chunks and prevent echo into the microphone. */
    if (pb->stopped) {
        return -ECANCELED;
    }

    /* Resample from TTS actual rate to pipeline rate if needed */
    const void* write_buf = buf;
    size_t write_len = len;
    int16_t* resampled = NULL;

    if (pb->channels == 1 && pb->bits_per_sample == 16) {
        int out_samples;
        resampled = resampler_process(&pb->rs,
            (const int16_t*)buf, (int)(len / 2), &out_samples);
        if (resampled) {
            write_buf = resampled;
            write_len = (size_t)out_samples * 2;
            syslog(LOG_DEBUG, "[%s] resample: %zu->%zu bytes (%uhz->%uhz)\n",
                TAG, len, write_len, pb->rs.src_rate, pb->rs.dst_rate);
        }
    }

    /* Keep writes bounded and use nonblocking backpressure so stop requests
     * can interrupt a stream even while the NxPlayer queue is full. */
    const unsigned char* data = (const unsigned char*)write_buf;
    size_t remaining = write_len;
    ssize_t total = 0;

    while (remaining > 0 && !pb->stopped) {
        size_t chunk = remaining > PB_WRITE_CHUNK ? PB_WRITE_CHUNK : remaining;
        ssize_t n = write(pb->fifo_fd, data, chunk);
        if (n > 0) {
            data += n;
            remaining -= (size_t)n;
            total += n;
        } else if (n == 0) {
            break; /* EOF */
        } else {
            int err = errno;
            if (err == EAGAIN || err == EINTR) {
                usleep(1000); /* 1ms */
                continue;
            }
            if (total == 0) {
                total = -err;
            }
            syslog(LOG_ERR, "[%s] FIFO write(%zu) failed: %d (partial=%zd)\n",
                TAG, chunk, err, total);
            break;
        }
    }

    if (resampled) {
        free(resampled);
    }

    if (total > 0) {
        pb->total_written += (size_t)total;
    }

    return (int)total;
}

void audio_playback_stop(audio_playback_t* pb)
{
    if (pb) {
        pb->stopped = 1;
        if (pb->player) {
#ifndef CONFIG_AUDIO_EXCLUDE_STOP
            nxplayer_stop(pb->player);
#endif
        }
    }
}

void audio_playback_close(audio_playback_t* pb)
{
    if (!pb) {
        return;
    }

    if (pb->player || pb->fifo_fd >= 0) {
        syslog(LOG_INFO, "[%s] closing (%zu bytes written)\n",
            TAG, pb->total_written);
        audio_playback_cleanup(pb);
        syslog(LOG_INFO, "[%s] playback done\n", TAG);
    }

    free(pb);
}
