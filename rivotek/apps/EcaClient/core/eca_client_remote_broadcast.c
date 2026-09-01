#include <nuttx/config.h>

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "eca_client_alert.h"
#include "eca_client_log.h"
#include "eca_client_plan.h"
#include "eca_client_remote_broadcast.h"

typedef struct eca_client_remote_broadcast_queue_s {
    eca_client_remote_broadcast_t *items[ECA_CLIENT_REMOTE_BROADCAST_QUEUE_SIZE];
    int read_index;
    int write_index;
    int count;
} eca_client_remote_broadcast_queue_t;

typedef struct eca_client_remote_broadcast_state_s {
    pthread_mutex_t lock;
    bool lock_initialized;
    eca_client_remote_broadcast_queue_t all_queue;
    eca_client_remote_broadcast_queue_t alert_queue;
    eca_client_remote_broadcast_queue_t plan_queue;
    eca_client_remote_broadcast_handler_t broadcast_handler;
    void *broadcast_handler_arg;
    eca_client_alert_handler_t alert_handler;
    void *alert_handler_arg;
    eca_client_plan_handler_t plan_handler;
    void *plan_handler_arg;
} eca_client_remote_broadcast_state_t;

static eca_client_remote_broadcast_state_t g_remote_broadcast;

static long long eca_client_remote_broadcast_now_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
        return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    }

    return (long long)time(NULL) * 1000;
}

static int eca_client_remote_broadcast_lock_init(void)
{
    int ret;

    if (g_remote_broadcast.lock_initialized) {
        return 0;
    }

    ret = pthread_mutex_init(&g_remote_broadcast.lock, NULL);
    if (ret != 0) {
        return -ret;
    }

    g_remote_broadcast.lock_initialized = true;
    return 0;
}

static bool eca_client_remote_broadcast_kind_is(
    const eca_client_remote_broadcast_t *broadcast, const char *kind)
{
    if (broadcast == NULL || kind == NULL) {
        return false;
    }

    return strncmp(broadcast->kind, kind, sizeof(broadcast->kind)) == 0;
}

static void eca_client_remote_broadcast_copy(
    eca_client_remote_broadcast_t *dst,
    const eca_client_remote_broadcast_t *src)
{
    memset(dst, 0, sizeof(*dst));
    snprintf(dst->command_id, sizeof(dst->command_id), "%s",
             src->command_id[0] != '\0' ? src->command_id : "");
    snprintf(dst->kind, sizeof(dst->kind), "%s",
             src->kind[0] != '\0' ? src->kind : "message");
    snprintf(dst->title, sizeof(dst->title), "%s",
             src->title[0] != '\0' ? src->title : "");
    snprintf(dst->text, sizeof(dst->text), "%s",
             src->text[0] != '\0' ? src->text : "");
    snprintf(dst->audio_url, sizeof(dst->audio_url), "%s",
             src->audio_url[0] != '\0' ? src->audio_url : "");
    snprintf(dst->source, sizeof(dst->source), "%s",
             src->source[0] != '\0' ? src->source : "app");
    snprintf(dst->payload_json, sizeof(dst->payload_json), "%s",
             src->payload_json[0] != '\0' ? src->payload_json : "");
    dst->received_at_ms = src->received_at_ms != 0 ?
        src->received_at_ms : eca_client_remote_broadcast_now_ms();
}

static void eca_client_remote_broadcast_queue_push(
    eca_client_remote_broadcast_queue_t *queue,
    const eca_client_remote_broadcast_t *broadcast)
{
    eca_client_remote_broadcast_t *item;

    item = malloc(sizeof(*item));
    if (item == NULL) {
        ECA_LOGE("remote broadcast queue alloc failed");
        return;
    }

    eca_client_remote_broadcast_copy(item, broadcast);

    if (queue->count == ECA_CLIENT_REMOTE_BROADCAST_QUEUE_SIZE) {
        free(queue->items[queue->read_index]);
        queue->items[queue->read_index] = NULL;
        queue->read_index = (queue->read_index + 1) %
            ECA_CLIENT_REMOTE_BROADCAST_QUEUE_SIZE;
        queue->count--;
        ECA_LOGW("remote broadcast queue full, drop oldest");
    }

    queue->items[queue->write_index] = item;
    queue->write_index = (queue->write_index + 1) %
        ECA_CLIENT_REMOTE_BROADCAST_QUEUE_SIZE;
    queue->count++;
}

static int eca_client_remote_broadcast_queue_pop(
    eca_client_remote_broadcast_queue_t *queue,
    eca_client_remote_broadcast_t *broadcast)
{
    if (queue->count == 0) {
        return -ENOENT;
    }

    *broadcast = *queue->items[queue->read_index];
    free(queue->items[queue->read_index]);
    queue->items[queue->read_index] = NULL;
    queue->read_index = (queue->read_index + 1) %
        ECA_CLIENT_REMOTE_BROADCAST_QUEUE_SIZE;
    queue->count--;
    return 0;
}

static void eca_client_remote_broadcast_to_alert(
    const eca_client_remote_broadcast_t *broadcast, eca_client_alert_t *alert)
{
    memset(alert, 0, sizeof(*alert));
    snprintf(alert->command_id, sizeof(alert->command_id), "%s",
             broadcast->command_id);
    snprintf(alert->title, sizeof(alert->title), "%s", broadcast->title);
    snprintf(alert->text, sizeof(alert->text), "%s", broadcast->text);
    snprintf(alert->source, sizeof(alert->source), "%s", broadcast->source);
    snprintf(alert->payload_json, sizeof(alert->payload_json), "%s",
             broadcast->payload_json);
    alert->received_at_ms = broadcast->received_at_ms;
}

static void eca_client_remote_broadcast_to_plan(
    const eca_client_remote_broadcast_t *broadcast, eca_client_plan_t *plan)
{
    memset(plan, 0, sizeof(*plan));
    snprintf(plan->command_id, sizeof(plan->command_id), "%s",
             broadcast->command_id);
    snprintf(plan->title, sizeof(plan->title), "%s", broadcast->title);
    snprintf(plan->text, sizeof(plan->text), "%s", broadcast->text);
    snprintf(plan->source, sizeof(plan->source), "%s", broadcast->source);
    snprintf(plan->payload_json, sizeof(plan->payload_json), "%s",
             broadcast->payload_json);
    plan->received_at_ms = broadcast->received_at_ms;
}

static void eca_client_remote_broadcast_from_alert(
    const eca_client_alert_t *alert, eca_client_remote_broadcast_t *broadcast)
{
    memset(broadcast, 0, sizeof(*broadcast));
    snprintf(broadcast->command_id, sizeof(broadcast->command_id), "%s",
             alert->command_id);
    snprintf(broadcast->kind, sizeof(broadcast->kind), "%s", "alert");
    snprintf(broadcast->title, sizeof(broadcast->title), "%s", alert->title);
    snprintf(broadcast->text, sizeof(broadcast->text), "%s", alert->text);
    snprintf(broadcast->source, sizeof(broadcast->source), "%s",
             alert->source[0] != '\0' ? alert->source : "alert_service");
    snprintf(broadcast->payload_json, sizeof(broadcast->payload_json), "%s",
             alert->payload_json);
    broadcast->received_at_ms = alert->received_at_ms;
}

static void eca_client_remote_broadcast_from_plan(
    const eca_client_plan_t *plan, eca_client_remote_broadcast_t *broadcast)
{
    memset(broadcast, 0, sizeof(*broadcast));
    snprintf(broadcast->command_id, sizeof(broadcast->command_id), "%s",
             plan->command_id);
    snprintf(broadcast->kind, sizeof(broadcast->kind), "%s", "plan");
    snprintf(broadcast->title, sizeof(broadcast->title), "%s", plan->title);
    snprintf(broadcast->text, sizeof(broadcast->text), "%s", plan->text);
    snprintf(broadcast->source, sizeof(broadcast->source), "%s",
             plan->source[0] != '\0' ? plan->source : "plan_service");
    snprintf(broadcast->payload_json, sizeof(broadcast->payload_json), "%s",
             plan->payload_json);
    broadcast->received_at_ms = plan->received_at_ms;
}

int eca_client_remote_broadcast_set_handler(
    eca_client_remote_broadcast_handler_t handler, void *arg)
{
    int ret;

    ret = eca_client_remote_broadcast_lock_init();
    if (ret < 0) {
        return ret;
    }

    pthread_mutex_lock(&g_remote_broadcast.lock);
    g_remote_broadcast.broadcast_handler = handler;
    g_remote_broadcast.broadcast_handler_arg = arg;
    pthread_mutex_unlock(&g_remote_broadcast.lock);
    return 0;
}

int eca_client_remote_broadcast_submit(
    const eca_client_remote_broadcast_t *broadcast)
{
    int ret;
    bool is_alert;
    bool is_plan;
    eca_client_remote_broadcast_handler_t broadcast_handler = NULL;
    void *broadcast_handler_arg = NULL;
    eca_client_alert_handler_t alert_handler = NULL;
    void *alert_handler_arg = NULL;
    eca_client_plan_handler_t plan_handler = NULL;
    void *plan_handler_arg = NULL;
    eca_client_alert_t alert;
    eca_client_plan_t plan;

    if (broadcast == NULL || broadcast->text[0] == '\0') {
        return -EINVAL;
    }

    ret = eca_client_remote_broadcast_lock_init();
    if (ret < 0) {
        return ret;
    }

    is_alert = eca_client_remote_broadcast_kind_is(broadcast, "alert");
    is_plan = eca_client_remote_broadcast_kind_is(broadcast, "plan");

    pthread_mutex_lock(&g_remote_broadcast.lock);
    eca_client_remote_broadcast_queue_push(&g_remote_broadcast.all_queue,
                                           broadcast);
    if (is_alert) {
        eca_client_remote_broadcast_queue_push(&g_remote_broadcast.alert_queue,
                                               broadcast);
    }
    if (is_plan) {
        eca_client_remote_broadcast_queue_push(&g_remote_broadcast.plan_queue,
                                               broadcast);
    }
    broadcast_handler = g_remote_broadcast.broadcast_handler;
    broadcast_handler_arg = g_remote_broadcast.broadcast_handler_arg;
    alert_handler = g_remote_broadcast.alert_handler;
    alert_handler_arg = g_remote_broadcast.alert_handler_arg;
    plan_handler = g_remote_broadcast.plan_handler;
    plan_handler_arg = g_remote_broadcast.plan_handler_arg;
    pthread_mutex_unlock(&g_remote_broadcast.lock);

    if (broadcast_handler != NULL) {
        broadcast_handler(broadcast, broadcast_handler_arg);
    }
    if (is_alert && alert_handler != NULL) {
        eca_client_remote_broadcast_to_alert(broadcast, &alert);
        alert_handler(&alert, alert_handler_arg);
    }
    if (is_plan && plan_handler != NULL) {
        eca_client_remote_broadcast_to_plan(broadcast, &plan);
        plan_handler(&plan, plan_handler_arg);
    }

    ECA_LOGI("remote broadcast received command_id=%s kind=%s source=%s text=%s",
             broadcast->command_id,
             broadcast->kind[0] != '\0' ? broadcast->kind : "message",
             broadcast->source[0] != '\0' ? broadcast->source : "app",
             broadcast->text);

    return 0;
}

int eca_client_remote_broadcast_pop(
    eca_client_remote_broadcast_t *broadcast)
{
    int ret;

    if (broadcast == NULL) {
        return -EINVAL;
    }

    ret = eca_client_remote_broadcast_lock_init();
    if (ret < 0) {
        return ret;
    }

    pthread_mutex_lock(&g_remote_broadcast.lock);
    ret = eca_client_remote_broadcast_queue_pop(&g_remote_broadcast.all_queue,
                                                broadcast);
    pthread_mutex_unlock(&g_remote_broadcast.lock);
    return ret;
}

int eca_client_alert_submit(const eca_client_alert_t *alert)
{
    eca_client_remote_broadcast_t *broadcast;
    int ret;

    if (alert == NULL || alert->text[0] == '\0') {
        return -EINVAL;
    }

    broadcast = malloc(sizeof(*broadcast));
    if (broadcast == NULL) {
        return -ENOMEM;
    }

    eca_client_remote_broadcast_from_alert(alert, broadcast);
    ret = eca_client_remote_broadcast_submit(broadcast);
    free(broadcast);
    return ret;
}

int eca_client_alert_set_handler(eca_client_alert_handler_t handler, void *arg)
{
    int ret;

    ret = eca_client_remote_broadcast_lock_init();
    if (ret < 0) {
        return ret;
    }

    pthread_mutex_lock(&g_remote_broadcast.lock);
    g_remote_broadcast.alert_handler = handler;
    g_remote_broadcast.alert_handler_arg = arg;
    pthread_mutex_unlock(&g_remote_broadcast.lock);
    return 0;
}

int eca_client_alert_pop(eca_client_alert_t *alert)
{
    int ret;
    eca_client_remote_broadcast_t *broadcast;

    if (alert == NULL) {
        return -EINVAL;
    }

    broadcast = malloc(sizeof(*broadcast));
    if (broadcast == NULL) {
        return -ENOMEM;
    }

    ret = eca_client_remote_broadcast_lock_init();
    if (ret < 0) {
        free(broadcast);
        return ret;
    }

    pthread_mutex_lock(&g_remote_broadcast.lock);
    ret = eca_client_remote_broadcast_queue_pop(&g_remote_broadcast.alert_queue,
                                                broadcast);
    if (ret == 0) {
        eca_client_remote_broadcast_to_alert(broadcast, alert);
    }
    pthread_mutex_unlock(&g_remote_broadcast.lock);
    free(broadcast);
    return ret;
}

int eca_client_plan_submit(const eca_client_plan_t *plan)
{
    eca_client_remote_broadcast_t *broadcast;
    int ret;

    if (plan == NULL || plan->text[0] == '\0') {
        return -EINVAL;
    }

    broadcast = malloc(sizeof(*broadcast));
    if (broadcast == NULL) {
        return -ENOMEM;
    }

    eca_client_remote_broadcast_from_plan(plan, broadcast);
    ret = eca_client_remote_broadcast_submit(broadcast);
    free(broadcast);
    return ret;
}

int eca_client_plan_set_handler(eca_client_plan_handler_t handler, void *arg)
{
    int ret;

    ret = eca_client_remote_broadcast_lock_init();
    if (ret < 0) {
        return ret;
    }

    pthread_mutex_lock(&g_remote_broadcast.lock);
    g_remote_broadcast.plan_handler = handler;
    g_remote_broadcast.plan_handler_arg = arg;
    pthread_mutex_unlock(&g_remote_broadcast.lock);
    return 0;
}

int eca_client_plan_pop(eca_client_plan_t *plan)
{
    int ret;
    eca_client_remote_broadcast_t *broadcast;

    if (plan == NULL) {
        return -EINVAL;
    }

    broadcast = malloc(sizeof(*broadcast));
    if (broadcast == NULL) {
        return -ENOMEM;
    }

    ret = eca_client_remote_broadcast_lock_init();
    if (ret < 0) {
        free(broadcast);
        return ret;
    }

    pthread_mutex_lock(&g_remote_broadcast.lock);
    ret = eca_client_remote_broadcast_queue_pop(&g_remote_broadcast.plan_queue,
                                                broadcast);
    if (ret == 0) {
        eca_client_remote_broadcast_to_plan(broadcast, plan);
    }
    pthread_mutex_unlock(&g_remote_broadcast.lock);
    free(broadcast);
    return ret;
}
