#include <nuttx/config.h>

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "eca_client_ai_agent.h"
#include "eca_client_log.h"

#define ECA_CLIENT_TTS_REQUEST_PATH   "/tmp/tts_request.txt"
#define ECA_CLIENT_TTS_PROCESSING_PATH "/tmp/tts_request.processing"
#define ECA_CLIENT_TTS_STATUS_PATH    "/tmp/tts_status.txt"
#define ECA_CLIENT_VOICE_REQUEST_PATH "/tmp/voice_request.txt"
#define ECA_CLIENT_VOICE_STATUS_PATH  "/tmp/voice_status.txt"

static pthread_mutex_t g_ai_agent_request_lock = PTHREAD_MUTEX_INITIALIZER;

static int eca_client_ai_agent_write_request(const char *path,
                                             const char *value,
                                             bool replace)
{
    char temp_path[64];
    FILE *fp;
    int ret = 0;

    if (path == NULL || value == NULL || value[0] == '\0') {
        return -EINVAL;
    }

    pthread_mutex_lock(&g_ai_agent_request_lock);
    if (!replace && access(path, F_OK) == 0) {
        ret = -EBUSY;
        goto out;
    }

    snprintf(temp_path, sizeof(temp_path), "%s.%d", path, getpid());
    fp = fopen(temp_path, "w");
    if (fp == NULL) {
        ret = -errno;
        goto out;
    }

    if (fputs(value, fp) < 0) {
        ret = -EIO;
    }
    if (fclose(fp) != 0 && ret == 0) {
        ret = -errno;
    }
    if (ret < 0) {
        unlink(temp_path);
        goto out;
    }

    if (rename(temp_path, path) < 0) {
        ret = -errno;
        unlink(temp_path);
    }

out:
    pthread_mutex_unlock(&g_ai_agent_request_lock);
    return ret;
}

int eca_client_ai_agent_start(void)
{
    int ret = system("ai_agent &");

    if (ret < 0) {
        ECA_LOGW("ai_agent start failed ret=%d", ret);
        return ret;
    }

    ECA_LOGI("ai_agent start requested");
    return 0;
}

int eca_client_ai_agent_speak(const char *text, void *arg)
{
    int ret;

    (void)arg;
    ret = eca_client_ai_agent_write_request(ECA_CLIENT_TTS_REQUEST_PATH, text,
                                            false);
    if (ret < 0) {
        ECA_LOGW("TTS request write failed ret=%d", ret);
        return ret;
    }

    ECA_LOGI("TTS request written: %s", text);
    return 0;
}

int eca_client_ai_agent_speak_priority(const char *text)
{
    return eca_client_ai_agent_write_request(ECA_CLIENT_TTS_REQUEST_PATH, text,
                                             true);
}

static int eca_client_ai_agent_read_status(const char *path, char *status,
                                           size_t status_size)
{
    FILE *fp;

    fp = fopen(path, "r");
    if (fp == NULL) {
        return -errno;
    }

    if (fgets(status, status_size, fp) == NULL) {
        fclose(fp);
        return -EIO;
    }

    fclose(fp);
    return 0;
}

int eca_client_ai_agent_is_idle(void)
{
    char voice_status[16];
    char tts_status[16];

    if (eca_client_ai_agent_read_status(ECA_CLIENT_VOICE_STATUS_PATH,
                                        voice_status,
                                        sizeof(voice_status)) < 0 ||
        eca_client_ai_agent_read_status(ECA_CLIENT_TTS_STATUS_PATH,
                                        tts_status,
                                        sizeof(tts_status)) < 0) {
        return 0;
    }

    return strcmp(voice_status, "idle") == 0 &&
           strcmp(tts_status, "idle") == 0 &&
           access(ECA_CLIENT_TTS_REQUEST_PATH, F_OK) != 0 &&
           access(ECA_CLIENT_TTS_PROCESSING_PATH, F_OK) != 0;
}

int eca_client_ai_agent_voice_start(void)
{
    return eca_client_ai_agent_write_request(
        ECA_CLIENT_VOICE_REQUEST_PATH, "start", false);
}

int eca_client_ai_agent_voice_stop(void)
{
    return eca_client_ai_agent_write_request(
        ECA_CLIENT_VOICE_REQUEST_PATH, "stop", true);
}

int eca_client_ai_agent_voice_status(char *status, size_t status_size)
{
    if (status == NULL || status_size == 0) {
        return -EINVAL;
    }
    return eca_client_ai_agent_read_status(ECA_CLIENT_VOICE_STATUS_PATH,
                                           status, status_size);
}
