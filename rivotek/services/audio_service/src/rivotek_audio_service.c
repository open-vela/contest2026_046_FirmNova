#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <nuttx/audio/audio.h>

#include "rivotek_audio_service.h"

#define RVT_AUDIO_SERVICE_LOG(fmt, ...) \
    printf("[rivotek_audio_service] " fmt "\n", ##__VA_ARGS__)

#ifdef CONFIG_AUDIOUTILS_NXAUDIO_DEVPATH
#define RVT_AUDIO_SERVICE_DEVPATH CONFIG_AUDIOUTILS_NXAUDIO_DEVPATH
#else
#define RVT_AUDIO_SERVICE_DEVPATH "/dev/audio/pcm0p"
#endif

typedef struct rivotek_audio_service_state_s {
    pthread_mutex_t lock;
    bool lock_initialized;
} rivotek_audio_service_state_t;

static rivotek_audio_service_state_t g_audio_service;

static int rivotek_audio_service_clamp_volume(int volume)
{
    if (volume < 0) {
        return 0;
    }

    if (volume > 100) {
        return 100;
    }

    return volume;
}

static int rivotek_audio_service_lock_init(void)
{
    int ret;

    if (g_audio_service.lock_initialized) {
        return 0;
    }

    ret = pthread_mutex_init(&g_audio_service.lock, NULL);
    if (ret != 0) {
        return -ret;
    }

    g_audio_service.lock_initialized = true;
    return 0;
}

int rivotek_audio_service_set_volume(int volume)
{
    struct audio_caps_desc_s caps_desc;
    int clamped = rivotek_audio_service_clamp_volume(volume);
    int fd;
    int ret;

    ret = rivotek_audio_service_lock_init();
    if (ret < 0) {
        return ret;
    }

    pthread_mutex_lock(&g_audio_service.lock);
    fd = open(RVT_AUDIO_SERVICE_DEVPATH, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        ret = -errno;
        pthread_mutex_unlock(&g_audio_service.lock);
        RVT_AUDIO_SERVICE_LOG("open audio device %s failed %d",
                              RVT_AUDIO_SERVICE_DEVPATH, ret);
        return ret;
    }

    memset(&caps_desc, 0, sizeof(caps_desc));
    caps_desc.caps.ac_len = sizeof(struct audio_caps_s);
    caps_desc.caps.ac_type = AUDIO_TYPE_FEATURE;
    caps_desc.caps.ac_format.hw = AUDIO_FU_VOLUME;
    caps_desc.caps.ac_controls.hw[0] = (uint16_t)(clamped * 10);

    ret = ioctl(fd, AUDIOIOC_CONFIGURE, (unsigned long)(uintptr_t)&caps_desc);
    if (ret < 0) {
        ret = -errno;
    }

    close(fd);
    pthread_mutex_unlock(&g_audio_service.lock);

    if (ret < 0) {
        RVT_AUDIO_SERVICE_LOG("set audio volume %d failed %d", clamped, ret);
        return ret;
    }

    RVT_AUDIO_SERVICE_LOG("audio volume set to %d", clamped);
    return 0;
}
