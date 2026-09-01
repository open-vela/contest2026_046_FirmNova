/*
 * ai_agent_lib.c — Library entry point for the Rivotek AI Agent framework.
 *
 * Wraps the original agent_main startup sequence behind ai_agent_run(profile).
 * agent_main.c is kept as-is in the tree for reference / original app compat;
 * this file is the one compiled into the vendor library (CSRCS in Makefile).
 */

#include "ai_agent_lib.h"
#include "ai_agent_profile.h"
#include "agent_compat.h"
#include "agent_config.h"

#include "core/agent_loop.h"
#include "core/message_bus.h"
#include "core/message_bus_tap.h"
#include "core/memory_store.h"
#include "core/session_mgr.h"
#include "channels/nsh_commands.h"
#include "channels/ws_server.h"
#include "infra/config_store.h"
#include "infra/cron_service.h"
#include "infra/heartbeat.h"
#include "infra/http_proxy.h"
#include "infra/network_manager.h"
#include "infra/vela_tls.h"
#include "llm/llm_proxy.h"
#include "llm/llm_router.h"
#include "llm/llm_cache.h"
#include "tools/tool_registry.h"
#include "tools/tool_guard.h"
#include "tools/skill_loader.h"
#include "tools/tool_media.h"
#include "voice/voice_channel.h"
#ifdef CONFIG_AI_AGENT_FEISHU
#include "channels/feishu_bot.h"
#endif
#ifdef CONFIG_AI_AGENT_WEIXIN
#include "channels/weixin_channel.h"
#endif
#ifdef CONFIG_AI_AGENT_MQTT
#include "channels/mqtt_channel.h"
#endif
#ifdef CONFIG_AI_AGENT_NODE
#include "node/node_client.h"
#include "node/node_manager.h"
#endif
#ifdef CONFIG_AI_AGENT_MCP
#include "tools/mcp_bridge.h"
#endif
#ifdef CONFIG_AI_AGENT_LVGL_UI
#include "ui/lvgl_ui_channel.h"
#endif
#if AGENT_SKILL_SYNC_ENABLED
#include "tools/skill_sync.h"
#endif

#include <errno.h>
#include <malloc.h>
#include <pthread.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static const char *TAG = "ai_agent_lib";

/* ── singleton state ─────────────────────────────────────────── */

static volatile bool                s_running;
static volatile bool                s_shutdown;
static const ai_agent_profile_t    *s_profile;
pthread_mutex_t                     g_stdout_lock = PTHREAD_MUTEX_INITIALIZER;

/* ── helpers ─────────────────────────────────────────────────── */

static inline long boot_ms(struct timespec *t0)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (long)((now.tv_sec - t0->tv_sec) * 1000L +
                  (now.tv_nsec - t0->tv_nsec) / 1000000L);
}

#define BLOG(t0, ph, msg) \
    syslog(LOG_INFO, "[%s] [+%ldms] " ph ": " msg "\n", TAG, boot_ms(t0))
#define BLOGRC(t0, ph, msg, rc) \
    syslog((rc)==OK ? LOG_INFO : LOG_WARNING, \
           "[%s] [+%ldms] " ph ": " msg " rc=%d\n", TAG, boot_ms(t0), (rc))

/* ── TTS file polling: consumes /tmp/tts_request.txt from EcaClient ── */
#define TTS_REQUEST_PATH "/tmp/tts_request.txt"
#define TTS_PROCESSING_PATH "/tmp/tts_request.processing"
#define VOICE_REQUEST_PATH "/tmp/voice_request.txt"
#define VOICE_PROCESSING_PATH "/tmp/voice_request.processing"
#define VOICE_STATUS_PATH "/tmp/voice_status.txt"
#define TTS_STATUS_PATH "/tmp/tts_status.txt"
#define TTS_POLL_INTERVAL_US 500000  /* 500ms */
#define VOICE_POLL_INTERVAL_US 100000  /* 100ms */
#define VOICE_DIALOG_TIMEOUT_SEC 90

static volatile bool s_voice_dialog_pending;
static volatile time_t s_voice_dialog_deadline;

static void request_write_status(const char *path, const char *status)
{
    FILE *fp = fopen(path, "w");

    if (fp != NULL) {
        fputs(status, fp);
        fclose(fp);
    }
}

static void voice_request_write_status(const char *status)
{
    request_write_status(VOICE_STATUS_PATH, status);
}

static void *voice_request_poll_task(void *arg)
{
    (void)arg;
    voice_request_write_status("idle");
    unlink(VOICE_PROCESSING_PATH);
    syslog(LOG_INFO, "[voice_poll] Thread started, polling %s\n",
           VOICE_REQUEST_PATH);

    while (!s_shutdown) {
        FILE *fp = NULL;

        if (s_voice_dialog_pending &&
            time(NULL) >= s_voice_dialog_deadline) {
            s_voice_dialog_pending = false;
            voice_request_write_status("error");
            syslog(LOG_WARNING,
                   "[voice_poll] dialog timeout, accepting new request\n");
        }

        if (rename(VOICE_REQUEST_PATH, VOICE_PROCESSING_PATH) == 0) {
            fp = fopen(VOICE_PROCESSING_PATH, "r");
        }

        if (fp != NULL) {
            char command[16] = {0};
            int ret = -EINVAL;

            if (fgets(command, sizeof(command), fp) != NULL) {
                size_t len = strlen(command);

                while (len > 0 &&
                       (command[len - 1] == '\n' || command[len - 1] == '\r')) {
                    command[--len] = '\0';
                }

                if (strcmp(command, "start") == 0) {
                    ret = s_voice_dialog_pending ? -EBUSY
                                                 : voice_channel_start();
                    voice_request_write_status(ret == 0 ? "recording" : "error");
                } else if (strcmp(command, "stop") == 0) {
                    voice_request_write_status("processing");
                    ret = voice_channel_stop();
                    if (ret == 0) {
                        s_voice_dialog_pending = true;
                        s_voice_dialog_deadline = time(NULL) +
                                                  VOICE_DIALOG_TIMEOUT_SEC;
                    } else {
                        voice_request_write_status("error");
                    }
                }

                syslog(ret == 0 ? LOG_INFO : LOG_WARNING,
                       "[voice_poll] command=%s ret=%d\n", command, ret);
            }
            fclose(fp);
            unlink(VOICE_PROCESSING_PATH);
        }
        usleep(VOICE_POLL_INTERVAL_US);
    }

    unlink(VOICE_STATUS_PATH);
    syslog(LOG_INFO, "[voice_poll] Thread exiting\n");
    return NULL;
}

static void *tts_request_poll_task(void *arg)
{
    (void)arg;
    unlink(TTS_PROCESSING_PATH);
    request_write_status(TTS_STATUS_PATH, "idle");
    syslog(LOG_INFO, "[tts_poll] Thread started, polling %s\n", TTS_REQUEST_PATH);

    while (!s_shutdown) {
        FILE *fp = NULL;

        if (rename(TTS_REQUEST_PATH, TTS_PROCESSING_PATH) == 0) {
            fp = fopen(TTS_PROCESSING_PATH, "r");
        }

        if (fp != NULL) {
            char text[512];
            if (fgets(text, sizeof(text), fp) != NULL) {
                size_t len = strlen(text);
                while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r'))
                    text[--len] = '\0';
                if (len > 0) {
                    syslog(LOG_INFO, "[tts_poll] Got request (%zu bytes): %s\n", len, text);
                    request_write_status(TTS_STATUS_PATH, "speaking");
                    int ret = voice_channel_speak(text);
                    request_write_status(TTS_STATUS_PATH, "idle");
                    syslog(LOG_INFO, "[tts_poll] voice_channel_speak returned %d\n", ret);
                } else {
                    syslog(LOG_WARNING, "[tts_poll] Empty TTS request, skipping\n");
                }
            }
            fclose(fp);
            unlink(TTS_PROCESSING_PATH);
            syslog(LOG_INFO, "[tts_poll] Request file consumed and removed\n");
        }
        usleep(TTS_POLL_INTERVAL_US);
    }
    unlink(TTS_STATUS_PATH);
    syslog(LOG_INFO, "[tts_poll] Thread exiting\n");
    return NULL;
}

/* ── profile helpers ─────────────────────────────────────────── */

static const char *pstr(const char *v, const char *def)
{
    return (v && v[0]) ? v : def;
}

static int pint(int v, int def)
{
    return v > 0 ? v : def;
}

/* Build default data_dir from project_id: "/data/<id>" */
static char s_data_dir_buf[64];
static char s_romfs_buf[64];
static char s_skills_buf[80];

static void resolve_paths(const ai_agent_profile_t *p)
{
    if (p->data_dir && p->data_dir[0]) {
        /* use as-is */
    } else {
        snprintf(s_data_dir_buf, sizeof(s_data_dir_buf), "/data/%s", p->project_id);
    }
    if (p->romfs_root && p->romfs_root[0]) {
        /* use as-is */
    } else {
        snprintf(s_romfs_buf, sizeof(s_romfs_buf), "/etc/%s", p->project_id);
    }
    const char *romfs = pstr(p->romfs_root, s_romfs_buf);
    if (p->extra_skills_dir && p->extra_skills_dir[0]) {
        /* use as-is */
    } else {
        snprintf(s_skills_buf, sizeof(s_skills_buf), "%s/skills", romfs);
    }
}

static const char *data_dir(void)
{
    const char *d = s_profile->data_dir;
    return (d && d[0]) ? d : s_data_dir_buf;
}

/* mkdir -p two levels */
static void mkdirs2(const char *base, const char *sub)
{
    char path[128];
    snprintf(path, sizeof(path), "%s/%s", base, sub);
    mkdir(path, 0755);
}

/* Seed a single file from romfs → data_dir if target absent */
static void seed_file(const char *src, const char *dst)
{
    struct stat st;
    if (stat(dst, &st) == 0) return;   /* already exists */
    FILE *in = fopen(src, "r");
    if (!in) return;
    FILE *out = fopen(dst, "w");
    if (!out) { fclose(in); return; }
    char buf[256];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
        fwrite(buf, 1, n, out);
    fclose(in);
    fclose(out);
    syslog(LOG_INFO, "[%s] seeded %s\n", TAG, dst);
}

static void romfs_seed(void)
{
    const char *romfs = pstr(s_profile->romfs_root, s_romfs_buf);
    const char *dd    = data_dir();
    char src[128], dst[128];

    snprintf(src, sizeof(src), "%s/SOUL.md", romfs);
    snprintf(dst, sizeof(dst), "%s/config/SOUL.md", dd);
    seed_file(src, dst);

    snprintf(src, sizeof(src), "%s/USER.md", romfs);
    snprintf(dst, sizeof(dst), "%s/config/USER.md", dd);
    seed_file(src, dst);

    snprintf(src, sizeof(src), "%s/MEMORY.md", romfs);
    snprintf(dst, sizeof(dst), "%s/memory/MEMORY.md", dd);
    seed_file(src, dst);

    /* skills: copy each .md that is absent in dst */
    const char *skills_src = pstr(s_profile->extra_skills_dir, s_skills_buf);
    DIR *dir = opendir(skills_src);
    if (!dir) return;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        size_t nl = strlen(ent->d_name);
        if (nl < 4 || strcmp(ent->d_name + nl - 3, ".md") != 0) continue;
        snprintf(src, sizeof(src), "%s/%s", skills_src, ent->d_name);
        snprintf(dst, sizeof(dst), "%s/skills/%s", dd, ent->d_name);
        seed_file(src, dst);
    }
    closedir(dir);
}

/* ── validate ────────────────────────────────────────────────── */

static int validate_profile(const ai_agent_profile_t *p)
{
    if (!p) return -EINVAL;
    if (p->abi_version != AI_AGENT_PROFILE_ABI_V1) {
        syslog(LOG_ERR, "[%s] ABI mismatch: got %u want %u\n",
               TAG, p->abi_version, AI_AGENT_PROFILE_ABI_V1);
        return -ENOSYS;
    }
    if (!p->project_id || !p->project_id[0] || strlen(p->project_id) > 16) {
        syslog(LOG_ERR, "[%s] project_id invalid\n", TAG);
        return -EINVAL;
    }
    if (p->llm_timeout_sec < 0 || p->llm_timeout_sec > 300) {
        syslog(LOG_ERR, "[%s] llm_timeout_sec out of range\n", TAG);
        return -EINVAL;
    }
    return OK;
}

/* ── inject LLM defaults into config_store ───────────────────── */

static void inject_llm_defaults(void)
{
    if (!s_profile->default_llm_backend || !s_profile->default_llm_backend[0])
        return;

    /* Only inject when no backend has been configured yet */
    llm_backend_t existing;
    if (llm_router_get_backend(0, &existing) == 0 && existing.enabled)
        return;

    llm_backend_t b;
    memset(&b, 0, sizeof(b));

    /* Parse "https://host/path" or preset name */
    const char *url = s_profile->default_llm_backend;
    const char *p = strstr(url, "://");
    if (p) {
        p += 3;  /* skip scheme */
        const char *slash = strchr(p, '/');
        if (slash) {
            size_t hlen = (size_t)(slash - p);
            if (hlen >= sizeof(b.host)) hlen = sizeof(b.host) - 1;
            memcpy(b.host, p, hlen);
            /* Append /chat/completions if path is just "/v1" */
            if (strstr(slash, "chat/completions"))
                strncpy(b.path, slash, sizeof(b.path) - 1);
            else {
                snprintf(b.path, sizeof(b.path), "%s/chat/completions", slash);
            }
        }
    } else {
        /* preset — let llm_proxy handle it; just store as host */
        strncpy(b.host, url, sizeof(b.host) - 1);
        strncpy(b.path, "/v1/chat/completions", sizeof(b.path) - 1);
    }

    if (s_profile->default_llm_model)
        strncpy(b.model, s_profile->default_llm_model, sizeof(b.model) - 1);
    if (s_profile->default_llm_api_key)
        strncpy(b.api_key, s_profile->default_llm_api_key, sizeof(b.api_key) - 1);

    b.enabled = true;
    b.cost_tier = 1;

    llm_router_set_backend(0, &b);
}

/* ── outbound dispatch ───────────────────────────────────────── */

static volatile bool        s_voice_cooldown;
static time_t               s_voice_cooldown_until;

static void *outbound_dispatch_task(void *arg)
{
    (void)arg;
    while (!s_shutdown) {
        agent_msg_t msg;
        if (message_bus_pop_outbound(&msg, 1000) != OK) continue;
        msg.channel[sizeof(msg.channel)-1] = '\0';
        msg.chat_id[sizeof(msg.chat_id)-1]  = '\0';
        if (!msg.content) continue;

        /* project outbound hook */
        if (s_profile->on_outbound_message &&
            s_profile->on_outbound_message(&msg, s_profile->user_ctx) > 0) {
            free(msg.content);
            continue;
        }

        if (mbus_tap_try_deliver(&msg)) { free(msg.content); continue; }

        if (strcmp(msg.channel, AGENT_CHAN_FEISHU) == 0) {
            bool done = false;
#ifdef CONFIG_AI_AGENT_FEISHU
            if (s_profile->enable_feishu) {
                const char *app_id = feishu_get_app_id();
                if (app_id && app_id[0]) {
                    feishu_send_message(msg.chat_id, msg.content);
                    done = true;
                }
            }
#endif
#ifdef CONFIG_AI_AGENT_NODE
            if (!done && s_profile->enable_node)
                done = node_client_send_chat_message(
                    msg.channel, msg.chat_id, msg.content) == OK;
#endif
            if (!done)
                syslog(LOG_WARNING, "[%s] feishu msg dropped\n", TAG);

        } else if (strcmp(msg.channel, AGENT_CHAN_WEBSOCKET) == 0) {
            if (s_profile->enable_websocket)
                ws_server_send(msg.chat_id, msg.content);
#ifdef CONFIG_AI_AGENT_MQTT
        } else if (strcmp(msg.channel, AGENT_CHAN_MQTT) == 0) {
            if (s_profile->enable_mqtt)
                mqtt_channel_send(msg.chat_id, msg.content);
#endif
        } else if (strcmp(msg.channel, AGENT_CHAN_VOICE) == 0) {
            int voice_ret = 0;

            if (s_voice_cooldown && time(NULL) < s_voice_cooldown_until) {
                syslog(LOG_WARNING, "[%s] voice cooldown\n", TAG);
                voice_ret = -EBUSY;
            } else {
                s_voice_cooldown = false;
                voice_ret = voice_channel_speak(msg.content);
                if (voice_ret != 0) {
                    s_voice_cooldown = true;
                    s_voice_cooldown_until = time(NULL) + 5;
                }
            }
            s_voice_dialog_pending = false;
            voice_request_write_status(voice_ret == 0 ? "idle" : "error");
#ifdef CONFIG_AI_AGENT_LVGL_UI
        } else if (strcmp(msg.channel, AGENT_CHAN_LVGL_UI) == 0) {
            if (s_profile->enable_lvgl_ui)
                lvgl_ui_channel_send(msg.content);
#endif
#ifdef CONFIG_AI_AGENT_WEIXIN
        } else if (strcmp(msg.channel, AGENT_CHAN_WEIXIN) == 0) {
            if (s_profile->enable_weixin) {
                char uid[64] = ""; const char *ctx = "";
                char *sep = strchr(msg.chat_id, '|');
                if (sep) {
                    size_t ul = (size_t)(sep - msg.chat_id);
                    if (ul >= sizeof(uid)) ul = sizeof(uid) - 1;
                    memcpy(uid, msg.chat_id, ul); uid[ul] = '\0';
                    ctx = sep + 1;
                } else {
                    strncpy(uid, msg.chat_id, sizeof(uid) - 1);
                }
                weixin_channel_send(uid, ctx, msg.content);
            }
#endif
        } else if (strcmp(msg.channel, "cli") == 0) {
            pthread_mutex_lock(&g_stdout_lock);
            printf("\n[Agent]: %s\nvela> ", msg.content);
            fflush(stdout);
            pthread_mutex_unlock(&g_stdout_lock);
        } else {
            syslog(LOG_WARNING, "[%s] unknown channel: %s\n", TAG, msg.channel);
        }
        free(msg.content);
    }
    return NULL;
}

/* ── network watcher ─────────────────────────────────────────── */

#ifdef CONFIG_AI_AGENT_NET_RPMSG
static volatile bool s_net_started;
static void net_state_cb(net_state_t state, void *arg)
{
    (void)arg;
    if (state == NET_STATE_CONNECTED && !s_net_started) {
#ifdef CONFIG_AI_AGENT_FEISHU
        if (s_profile->enable_feishu) feishu_bot_start();
#endif
        agent_loop_start();
        if (s_profile->enable_websocket) ws_server_start();
#ifdef CONFIG_AI_AGENT_NODE
        if (s_profile->enable_node) node_client_start();
#endif
#ifdef CONFIG_AI_AGENT_MQTT
        if (s_profile->enable_mqtt) mqtt_channel_start();
#endif
#ifdef CONFIG_AI_AGENT_WEIXIN
        if (s_profile->enable_weixin) weixin_channel_start();
#endif
        s_net_started = true;
    }
}
#endif

static void *network_watch_task(void *arg)
{
    (void)arg;
    network_wifi_reconnect();
    if (network_wait_connected(30000) != OK) {
        syslog(LOG_WARNING, "[%s] network timeout\n", TAG);
#ifdef CONFIG_AI_AGENT_NET_RPMSG
        network_register_listener(net_state_cb, NULL);
        while (!s_shutdown) sleep(1);
#endif
        return NULL;
    }

#if AGENT_SKILL_SYNC_ENABLED
    skill_sync_from_bitable();
#endif
#ifdef CONFIG_AI_AGENT_FEISHU
    if (s_profile->enable_feishu) feishu_bot_start();
#endif
    agent_loop_start();
    if (s_profile->enable_websocket) ws_server_start();
#ifdef CONFIG_AI_AGENT_NODE
    if (s_profile->enable_node) node_client_start();
#endif
#ifdef CONFIG_AI_AGENT_MQTT
    if (s_profile->enable_mqtt) mqtt_channel_start();
#endif
#ifdef CONFIG_AI_AGENT_WEIXIN
    if (s_profile->enable_weixin) weixin_channel_start();
#endif

    /* on_ready hook */
    if (s_profile->on_ready)
        s_profile->on_ready(s_profile->user_ctx);

#ifdef CONFIG_AI_AGENT_NET_RPMSG
    s_net_started = true;
    network_register_listener(net_state_cb, NULL);
    while (!s_shutdown) sleep(1);
#endif
    return NULL;
}

/* ── public API ──────────────────────────────────────────────── */

void ai_agent_request_shutdown(void) { s_shutdown = true; }
bool ai_agent_shutdown_requested(void) { return s_shutdown; }
const ai_agent_profile_t *ai_agent_current_profile(void) { return s_profile; }

int ai_agent_run(const ai_agent_profile_t *profile)
{
    int rc;

    rc = validate_profile(profile);
    if (rc != OK) return rc;

    if (s_running) {
        syslog(LOG_ERR, "[%s] already running\n", TAG);
        return -EBUSY;
    }
    s_running  = true;
    s_shutdown = false;
    s_profile  = profile;

    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    syslog(LOG_INFO, "[%s] starting project=%s build=%s\n",
           TAG, profile->project_id, AGENT_BUILD_VERSION);

    /* on_pre_init hook */
    if (profile->on_pre_init) {
        rc = profile->on_pre_init(profile->user_ctx);
        if (rc != OK) { s_running = false; return -EIO; }
    }

    /* P0: timezone + paths */
    resolve_paths(profile);
    setenv("TZ", pstr(profile->timezone, AGENT_TIMEZONE), 1);
#ifdef CONFIG_LIBC_LOCALTIME
    tzset();
#endif
    BLOG(&t0, "P0", "timezone set");

    const char *dd = data_dir();
    struct stat st;
    if (stat("/data", &st) != 0)
        mount(NULL, "/data", "tmpfs", 0, NULL);

    mkdir(dd, 0755);
    mkdirs2(dd, "config");
    mkdirs2(dd, "memory");
    mkdirs2(dd, "sessions");
    mkdirs2(dd, "skills");
    romfs_seed();
    BLOG(&t0, "P0", "storage ready");

    /* P1 */
    rc = config_store_init(); BLOGRC(&t0, "P1", "config_store", rc);
    inject_llm_defaults();
    rc = message_bus_init();  BLOGRC(&t0, "P1", "message_bus",  rc);
    if (rc != OK) { s_running = false; return -ENOMEM; }
    memory_store_init();
    session_mgr_init();

    /* P2 */
    http_proxy_init();

    /* P3 */
#ifdef CONFIG_AI_AGENT_FEISHU
    if (profile->enable_feishu) { rc = feishu_bot_init(); BLOGRC(&t0, "P3", "feishu_bot", rc); }
#endif
    llm_proxy_init();
    llm_router_init();
#ifdef CONFIG_AI_AGENT_NODE
    if (profile->enable_node) { rc = node_manager_init(); BLOGRC(&t0, "P3", "node_manager", rc); }
#endif
    rc = tool_registry_init(); BLOGRC(&t0, "P3", "tool_registry", rc);

    /* inject project tools */
    if (profile->project_get_tools && profile->project_exec)
        tool_registry_register_provider(profile->project_id,
                                        profile->project_get_tools,
                                        profile->project_exec);

    tool_guard_init();
    skill_loader_init();
    agent_loop_init();
    cron_service_init();
    heartbeat_init();
#ifdef CONFIG_AI_AGENT_NODE
    if (profile->enable_node) node_client_init();
#endif
#ifdef CONFIG_AI_AGENT_MQTT
    if (profile->enable_mqtt) mqtt_channel_init();
#endif
    voice_channel_init();
#ifdef CONFIG_AI_AGENT_WEIXIN
    if (profile->enable_weixin) weixin_channel_init();
#endif
#ifdef CONFIG_AI_AGENT_LVGL_UI
    if (profile->enable_lvgl_ui) lvgl_ui_channel_init();
#endif

    /* on_post_init hook */
    if (profile->on_post_init) {
        rc = profile->on_post_init(profile->user_ctx);
        if (rc != OK) { s_running = false; return -EIO; }
    }
    BLOG(&t0, "P3", "all services init");

    /* P4 */
    if (profile->enable_cli) nsh_commands_init();

    /* P5 */
    agent_task_create(outbound_dispatch_task, "outbound",
                      AGENT_OUTBOUND_STACK, NULL, AGENT_OUTBOUND_PRIO);
    cron_service_start();
    heartbeat_start();

    /* Start TTS file polling thread (reads /tmp/tts_request.txt from EcaClient) */
    agent_task_create(tts_request_poll_task, "tts_poll",
                      AGENT_OUTBOUND_STACK, NULL, AGENT_OUTBOUND_PRIO);
    agent_task_create(voice_request_poll_task, "voice_poll",
                      AGENT_OUTBOUND_STACK, NULL, AGENT_OUTBOUND_PRIO);
#ifdef CONFIG_AI_AGENT_LVGL_UI
    if (profile->enable_lvgl_ui) lvgl_ui_channel_start();
#endif
    agent_task_create(network_watch_task, "net_watch",
                      AGENT_OUTBOUND_STACK, NULL, AGENT_OUTBOUND_PRIO);

    /* P6 */
    if (profile->enable_cli) nsh_commands_start();
    BLOG(&t0, "P6", "ready");

    while (!s_shutdown) sleep(1);

    /* on_shutdown hook */
    if (profile->on_shutdown)
        profile->on_shutdown(profile->user_ctx);

    /* teardown */
    message_bus_wakeup();
#ifdef CONFIG_AI_AGENT_WEIXIN
    if (profile->enable_weixin) weixin_channel_stop();
#endif
#ifdef CONFIG_AI_AGENT_NODE
    if (profile->enable_node) node_client_stop();
#endif
#ifdef CONFIG_AI_AGENT_MQTT
    if (profile->enable_mqtt) mqtt_channel_stop();
#endif
    if (profile->enable_websocket) ws_server_stop();
#ifdef CONFIG_AI_AGENT_LVGL_UI
    if (profile->enable_lvgl_ui) lvgl_ui_channel_stop();
#endif
    cron_service_stop();
    heartbeat_stop();
    usleep(500 * 1000);

    tool_media_cleanup();
#ifdef CONFIG_AI_AGENT_MCP
    mcp_bridge_cleanup();
#endif
    tool_guard_cleanup();
    tool_registry_cleanup();
    llm_cache_cleanup();
    vela_tls_pool_cleanup();
    message_bus_destroy();

    s_profile = NULL;
    s_running = false;
    syslog(LOG_INFO, "[%s] shutdown complete\n", TAG);
    return OK;
}
