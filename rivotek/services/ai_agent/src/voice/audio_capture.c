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

/* Audio capture via NxRecorder and the NuttX PCM device. */

#include "voice/audio_capture.h"
#include "agent_config.h"

#include <errno.h>
#include <fcntl.h>
#include <nuttx/audio/audio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <syslog.h>
#include <system/nxrecorder.h>
#include <unistd.h>

static const char* TAG = "audio_cap";

#define CAP_FIFO_DIR "/var/pipe"
#define CAP_FIFO_PATH CAP_FIFO_DIR "/ai_agent_capture"

/* Audio gain multiplier to compensate for multi-channel DMIC mixing.
 * When board configures 6 DMIC channels but only 1 has a physical mic,
 * the mono mix dilutes the signal by ~6x. Set gain to compensate.
 * Configurable via CONFIG_AI_AGENT_AUDIO_CAPTURE_GAIN in Kconfig. */
#ifdef CONFIG_AI_AGENT_AUDIO_CAPTURE_GAIN
#define AGENT_AUDIO_CAPTURE_GAIN CONFIG_AI_AGENT_AUDIO_CAPTURE_GAIN
#else
#define AGENT_AUDIO_CAPTURE_GAIN 6
#endif

struct audio_capture {
    struct nxrecorder_s* recorder;
    int fifo_fd;
    unsigned int sample_rate;
    unsigned int channels;
    unsigned int device_channels;
    unsigned int bits_per_sample;
    volatile int started;
    volatile int stopping;
};

audio_capture_t* audio_capture_open(const char* dev_path,
    unsigned int sample_rate, unsigned int channels,
    unsigned int bits_per_sample)
{
    if (!dev_path || channels == 0 || channels > 255
        || bits_per_sample == 0 || bits_per_sample > 255) {
        return NULL;
    }

    audio_capture_t* cap = calloc(1, sizeof(*cap));

    if (!cap) {
        return NULL;
    }

    cap->fifo_fd = -1;
    cap->sample_rate = sample_rate;
    cap->channels = channels;
    cap->device_channels = channels;
    cap->bits_per_sample = bits_per_sample;

    if (mkdir(CAP_FIFO_DIR, 0777) < 0 && errno != EEXIST) {
        syslog(LOG_ERR, "[%s] mkdir %s failed: %d\n",
            TAG, CAP_FIFO_DIR, errno);
        free(cap);
        return NULL;
    }

    unlink(CAP_FIFO_PATH);
    if (mkfifo(CAP_FIFO_PATH, 0666) < 0) {
        syslog(LOG_ERR, "[%s] mkfifo %s failed: %d\n",
            TAG, CAP_FIFO_PATH, errno);
        free(cap);
        return NULL;
    }

    cap->fifo_fd = open(CAP_FIFO_PATH, O_RDONLY | O_NONBLOCK);
    if (cap->fifo_fd < 0) {
        syslog(LOG_ERR, "[%s] open FIFO failed: %d\n", TAG, errno);
        unlink(CAP_FIFO_PATH);
        free(cap);
        return NULL;
    }

    cap->recorder = nxrecorder_create();
    if (!cap->recorder) {
        syslog(LOG_ERR, "[%s] nxrecorder_create failed\n", TAG);
        close(cap->fifo_fd);
        unlink(CAP_FIFO_PATH);
        free(cap);
        return NULL;
    }

    int ret = nxrecorder_setdevice(cap->recorder, dev_path);
    if (ret < 0) {
        syslog(LOG_ERR, "[%s] set device %s failed: %d\n",
            TAG, dev_path, ret);
        nxrecorder_release(cap->recorder);
        close(cap->fifo_fd);
        unlink(CAP_FIFO_PATH);
        free(cap);
        return NULL;
    }

    syslog(LOG_INFO,
        "[%s] opened (%uHz %uch %ubit) via %s\n",
        TAG, sample_rate, channels, bits_per_sample, dev_path);
    return cap;
}

int audio_capture_start(audio_capture_t* cap)
{
    if (!cap || !cap->recorder) {
        return -EINVAL;
    }

    cap->stopping = 0;
    int ret = nxrecorder_recordinternal(cap->recorder, CAP_FIFO_PATH,
        AUDIO_FMT_PCM, (uint8_t)cap->device_channels,
        (uint8_t)cap->bits_per_sample, cap->sample_rate, 0);

    if (ret < 0) {
        syslog(LOG_ERR, "[%s] start failed: %d\n", TAG, ret);
        return ret;
    }

    cap->started = 1;
    syslog(LOG_INFO, "[%s] capture started\n", TAG);
    return 0;
}

int audio_capture_read(audio_capture_t* cap, void* buf, size_t len)
{
    if (!cap || cap->fifo_fd < 0 || !buf || len == 0) {
        return -EINVAL;
    }

    ssize_t n;

    do {
        n = read(cap->fifo_fd, buf, len);
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (cap->stopping) {
                return -ECANCELED;
            }
            usleep(1000);
        } else if (n == 0 && !cap->stopping) {
            usleep(1000);
        }
    } while ((n < 0 && (errno == EINTR || errno == EAGAIN
        || errno == EWOULDBLOCK)) || (n == 0 && !cap->stopping));

    if (n == 0 && cap->stopping) {
        return -ECANCELED;
    }

    if (n < 0) {
        return -errno;
    }

    if (cap->channels == 1 && cap->device_channels == 2
        && cap->bits_per_sample == 16) {
        int16_t* samples = (int16_t*)buf;
        int frames = (int)n / (2 * (int)sizeof(int16_t));

        for (int frame = 0; frame < frames; frame++) {
            int32_t left = samples[frame * 2];
            int32_t right = samples[frame * 2 + 1];
            samples[frame] = (int16_t)((left + right) / 2);
        }

        n = (ssize_t)frames * sizeof(int16_t);
    }

    /* Apply gain to compensate for multi-channel DMIC mixing.
     * When 6 DMIC channels are configured but only 1 has a physical mic,
     * the mono downmix dilutes the signal. Amplify to restore level. */
#if AGENT_AUDIO_CAPTURE_GAIN > 1
    if (n > 0) {
        int16_t* samples = (int16_t*)buf;
        int sample_count = (int)n / 2;

        for (int i = 0; i < sample_count; i++) {
            int32_t amplified = (int32_t)samples[i] * AGENT_AUDIO_CAPTURE_GAIN;

            /* Clamp to 16-bit range to prevent overflow */
            if (amplified > 32767) {
                amplified = 32767;
            } else if (amplified < -32768) {
                amplified = -32768;
            }

            samples[i] = (int16_t)amplified;
        }
    }
#endif

    return (int)n;
}

int audio_capture_stop(audio_capture_t* cap)
{
    if (!cap || !cap->recorder) {
        return -EINVAL;
    }

    if (!cap->started) {
        cap->stopping = 1;
        return 0;
    }

    syslog(LOG_INFO, "[%s] stopping capture\n", TAG);
    int ret = nxrecorder_stop(cap->recorder);
    cap->stopping = 1;
    cap->started = 0;
    syslog(LOG_INFO, "[%s] capture stopped: %d\n", TAG, ret);
    return ret;
}

void audio_capture_close(audio_capture_t* cap)
{
    if (!cap) {
        return;
    }

    if (cap->recorder) {
        if (cap->started) {
            int ret = audio_capture_stop(cap);
            if (ret < 0) {
                syslog(LOG_WARNING, "[%s] nxrecorder_stop: %d\n", TAG, ret);
            }
        }

        nxrecorder_release(cap->recorder);
    }

    if (cap->fifo_fd >= 0) {
        close(cap->fifo_fd);
    }

    unlink(CAP_FIFO_PATH);
    free(cap);
}
