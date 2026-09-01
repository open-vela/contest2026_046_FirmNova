#include <nuttx/config.h>

#include <errno.h>
#include <ctype.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <netutils/cJSON.h>

#include "eca_client_ai_agent.h"
#include "eca_client_alert.h"
#include "eca_client_command_dispatcher.h"
#include "eca_client_device_state.h"
#include "eca_client_log.h"
#include "eca_client_plan.h"
#include "eca_client_remote_broadcast.h"
#include "rivotek_audio_service.h"

#define ECA_CLIENT_COMMAND_QUEUE_SIZE 4
#define ECA_CLIENT_COMMAND_PAYLOAD_MAX 1024
#define ECA_CLIENT_HANDLED_COMMANDS 16
#define ECA_CLIENT_COMMAND_THREAD_STACKSIZE 8192

typedef struct eca_client_command_item_s {
    char payload[ECA_CLIENT_COMMAND_PAYLOAD_MAX];
} eca_client_command_item_t;

typedef struct eca_client_handled_command_s {
    char command_id[64];
    char status[16];
    char error[64];
} eca_client_handled_command_t;

typedef struct eca_client_command_dispatcher_s {
    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    bool initialized;
    bool running;
    char device_id[32];
    eca_client_command_ack_cb_t ack_cb;
    void *ack_arg;
    eca_client_command_item_t queue[ECA_CLIENT_COMMAND_QUEUE_SIZE];
    int read_index;
    int write_index;
    int count;
    eca_client_handled_command_t handled[ECA_CLIENT_HANDLED_COMMANDS];
    int handled_index;
} eca_client_command_dispatcher_t;

typedef void (*eca_client_command_handler_t)(const char *command_id,
                                             cJSON *payload);

typedef struct eca_client_command_handler_entry_s {
    const char *type;
    eca_client_command_handler_t handler;
} eca_client_command_handler_entry_t;

static eca_client_command_dispatcher_t g_dispatcher;
static pthread_mutex_t g_observer_lock = PTHREAD_MUTEX_INITIALIZER;
static eca_client_command_dispatcher_observer_t g_observer;

void eca_client_command_dispatcher_set_observer(
    const eca_client_command_dispatcher_observer_t *observer)
{
    pthread_mutex_lock(&g_observer_lock);
    if (observer == NULL) {
        memset(&g_observer, 0, sizeof(g_observer));
    } else {
        g_observer = *observer;
    }
    pthread_mutex_unlock(&g_observer_lock);
}

static void eca_client_command_emit_event(const char *event)
{
    eca_client_command_event_cb_t event_cb;
    void *event_arg;

    pthread_mutex_lock(&g_observer_lock);
    event_cb = g_observer.event_cb;
    event_arg = g_observer.event_arg;
    pthread_mutex_unlock(&g_observer_lock);

    if (event_cb != NULL) {
        event_cb(event, event_arg);
    }
}

static long long eca_client_command_now_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
        return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    }

    return (long long)time(NULL) * 1000;
}

static void eca_client_command_ack(const char *command_id, const char *status,
                                   const char *error)
{
    if (g_dispatcher.ack_cb != NULL) {
        g_dispatcher.ack_cb(command_id, status, error, g_dispatcher.ack_arg);
    }
}

static int eca_client_command_normalize_device_id(const char *src,
                                                  char *dst,
                                                  size_t dst_size);

static const eca_client_handled_command_t *
eca_client_command_find_handled(const char *command_id)
{
    int i;

    for (i = 0; i < ECA_CLIENT_HANDLED_COMMANDS; i++) {
        if (strcmp(g_dispatcher.handled[i].command_id, command_id) == 0) {
            return &g_dispatcher.handled[i];
        }
    }

    return NULL;
}

static void eca_client_command_mark_handled(const char *command_id,
                                            const char *status,
                                            const char *error)
{
    eca_client_handled_command_t *handled;

    handled = &g_dispatcher.handled[g_dispatcher.handled_index];
    snprintf(handled->command_id, sizeof(handled->command_id), "%s", command_id);
    snprintf(handled->status, sizeof(handled->status), "%s", status);
    if (error == NULL) {
        handled->error[0] = '\0';
    } else {
        snprintf(handled->error, sizeof(handled->error), "%s", error);
    }

    g_dispatcher.handled_index =
        (g_dispatcher.handled_index + 1) % ECA_CLIENT_HANDLED_COMMANDS;
}

static bool eca_client_command_device_matches(const char *device_id)
{
    char current_device_id[sizeof(g_dispatcher.device_id)];
    char normalized_incoming[sizeof(g_dispatcher.device_id)];
    char normalized_current[sizeof(g_dispatcher.device_id)];
    bool matches;

    pthread_mutex_lock(&g_dispatcher.lock);
    snprintf(current_device_id, sizeof(current_device_id), "%s",
             g_dispatcher.device_id);
    pthread_mutex_unlock(&g_dispatcher.lock);

    matches = strcmp(device_id, current_device_id) == 0;
    if (!matches &&
        eca_client_command_normalize_device_id(device_id, normalized_incoming,
                                               sizeof(normalized_incoming)) == 0 &&
        eca_client_command_normalize_device_id(current_device_id,
                                               normalized_current,
                                               sizeof(normalized_current)) == 0) {
        matches = strcmp(normalized_incoming, normalized_current) == 0;
    }

    return matches;
}

static int eca_client_command_normalize_device_id(const char *src,
                                                  char *dst,
                                                  size_t dst_size)
{
    size_t pos = 0;

    if (src == NULL || dst == NULL || dst_size == 0) {
        return -EINVAL;
    }

    while (*src != '\0') {
        unsigned char ch = (unsigned char)*src++;

        if (ch == ':' || ch == '-' || ch == '_') {
            continue;
        }
        if (!isxdigit(ch) || pos + 1 >= dst_size) {
            return -EINVAL;
        }
        dst[pos++] = (char)toupper(ch);
    }

    if (pos != 12) {
        return -EINVAL;
    }

    dst[pos] = '\0';
    return 0;
}

static void eca_client_command_current_device_id(char *device_id,
                                                 size_t device_id_size)
{
    if (device_id == NULL || device_id_size == 0) {
        return;
    }

    pthread_mutex_lock(&g_dispatcher.lock);
    snprintf(device_id, device_id_size, "%s", g_dispatcher.device_id);
    pthread_mutex_unlock(&g_dispatcher.lock);
}

static void eca_client_command_copy_json_string(cJSON *root, const char *key,
                                                char *dst, size_t dst_size)
{
    cJSON *item = cJSON_IsObject(root) ?
        cJSON_GetObjectItemCaseSensitive(root, key) : NULL;

    if (cJSON_IsString(item) && item->valuestring != NULL &&
        item->valuestring[0] != '\0') {
        snprintf(dst, dst_size, "%s", item->valuestring);
    }
}

static void eca_client_command_copy_json_payload(cJSON *item, char *dst,
                                                 size_t dst_size)
{
    char *json;

    if (item == NULL || dst == NULL || dst_size == 0) {
        return;
    }

    json = cJSON_PrintUnformatted(item);
    if (json == NULL) {
        return;
    }

    snprintf(dst, dst_size, "%s", json);
    free(json);
}

static void eca_client_command_handle_play_message(const char *command_id,
                                                   cJSON *payload)
{
    cJSON *text = cJSON_IsObject(payload) ?
        cJSON_GetObjectItemCaseSensitive(payload, "text") : NULL;
    cJSON *audio_url = cJSON_IsObject(payload) ?
        cJSON_GetObjectItemCaseSensitive(payload, "audio_url") : NULL;
    cJSON *source = cJSON_IsObject(payload) ?
        cJSON_GetObjectItemCaseSensitive(payload, "source") : NULL;
    eca_client_remote_broadcast_t broadcast;

    if (!cJSON_IsString(text) || text->valuestring == NULL ||
        text->valuestring[0] == '\0') {
        eca_client_command_ack(command_id, "rejected", "missing text");
        eca_client_command_mark_handled(command_id, "rejected", "missing text");
        return;
    }

    memset(&broadcast, 0, sizeof(broadcast));
    snprintf(broadcast.command_id, sizeof(broadcast.command_id), "%s",
             command_id);
    snprintf(broadcast.kind, sizeof(broadcast.kind), "%s", "message");
    snprintf(broadcast.text, sizeof(broadcast.text), "%s", text->valuestring);
    eca_client_command_copy_json_payload(payload, broadcast.payload_json,
                                         sizeof(broadcast.payload_json));
    if (cJSON_IsString(audio_url) && audio_url->valuestring != NULL) {
        snprintf(broadcast.audio_url, sizeof(broadcast.audio_url), "%s",
                 audio_url->valuestring);
    }
    if (cJSON_IsString(source) && source->valuestring != NULL &&
        source->valuestring[0] != '\0') {
        snprintf(broadcast.source, sizeof(broadcast.source), "%s",
                 source->valuestring);
    } else {
        snprintf(broadcast.source, sizeof(broadcast.source), "%s", "app");
    }

    if (eca_client_remote_broadcast_submit(&broadcast) < 0) {
        eca_client_command_ack(command_id, "rejected", "play message failed");
        eca_client_command_mark_handled(command_id, "rejected",
                                        "play message failed");
        return;
    }

    eca_client_command_ack(command_id, "success", NULL);
    eca_client_ai_agent_speak(text->valuestring, NULL);
    eca_client_command_mark_handled(command_id, "success", NULL);
}

static void eca_client_command_handle_sync_plan(const char *command_id,
                                                cJSON *payload)
{
    cJSON *plan = cJSON_IsObject(payload) ?
        cJSON_GetObjectItemCaseSensitive(payload, "plan") : NULL;
    eca_client_remote_broadcast_t broadcast;

    if (!cJSON_IsObject(plan)) {
        eca_client_command_ack(command_id, "rejected", "missing plan");
        eca_client_command_mark_handled(command_id, "rejected", "missing plan");
        return;
    }

    memset(&broadcast, 0, sizeof(broadcast));
    snprintf(broadcast.command_id, sizeof(broadcast.command_id), "%s",
             command_id);
    snprintf(broadcast.kind, sizeof(broadcast.kind), "%s", "plan");
    eca_client_command_copy_json_string(payload, "title", broadcast.title,
                                        sizeof(broadcast.title));
    if (broadcast.title[0] == '\0') {
        eca_client_command_copy_json_string(plan, "title", broadcast.title,
                                            sizeof(broadcast.title));
    }
    eca_client_command_copy_json_string(payload, "text", broadcast.text,
                                        sizeof(broadcast.text));
    if (broadcast.text[0] == '\0') {
        snprintf(broadcast.text, sizeof(broadcast.text), "%s",
                 broadcast.title[0] != '\0' ? broadcast.title : "提醒计划已同步");
    }
    eca_client_command_copy_json_string(payload, "source", broadcast.source,
                                        sizeof(broadcast.source));
    if (broadcast.source[0] == '\0') {
        snprintf(broadcast.source, sizeof(broadcast.source), "%s",
                 "plan_service");
    }
    eca_client_command_copy_json_payload(plan, broadcast.payload_json,
                                         sizeof(broadcast.payload_json));

    if (eca_client_remote_broadcast_submit(&broadcast) < 0) {
        eca_client_command_ack(command_id, "rejected", "sync plan failed");
        eca_client_command_mark_handled(command_id, "rejected",
                                        "sync plan failed");
        return;
    }

    eca_client_command_ack(command_id, "success", NULL);
    eca_client_command_mark_handled(command_id, "success", NULL);
}

static void eca_client_command_handle_notify_alert(const char *command_id,
                                                   cJSON *payload)
{
    cJSON *alert = cJSON_IsObject(payload) ?
        cJSON_GetObjectItemCaseSensitive(payload, "alert") : NULL;
    eca_client_remote_broadcast_t broadcast;

    if (!cJSON_IsObject(alert)) {
        eca_client_command_ack(command_id, "rejected", "missing alert");
        eca_client_command_mark_handled(command_id, "rejected", "missing alert");
        return;
    }

    memset(&broadcast, 0, sizeof(broadcast));
    snprintf(broadcast.command_id, sizeof(broadcast.command_id), "%s",
             command_id);
    snprintf(broadcast.kind, sizeof(broadcast.kind), "%s", "alert");
    eca_client_command_copy_json_string(payload, "title", broadcast.title,
                                        sizeof(broadcast.title));
    if (broadcast.title[0] == '\0') {
        snprintf(broadcast.title, sizeof(broadcast.title), "%s", "告警通知");
    }
    eca_client_command_copy_json_string(payload, "text", broadcast.text,
                                        sizeof(broadcast.text));
    if (broadcast.text[0] == '\0') {
        eca_client_command_copy_json_string(alert, "message", broadcast.text,
                                            sizeof(broadcast.text));
    }
    if (broadcast.text[0] == '\0') {
        snprintf(broadcast.text, sizeof(broadcast.text), "%s", "设备告警");
    }
    eca_client_command_copy_json_string(payload, "source", broadcast.source,
                                        sizeof(broadcast.source));
    if (broadcast.source[0] == '\0') {
        snprintf(broadcast.source, sizeof(broadcast.source), "%s",
                 "alert_service");
    }
    eca_client_command_copy_json_payload(alert, broadcast.payload_json,
                                         sizeof(broadcast.payload_json));

    if (eca_client_remote_broadcast_submit(&broadcast) < 0) {
        eca_client_command_ack(command_id, "rejected", "notify alert failed");
        eca_client_command_mark_handled(command_id, "rejected",
                                        "notify alert failed");
        return;
    }

    eca_client_command_ack(command_id, "success", NULL);
    eca_client_command_mark_handled(command_id, "success", NULL);
}

static void eca_client_command_handle_set_volume(const char *command_id,
                                                 cJSON *payload)
{
    cJSON *volume = cJSON_IsObject(payload) ?
        cJSON_GetObjectItemCaseSensitive(payload, "volume") : NULL;
    int value;

    if (!cJSON_IsNumber(volume)) {
        eca_client_command_ack(command_id, "rejected", "missing volume");
        eca_client_command_mark_handled(command_id, "rejected", "missing volume");
        return;
    }

    value = (int)volume->valueint;
    if (rivotek_audio_service_set_volume(value) < 0) {
        eca_client_command_ack(command_id, "rejected", "set volume failed");
        eca_client_command_mark_handled(command_id, "rejected",
                                        "set volume failed");
        return;
    }

    eca_client_device_state_set_volume(value);
    ECA_LOGI("set volume %d", value);
    eca_client_command_ack(command_id, "success", NULL);
    eca_client_command_mark_handled(command_id, "success", NULL);
}

static void eca_client_command_handle_complete_registration(const char *command_id,
                                                           cJSON *payload)
{
    (void)payload;

    eca_client_command_emit_event("registration_completed");
    eca_client_command_ack(command_id, "success", NULL);
    eca_client_command_mark_handled(command_id, "success", NULL);
}

static const eca_client_command_handler_entry_t g_command_handlers[] = {
    {"play_message", eca_client_command_handle_play_message},
    {"sync_plan", eca_client_command_handle_sync_plan},
    {"notify_alert", eca_client_command_handle_notify_alert},
    {"set_volume", eca_client_command_handle_set_volume},
    {"complete_registration", eca_client_command_handle_complete_registration},
};

static eca_client_command_handler_t
eca_client_command_find_handler(const char *type)
{
    size_t i;

    for (i = 0; i < sizeof(g_command_handlers) / sizeof(g_command_handlers[0]);
         i++) {
        if (strcmp(g_command_handlers[i].type, type) == 0) {
            return g_command_handlers[i].handler;
        }
    }

    return NULL;
}

static void eca_client_command_process(const char *payload)
{
    const eca_client_handled_command_t *handled;
    eca_client_command_handler_t handler;
    cJSON *root;
    cJSON *item;
    cJSON *command_payload;
    char current_device_id[32];
    const char *command_id = NULL;
    const char *device_id = NULL;
    const char *type = NULL;

    root = cJSON_Parse(payload);
    if (root == NULL) {
        ECA_LOGW("command json parse failed payload=%s", payload);
        return;
    }

    item = cJSON_GetObjectItemCaseSensitive(root, "command_id");
    if (cJSON_IsString(item)) {
        command_id = item->valuestring;
    }
    item = cJSON_GetObjectItemCaseSensitive(root, "device_id");
    if (cJSON_IsString(item)) {
        device_id = item->valuestring;
    }
    item = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (cJSON_IsString(item)) {
        type = item->valuestring;
    }

    if (command_id == NULL || type == NULL || device_id == NULL) {
        ECA_LOGW("command dropped: missing required field command_id=%s type=%s device_id=%s payload=%s",
                 command_id != NULL ? command_id : "(null)",
                 type != NULL ? type : "(null)",
                 device_id != NULL ? device_id : "(null)", payload);
        cJSON_Delete(root);
        return;
    }

    if (!eca_client_command_device_matches(device_id)) {
        eca_client_command_current_device_id(current_device_id,
                                             sizeof(current_device_id));
        ECA_LOGW("command dropped: device_id mismatch incoming=%s current=%s command_id=%s type=%s",
                 device_id, current_device_id, command_id, type);
        cJSON_Delete(root);
        return;
    }

    ECA_LOGI("command received command_id=%s type=%s device_id=%s",
             command_id, type, device_id);

    item = cJSON_GetObjectItemCaseSensitive(root, "expires_at");
    if (cJSON_IsNumber(item) &&
        (long long)item->valuedouble < eca_client_command_now_ms()) {
        ECA_LOGW("command expired command_id=%s type=%s", command_id, type);
        eca_client_command_ack(command_id, "expired", NULL);
        eca_client_command_mark_handled(command_id, "expired", NULL);
        cJSON_Delete(root);
        return;
    }

    handled = eca_client_command_find_handled(command_id);
    if (handled != NULL) {
        ECA_LOGI("command duplicate command_id=%s status=%s", command_id,
                 handled->status);
        eca_client_command_ack(command_id, handled->status,
                               handled->error[0] == '\0' ? NULL : handled->error);
        cJSON_Delete(root);
        return;
    }

    command_payload = cJSON_GetObjectItemCaseSensitive(root, "payload");
    handler = eca_client_command_find_handler(type);
    if (handler != NULL) {
        handler(command_id, command_payload);
    } else {
        ECA_LOGW("unsupported command %s", type);
        eca_client_command_ack(command_id, "rejected", "unsupported command");
        eca_client_command_mark_handled(command_id, "rejected",
                                        "unsupported command");
    }

    cJSON_Delete(root);
}

static bool eca_client_command_pop(char *payload, size_t payload_size)
{
    bool has_item = false;

    pthread_mutex_lock(&g_dispatcher.lock);
    while (g_dispatcher.running && g_dispatcher.count == 0) {
        pthread_cond_wait(&g_dispatcher.cond, &g_dispatcher.lock);
    }

    if (g_dispatcher.count > 0) {
        snprintf(payload, payload_size, "%s",
                 g_dispatcher.queue[g_dispatcher.read_index].payload);
        g_dispatcher.read_index =
            (g_dispatcher.read_index + 1) % ECA_CLIENT_COMMAND_QUEUE_SIZE;
        g_dispatcher.count--;
        has_item = true;
    }

    pthread_mutex_unlock(&g_dispatcher.lock);
    return has_item;
}

static void *eca_client_command_thread(void *arg)
{
    char payload[ECA_CLIENT_COMMAND_PAYLOAD_MAX];

    (void)arg;
    while (eca_client_command_pop(payload, sizeof(payload))) {
        eca_client_command_process(payload);
    }

    return NULL;
}

int eca_client_command_dispatcher_start(const char *device_id,
                                        eca_client_command_ack_cb_t ack_cb,
                                        void *ack_arg)
{
    pthread_attr_t attr;
    bool attr_initialized = false;
    int ret;

    if (g_dispatcher.initialized) {
        return 0;
    }

    memset(&g_dispatcher, 0, sizeof(g_dispatcher));
    ret = pthread_mutex_init(&g_dispatcher.lock, NULL);
    if (ret != 0) {
        return -ret;
    }

    ret = pthread_cond_init(&g_dispatcher.cond, NULL);
    if (ret != 0) {
        pthread_mutex_destroy(&g_dispatcher.lock);
        return -ret;
    }

    snprintf(g_dispatcher.device_id, sizeof(g_dispatcher.device_id), "%s",
             device_id);
    g_dispatcher.ack_cb = ack_cb;
    g_dispatcher.ack_arg = ack_arg;
    g_dispatcher.running = true;

    ret = pthread_attr_init(&attr);
    if (ret == 0) {
        attr_initialized = true;
        ret = pthread_attr_setstacksize(&attr,
                                        ECA_CLIENT_COMMAND_THREAD_STACKSIZE);
    }
    if (ret != 0) {
        if (attr_initialized) {
            pthread_attr_destroy(&attr);
        }
        g_dispatcher.running = false;
        pthread_cond_destroy(&g_dispatcher.cond);
        pthread_mutex_destroy(&g_dispatcher.lock);
        return -ret;
    }

    ret = pthread_create(&g_dispatcher.thread, &attr, eca_client_command_thread,
                         NULL);
    pthread_attr_destroy(&attr);
    if (ret != 0) {
        g_dispatcher.running = false;
        pthread_cond_destroy(&g_dispatcher.cond);
        pthread_mutex_destroy(&g_dispatcher.lock);
        return -ret;
    }

    g_dispatcher.initialized = true;
    return 0;
}

int eca_client_command_dispatcher_update_device_id(const char *device_id)
{
    if (device_id == NULL || device_id[0] == '\0') {
        return -EINVAL;
    }

    if (!g_dispatcher.initialized) {
        return -ENODEV;
    }

    pthread_mutex_lock(&g_dispatcher.lock);
    if (strcmp(g_dispatcher.device_id, device_id) != 0) {
        ECA_LOGI("command dispatcher device_id updated %s -> %s",
                 g_dispatcher.device_id, device_id);
        snprintf(g_dispatcher.device_id, sizeof(g_dispatcher.device_id), "%s",
                 device_id);
    }
    pthread_mutex_unlock(&g_dispatcher.lock);
    return 0;
}

void eca_client_command_dispatcher_stop(void)
{
    if (!g_dispatcher.initialized) {
        return;
    }

    pthread_mutex_lock(&g_dispatcher.lock);
    g_dispatcher.running = false;
    pthread_cond_signal(&g_dispatcher.cond);
    pthread_mutex_unlock(&g_dispatcher.lock);

    pthread_join(g_dispatcher.thread, NULL);
    pthread_cond_destroy(&g_dispatcher.cond);
    pthread_mutex_destroy(&g_dispatcher.lock);
    memset(&g_dispatcher, 0, sizeof(g_dispatcher));
}

int eca_client_command_dispatcher_enqueue(const char *payload)
{
    if (payload == NULL || strlen(payload) >= ECA_CLIENT_COMMAND_PAYLOAD_MAX) {
        return -EINVAL;
    }

    pthread_mutex_lock(&g_dispatcher.lock);
    if (!g_dispatcher.running) {
        pthread_mutex_unlock(&g_dispatcher.lock);
        return -ENODEV;
    }

    if (g_dispatcher.count == ECA_CLIENT_COMMAND_QUEUE_SIZE) {
        pthread_mutex_unlock(&g_dispatcher.lock);
        ECA_LOGW("command queue full");
        return -ENOSPC;
    }

    snprintf(g_dispatcher.queue[g_dispatcher.write_index].payload,
             sizeof(g_dispatcher.queue[g_dispatcher.write_index].payload),
             "%s", payload);
    g_dispatcher.write_index =
        (g_dispatcher.write_index + 1) % ECA_CLIENT_COMMAND_QUEUE_SIZE;
    g_dispatcher.count++;
    pthread_cond_signal(&g_dispatcher.cond);
    pthread_mutex_unlock(&g_dispatcher.lock);
    return 0;
}
