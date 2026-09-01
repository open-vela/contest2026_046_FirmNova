#include <nuttx/config.h>

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <netutils/cJSON.h>

#include "eca_client_log.h"
#include "eca_client_plan.h"
#include "eca_client_plan_observer.h"

typedef struct eca_client_plan_observer_s {
    pthread_mutex_t lock;
    bool lock_initialized;
    bool pending;
    eca_client_model_t *model;
    char latest_title[ECA_CLIENT_TEXT_TITLE_SIZE];
    char latest_text[ECA_CLIENT_REMOTE_BROADCAST_TEXT_MAX];
    char speak_text[ECA_CLIENT_REMOTE_BROADCAST_TEXT_MAX +
                    ECA_CLIENT_TEXT_TITLE_SIZE + 8];
    eca_client_plan_speak_cb_t speak_cb;
    void *speak_arg;
} eca_client_plan_observer_t;

static eca_client_plan_observer_t g_plan_observer;

static int eca_client_plan_observer_lock_init(void)
{
    int ret;

    if (g_plan_observer.lock_initialized) {
        return 0;
    }

    ret = pthread_mutex_init(&g_plan_observer.lock, NULL);
    if (ret != 0) {
        return -ret;
    }

    g_plan_observer.lock_initialized = true;
    return 0;
}

static void eca_client_plan_observer_build_speak_text(
    const char *title, const char *text, char *dst, size_t dst_size)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }

    if (title != NULL && title[0] != '\0' && text != NULL && text[0] != '\0') {
        snprintf(dst, dst_size, "%s，%s", title, text);
    } else if (text != NULL && text[0] != '\0') {
        snprintf(dst, dst_size, "%s", text);
    } else if (title != NULL && title[0] != '\0') {
        snprintf(dst, dst_size, "%s", title);
    } else {
        snprintf(dst, dst_size, "%s", "暂无提醒计划");
    }
}

static int eca_client_plan_observer_on_plan(const eca_client_plan_t *plan,
                                            void *arg)
{
    cJSON *root;
    cJSON *schedule;
    cJSON *frequency;
    cJSON *time;
    const char *title;
    const char *text;
    char schedule_text[96];
    char speak_text[sizeof(g_plan_observer.speak_text)];
    eca_client_plan_speak_cb_t speak_cb;
    void *speak_arg;

    (void)arg;

    if (plan == NULL) {
        return -EINVAL;
    }

    title = plan->title[0] != '\0' ? plan->title : "最近提醒";
    text = plan->text[0] != '\0' ? plan->text : "提醒计划已同步";
    schedule_text[0] = '\0';

    root = plan->payload_json[0] != '\0' ? cJSON_Parse(plan->payload_json) : NULL;
    schedule = cJSON_IsObject(root) ?
        cJSON_GetObjectItemCaseSensitive(root, "schedule") : NULL;
    frequency = cJSON_IsObject(schedule) ?
        cJSON_GetObjectItemCaseSensitive(schedule, "frequency") : NULL;
    time = cJSON_IsObject(schedule) ?
        cJSON_GetObjectItemCaseSensitive(schedule, "time") : NULL;
    if (cJSON_IsString(time) && time->valuestring != NULL &&
        time->valuestring[0] != '\0') {
        const char *frequency_text = "每天";

        if (cJSON_IsString(frequency) && frequency->valuestring != NULL) {
            if (strcmp(frequency->valuestring, "weekly") == 0) {
                frequency_text = "每周";
            } else if (strcmp(frequency->valuestring, "weekdays") == 0) {
                frequency_text = "工作日";
            }
        }

        snprintf(schedule_text, sizeof(schedule_text), "%s %s",
                 frequency_text, time->valuestring);
        text = schedule_text;
    }

    pthread_mutex_lock(&g_plan_observer.lock);
    snprintf(g_plan_observer.latest_title,
             sizeof(g_plan_observer.latest_title), "%s", title);
    snprintf(g_plan_observer.latest_text,
             sizeof(g_plan_observer.latest_text), "%s", text);
    eca_client_plan_observer_build_speak_text(
        title, text, g_plan_observer.speak_text,
        sizeof(g_plan_observer.speak_text));
    g_plan_observer.pending = true;
    speak_cb = g_plan_observer.speak_cb;
    speak_arg = g_plan_observer.speak_arg;
    snprintf(speak_text, sizeof(speak_text), "%s",
             g_plan_observer.speak_text);
    pthread_mutex_unlock(&g_plan_observer.lock);

    ECA_LOGI("latest reminder plan updated title=%s text=%s", title, text);
    if (speak_cb != NULL && speak_cb(speak_text, speak_arg) < 0) {
        ECA_LOGW("reminder plan TTS request failed");
    }
    cJSON_Delete(root);
    return 0;
}

int eca_client_plan_observer_init(eca_client_model_t *model)
{
    int ret;

    if (model == NULL) {
        return -EINVAL;
    }

    ret = eca_client_plan_observer_lock_init();
    if (ret < 0) {
        return ret;
    }

    pthread_mutex_lock(&g_plan_observer.lock);
    g_plan_observer.model = model;
    snprintf(g_plan_observer.latest_title,
             sizeof(g_plan_observer.latest_title), "%s", "最近提醒");
    snprintf(g_plan_observer.latest_text,
             sizeof(g_plan_observer.latest_text), "%s", "暂无提醒计划");
    eca_client_plan_observer_build_speak_text(
        g_plan_observer.latest_title, g_plan_observer.latest_text,
        g_plan_observer.speak_text, sizeof(g_plan_observer.speak_text));
    pthread_mutex_unlock(&g_plan_observer.lock);

    return eca_client_plan_set_handler(eca_client_plan_observer_on_plan, model);
}

void eca_client_plan_observer_poll(void)
{
    eca_client_model_t *model;
    char title[sizeof(g_plan_observer.latest_title)];
    char text[sizeof(g_plan_observer.latest_text)];

    if (!g_plan_observer.lock_initialized) {
        return;
    }

    pthread_mutex_lock(&g_plan_observer.lock);
    if (!g_plan_observer.pending) {
        pthread_mutex_unlock(&g_plan_observer.lock);
        return;
    }
    model = g_plan_observer.model;
    snprintf(title, sizeof(title), "%s", g_plan_observer.latest_title);
    snprintf(text, sizeof(text), "%s", g_plan_observer.latest_text);
    g_plan_observer.pending = false;
    pthread_mutex_unlock(&g_plan_observer.lock);

    if (model != NULL) {
        eca_client_model_set_reminder(model, title, text);
    }
}

int eca_client_plan_observer_get_latest(char *buffer, size_t buffer_size)
{
    int ret;

    if (buffer == NULL || buffer_size == 0) {
        return -EINVAL;
    }

    ret = eca_client_plan_observer_lock_init();
    if (ret < 0) {
        return ret;
    }

    pthread_mutex_lock(&g_plan_observer.lock);
    snprintf(buffer, buffer_size, "%s", g_plan_observer.speak_text);
    pthread_mutex_unlock(&g_plan_observer.lock);
    return 0;
}

int eca_client_plan_observer_set_speaker(eca_client_plan_speak_cb_t cb,
                                         void *arg)
{
    int ret;

    ret = eca_client_plan_observer_lock_init();
    if (ret < 0) {
        return ret;
    }

    pthread_mutex_lock(&g_plan_observer.lock);
    g_plan_observer.speak_cb = cb;
    g_plan_observer.speak_arg = arg;
    pthread_mutex_unlock(&g_plan_observer.lock);
    return 0;
}

int eca_client_plan_observer_speak_latest(void)
{
    eca_client_plan_speak_cb_t cb;
    void *arg;
    char text[sizeof(g_plan_observer.speak_text)];
    int ret;

    ret = eca_client_plan_observer_lock_init();
    if (ret < 0) {
        return ret;
    }

    pthread_mutex_lock(&g_plan_observer.lock);
    cb = g_plan_observer.speak_cb;
    arg = g_plan_observer.speak_arg;
    snprintf(text, sizeof(text), "%s", g_plan_observer.speak_text);
    pthread_mutex_unlock(&g_plan_observer.lock);

    if (cb == NULL) {
        return -ENOSYS;
    }

    return cb(text, arg);
}
