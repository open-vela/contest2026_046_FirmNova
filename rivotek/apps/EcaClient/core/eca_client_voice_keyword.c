#include <nuttx/config.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "eca_client_log.h"
#include "eca_client_network.h"
#include "eca_client_voice_keyword.h"

#define ECA_CLIENT_VOICE_KEYWORD_STACKSIZE 4096
#define ECA_CLIENT_VOICE_KEYWORD_LINE_MAX  96

typedef struct eca_client_voice_keyword_state_s {
    pthread_t thread;
    volatile bool running;
    bool thread_started;
    int fd;
    eca_client_model_t *model;
} eca_client_voice_keyword_state_t;

static eca_client_voice_keyword_state_t g_voice_keyword = {
    .fd = -1,
};

static void eca_client_voice_keyword_close_fd(void)
{
    if (g_voice_keyword.fd >= 0) {
        close(g_voice_keyword.fd);
        g_voice_keyword.fd = -1;
    }
}

static int eca_client_voice_keyword_ensure_pipe(void)
{
    int ret;

    ret = mkdir("/var/pipe", 0777);
    if (ret < 0 && errno != EEXIST) {
        ECA_LOGW("mkdir /var/pipe failed: %d", errno);
    }

    ret = mkfifo(ECA_CLIENT_VOICE_KEYWORD_PIPE, 0666);
    if (ret < 0 && errno != EEXIST) {
        ECA_LOGW("mkfifo %s failed: %d", ECA_CLIENT_VOICE_KEYWORD_PIPE, errno);
        return -errno;
    }

    return 0;
}

static void eca_client_voice_keyword_process_line(char *line)
{
    char keyword[32];
    char source[32];
    char *p = line;
    int confidence = -1;
    int matched;

    while (isspace((unsigned char)*p)) {
        p++;
    }

    if (*p == '\0') {
        return;
    }

    keyword[0] = '\0';
    source[0] = '\0';
    matched = sscanf(p, "%31s %d %31s", keyword, &confidence, source);
    if (matched < 1) {
        return;
    }

    if (strcasecmp(keyword, "yes") != 0 &&
        strcasecmp(keyword, "no") != 0 &&
        strcmp(keyword, "异常声音上报") != 0) {
        ECA_LOGD("voice keyword ignored: %s", keyword);
        return;
    }

    if (matched < 3 || source[0] == '\0') {
        snprintf(source, sizeof(source), "dsp");
    }

    ECA_LOGI("voice keyword detected keyword=%s confidence=%d source=%s",
             keyword, confidence, source);
    eca_client_network_publish_voice_keyword(keyword, confidence, source);
}

static void eca_client_voice_keyword_process_bytes(char *line, size_t *line_len,
                                                   const char *buf, ssize_t len)
{
    for (ssize_t i = 0; i < len; i++) {
        char ch = buf[i];

        if (ch == '\r') {
            continue;
        }

        if (ch == '\n') {
            line[*line_len] = '\0';
            eca_client_voice_keyword_process_line(line);
            *line_len = 0;
            continue;
        }

        if (*line_len + 1 < ECA_CLIENT_VOICE_KEYWORD_LINE_MAX) {
            line[(*line_len)++] = ch;
        } else {
            *line_len = 0;
        }
    }
}

static void *eca_client_voice_keyword_thread(void *arg)
{
    char buf[64];
    char line[ECA_CLIENT_VOICE_KEYWORD_LINE_MAX];
    size_t line_len = 0;

    (void)arg;

    ECA_LOGI("voice keyword listener started pipe=%s",
             ECA_CLIENT_VOICE_KEYWORD_PIPE);

    while (g_voice_keyword.running) {
        struct pollfd pfd;
        int ret;

        if (g_voice_keyword.fd < 0) {
            g_voice_keyword.fd = open(ECA_CLIENT_VOICE_KEYWORD_PIPE,
                                      O_RDONLY | O_NONBLOCK);
            if (g_voice_keyword.fd < 0) {
                ECA_LOGW("open %s failed: %d", ECA_CLIENT_VOICE_KEYWORD_PIPE,
                         errno);
                usleep(500000);
                continue;
            }
        }

        pfd.fd = g_voice_keyword.fd;
        pfd.events = POLLIN;
        pfd.revents = 0;

        ret = poll(&pfd, 1, 500);
        if (ret < 0) {
            if (errno != EINTR) {
                ECA_LOGW("voice keyword poll failed: %d", errno);
                eca_client_voice_keyword_close_fd();
            }
            continue;
        }

        if (ret == 0) {
            continue;
        }

        if ((pfd.revents & POLLIN) != 0) {
            ret = read(g_voice_keyword.fd, buf, sizeof(buf));
            if (ret > 0) {
                eca_client_voice_keyword_process_bytes(line, &line_len, buf,
                                                       ret);
            } else if (ret == 0) {
                usleep(100000);
            } else if (errno != EAGAIN && errno != EINTR) {
                ECA_LOGW("voice keyword read failed: %d", errno);
                eca_client_voice_keyword_close_fd();
            }
        }

        if ((pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) != 0) {
            eca_client_voice_keyword_close_fd();
        }
    }

    eca_client_voice_keyword_close_fd();
    ECA_LOGI("voice keyword listener stopped");
    return NULL;
}

int eca_client_voice_keyword_start(eca_client_model_t *model)
{
    pthread_attr_t attr;
    int ret;

    if (g_voice_keyword.thread_started) {
        return 0;
    }

    ret = eca_client_voice_keyword_ensure_pipe();
    if (ret < 0) {
        return ret;
    }

    g_voice_keyword.model = model;
    g_voice_keyword.running = true;

    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, ECA_CLIENT_VOICE_KEYWORD_STACKSIZE);
    ret = pthread_create(&g_voice_keyword.thread, &attr,
                         eca_client_voice_keyword_thread, NULL);
    pthread_attr_destroy(&attr);
    if (ret != 0) {
        g_voice_keyword.running = false;
        return -ret;
    }

    g_voice_keyword.thread_started = true;
    return 0;
}

void eca_client_voice_keyword_stop(void)
{
    if (!g_voice_keyword.thread_started) {
        return;
    }

    g_voice_keyword.running = false;
    pthread_join(g_voice_keyword.thread, NULL);
    g_voice_keyword.thread_started = false;
    g_voice_keyword.model = NULL;
}
