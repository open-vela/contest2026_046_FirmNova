#include <nuttx/config.h>

#include "eca_client_registration.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "eca_client_log.h"
#include "eca_client_command_dispatcher.h"
#include "eca_client_network.h"
#include "eca_client_ui_common.h"

#ifdef CONFIG_RIVOTEK_QRCODE_SERVICE
#include "rvt_qrcode.h"
#endif
#ifdef CONFIG_RIVOTEK_PROVISIONING_SERVICE
#include "rvt_provisioning_service.h"
#endif

#define ECA_CLIENT_REG_QR_TTL_SECONDS 0
#define ECA_CLIENT_REG_STATUS_CHECK_INTERVAL_MS 3000
#define ECA_CLIENT_REG_FLAG_PATH "/data/eca_client_registered.flag"
#define ECA_CLIENT_WIFI_CONFIG_PATH "/data/etc/wifi/wapi.conf"

typedef struct eca_client_registration_state_s {
    pthread_mutex_t lock;
    bool lock_initialized;
    bool initialized;
    bool registered;
    bool qrcode_visible;
    bool completion_handled;
    bool status_check_running;
    bool status_auth_logged;
    long long last_status_check_ms;
    eca_client_model_t *model;
} eca_client_registration_state_t;

static eca_client_registration_state_t g_registration;

#ifdef CONFIG_RIVOTEK_QRCODE_SERVICE
static void eca_client_registration_apply_completed(void);
#endif

static long long eca_client_registration_now_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
        return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    }

    return (long long)time(NULL) * 1000;
}

static bool eca_client_registration_file_nonempty(const char *path)
{
    struct stat st;

    if (path == NULL) {
        return false;
    }

    return stat(path, &st) == 0 && st.st_size > 0;
}

static int eca_client_registration_write_local_completed(void)
{
    static const char value[] = "1\n";
    int fd;
    int ret = 0;

    fd = open(ECA_CLIENT_REG_FLAG_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        return -errno;
    }

    if (write(fd, value, sizeof(value) - 1) != (ssize_t)(sizeof(value) - 1)) {
        ret = -EIO;
    }
    fsync(fd);
    close(fd);

    return ret;
}

static bool eca_client_registration_load_local_completed(void)
{
    if (eca_client_registration_file_nonempty(ECA_CLIENT_REG_FLAG_PATH)) {
        return true;
    }

#ifdef CONFIG_RIVOTEK_PROVISIONING_SERVICE
    if (rvt_provisioning_has_provisioned() &&
        eca_client_registration_file_nonempty(ECA_CLIENT_WIFI_CONFIG_PATH)) {
        int ret = eca_client_registration_write_local_completed();

        if (ret < 0) {
            ECA_LOGW("registration local flag migration failed ret=%d", ret);
        } else {
            ECA_LOGI("registration restored from provisioned wifi state");
        }
        return true;
    }
#endif

    return false;
}

static void eca_client_registration_command_event(const char *event, void *arg)
{
    (void)arg;

    if (event != NULL && strcmp(event, "registration_completed") == 0) {
        eca_client_registration_complete();
    }
}

static int eca_client_registration_check_remote_status(void)
{
    eca_client_registration_status_t status;
    int ret;

    ret = eca_client_network_get_registration_status(&status);
    if (ret < 0) {
        return ret;
    }

    if (status.registered || status.bound) {
        ECA_LOGI("registration already completed device_id=%s registered=%d bound=%d bound_to_current_user=%d claim_status=%s",
                 status.device_id,
                 status.registered ? 1 : 0,
                 status.bound ? 1 : 0,
                 status.bound_to_current_user ? 1 : 0,
                 status.claim_status);
        eca_client_registration_complete();
        return 1;
    }

    return 0;
}

static bool eca_client_registration_is_url_safe(char ch)
{
    return (ch >= 'A' && ch <= 'Z') ||
           (ch >= 'a' && ch <= 'z') ||
           (ch >= '0' && ch <= '9') ||
           ch == '-' || ch == '_' || ch == '.' || ch == '~';
}

static void eca_client_registration_url_encode(const char *src,
                                               char *dst,
                                               size_t dst_size)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t pos = 0;

    if (dst_size == 0) {
        return;
    }

    while (src != NULL && *src != '\0' && pos + 1 < dst_size) {
        unsigned char ch = (unsigned char)*src++;

        if (eca_client_registration_is_url_safe((char)ch)) {
            dst[pos++] = (char)ch;
        } else if (pos + 3 < dst_size) {
            dst[pos++] = '%';
            dst[pos++] = hex[(ch >> 4) & 0x0f];
            dst[pos++] = hex[ch & 0x0f];
        } else {
            break;
        }
    }

    dst[pos] = '\0';
}

static void *eca_client_registration_status_thread(void *arg)
{
    int ret;

    (void)arg;
    ret = eca_client_registration_check_remote_status();
    if (ret == -EACCES) {
        bool should_log;

        pthread_mutex_lock(&g_registration.lock);
        should_log = !g_registration.status_auth_logged;
        g_registration.status_auth_logged = true;
        pthread_mutex_unlock(&g_registration.lock);

        if (should_log) {
            ECA_LOGW("registration status check skipped: http token is missing or disabled");
        }
    } else if (ret < 0) {
        ECA_LOGD("registration status check failed ret=%d", ret);
    } else if (ret == 0) {
        ECA_LOGI("registration status checked: not registered yet");
    }

    pthread_mutex_lock(&g_registration.lock);
    g_registration.status_check_running = false;
    pthread_mutex_unlock(&g_registration.lock);

    return NULL;
}

static void eca_client_registration_maybe_check_status(bool force)
{
    pthread_t thread;
    long long now_ms;
    long long last_check_ms;
    bool status_check_running;
    bool registered;
    int ret;

    if (!g_registration.lock_initialized) {
        return;
    }

    now_ms = eca_client_registration_now_ms();
    pthread_mutex_lock(&g_registration.lock);
    registered = g_registration.registered;
    status_check_running = g_registration.status_check_running;
    last_check_ms = g_registration.last_status_check_ms;
    if (registered || status_check_running ||
        (!force &&
         last_check_ms > 0 &&
         now_ms - last_check_ms < ECA_CLIENT_REG_STATUS_CHECK_INTERVAL_MS)) {
        pthread_mutex_unlock(&g_registration.lock);
        return;
    }

    g_registration.status_check_running = true;
    g_registration.last_status_check_ms = now_ms;
    pthread_mutex_unlock(&g_registration.lock);

    ret = pthread_create(&thread, NULL,
                         eca_client_registration_status_thread, NULL);
    if (ret != 0) {
        pthread_mutex_lock(&g_registration.lock);
        g_registration.status_check_running = false;
        pthread_mutex_unlock(&g_registration.lock);
        ECA_LOGW("registration status thread create failed ret=%d", ret);
        return;
    }

    pthread_detach(thread);
}

#if defined(CONFIG_RIVOTEK_PROVISIONING_SERVICE) && \
    defined(CONFIG_RIVOTEK_QRCODE_SERVICE)
static void eca_client_registration_provisioning_event_cb(
    rvt_provisioning_event_t event,
    const rvt_provisioning_session_t *session,
    void *user_data)
{
    (void)session;
    (void)user_data;

    if (event == RVT_PROVISIONING_EVENT_CONNECTED) {
        ECA_LOGI("provisioning connected, complete registration locally");
        eca_client_network_notify_wifi_ready();
        eca_client_registration_complete();
        eca_client_registration_maybe_check_status(true);
    } else if (event == RVT_PROVISIONING_EVENT_FAILED ||
               event == RVT_PROVISIONING_EVENT_STOPPED) {
        ECA_LOGW("provisioning finished without connection event=%d",
                 (int)event);
    }
}
#endif

#ifdef CONFIG_RIVOTEK_PROVISIONING_SERVICE
static int eca_client_registration_get_softap(
    rvt_provisioning_session_t *session)
{
    int ret;

    if (session == NULL) {
        return -EINVAL;
    }

    memset(session, 0, sizeof(*session));
    ret = rvt_provisioning_get_session(session);
    if (ret == 0 && rvt_provisioning_is_running() &&
        session->ap_ssid[0] != '\0' && session->ap_password[0] != '\0') {
        return 0;
    }

    ret = rvt_provisioning_start(RVT_PROVISIONING_TYPE_WIFI);
    if (ret < 0 && ret != -EALREADY) {
        return ret;
    }

    memset(session, 0, sizeof(*session));
    ret = rvt_provisioning_get_session(session);
    if (ret < 0) {
        return ret;
    }

    if (session->ap_ssid[0] == '\0' || session->ap_password[0] == '\0') {
        return -ENODEV;
    }

    return 0;
}

static void eca_client_registration_stop_softap(void)
{
    rvt_provisioning_session_t session;

    memset(&session, 0, sizeof(session));
    if (rvt_provisioning_get_session(&session) == 0 &&
        session.state == RVT_PROVISIONING_STATE_CONNECTED) {
        return;
    }

    if (rvt_provisioning_is_running()) {
        rvt_provisioning_stop();
    }
}
#endif

#ifdef CONFIG_RIVOTEK_QRCODE_SERVICE
static void eca_client_registration_apply_completed(void)
{
    bool completion_handled;
    eca_client_model_t *model;
    int ret;

    pthread_mutex_lock(&g_registration.lock);
    completion_handled = g_registration.completion_handled;
    model = g_registration.model;
    g_registration.registered = true;
    g_registration.qrcode_visible = false;
    g_registration.completion_handled = true;
    pthread_mutex_unlock(&g_registration.lock);

    ret = eca_client_registration_write_local_completed();
    if (ret < 0) {
        ECA_LOGW("registration local flag save failed ret=%d", ret);
    }

    if (!completion_handled && model != NULL) {
        eca_client_model_set_registered(model, true);
    }
#ifdef CONFIG_RIVOTEK_PROVISIONING_SERVICE
    eca_client_registration_stop_softap();
#endif
    rvt_qrcode_hide();
    if (!completion_handled) {
        ECA_LOGI("registration completed");
    }
}
#endif

static int eca_client_registration_build_payload(char *payload,
                                                 size_t payload_size)
{
    eca_client_registration_info_t info;
    char device_id[96];
    char device_name[128];
    char claim_token[160];
    char api[192];
#ifdef CONFIG_RIVOTEK_PROVISIONING_SERVICE
    rvt_provisioning_session_t session;
    char ssid[32];
    char psk[64];
    char ip[32];
    char nonce[32];
#endif
    int ret;

#ifdef CONFIG_RIVOTEK_PROVISIONING_SERVICE
    ret = eca_client_registration_get_softap(&session);
    if (ret < 0) {
        ECA_LOGD("registration softap unavailable ret=%d", ret);
        return ret;
    }
#endif

    ret = eca_client_network_get_registration_info(&info);
    if (ret < 0) {
        return ret;
    }

    eca_client_registration_url_encode(info.device_id, device_id,
                                       sizeof(device_id));
    eca_client_registration_url_encode(info.device_name, device_name,
                                       sizeof(device_name));
    eca_client_registration_url_encode(info.claim_token, claim_token,
                                       sizeof(claim_token));
    eca_client_registration_url_encode(info.http_base_url, api, sizeof(api));

#ifdef CONFIG_RIVOTEK_PROVISIONING_SERVICE
    eca_client_registration_url_encode(session.ap_ssid, ssid, sizeof(ssid));
    eca_client_registration_url_encode(session.ap_password, psk, sizeof(psk));
    eca_client_registration_url_encode(session.ip, ip, sizeof(ip));
    eca_client_registration_url_encode(session.nonce, nonce, sizeof(nonce));

    ret = snprintf(payload, payload_size,
                   "eca://register?v=1&device_id=%s&name=%s&claim_token=%s&api=%s"
                   "&ap_ssid=%s&ap_psk=%s&ap_ip=%s&ap_port=%d&ap_nonce=%s",
                   device_id, device_name, claim_token, api,
                   ssid, psk, ip, session.port, nonce);
#else
    ret = snprintf(payload, payload_size,
                   "eca://register?v=1&device_id=%s&name=%s&claim_token=%s&api=%s",
                   device_id, device_name, claim_token, api);
#endif
    if (ret < 0 || (size_t)ret >= payload_size) {
        ECA_LOGW("registration QR payload too long ret=%d max=%zu",
                 ret, payload_size);
        return -ENOSPC;
    }

#ifdef CONFIG_RIVOTEK_PROVISIONING_SERVICE
    ECA_LOGI("registration QR payload ready len=%d device_id=%s api=%s claim_token_set=%d ssid=%s",
             ret, info.device_id, info.http_base_url,
             info.claim_token[0] != '\0' ? 1 : 0, session.ap_ssid);
#else
    ECA_LOGI("registration QR payload ready len=%d device_id=%s api=%s claim_token_set=%d",
             ret, info.device_id, info.http_base_url,
             info.claim_token[0] != '\0' ? 1 : 0);
#endif

    return 0;
}

int eca_client_registration_init(lv_obj_t *screen, eca_client_model_t *model)
{
#ifdef CONFIG_RIVOTEK_QRCODE_SERVICE
    eca_client_command_dispatcher_observer_t observer;
    const eca_client_ui_fonts_t *fonts;
    rvt_qrcode_ui_metrics_t qrcode_ui;
    bool local_registered;
    int ret;

    if (!g_registration.lock_initialized) {
        ret = pthread_mutex_init(&g_registration.lock, NULL);
        if (ret != 0) {
            return -ret;
        }
        g_registration.lock_initialized = true;
    }

    fonts = eca_client_ui_fonts_get();
    memset(&qrcode_ui, 0, sizeof(qrcode_ui));
    qrcode_ui.disp_w = lv_obj_get_width(screen);
    qrcode_ui.disp_h = lv_obj_get_height(screen);
    qrcode_ui.font_16 = fonts->font_14;
    qrcode_ui.font_20 = fonts->font_18;
    qrcode_ui.font_28 = fonts->font_bold_24;
    qrcode_ui.font_40 = fonts->font_bold_28;

    rvt_qrcode_init(screen, &qrcode_ui);
    local_registered = eca_client_registration_load_local_completed();

    pthread_mutex_lock(&g_registration.lock);
    g_registration.model = model;
    g_registration.initialized = true;
    g_registration.registered = local_registered;
    g_registration.qrcode_visible = false;
    g_registration.completion_handled = local_registered;
    g_registration.status_check_running = false;
    g_registration.status_auth_logged = false;
    g_registration.last_status_check_ms = 0;
    pthread_mutex_unlock(&g_registration.lock);

    if (model != NULL) {
        eca_client_model_set_registered(model, local_registered);
    }

    observer.event_cb = eca_client_registration_command_event;
    observer.event_arg = NULL;
    eca_client_command_dispatcher_set_observer(&observer);

#ifdef CONFIG_RIVOTEK_PROVISIONING_SERVICE
    rvt_provisioning_set_event_callback(
        eca_client_registration_provisioning_event_cb, NULL);
#endif

    return 0;
#else
    (void)screen;
    (void)model;
    return -ENOSYS;
#endif
}

int eca_client_registration_show_qrcode(void)
{
#ifdef CONFIG_RIVOTEK_QRCODE_SERVICE
    rvt_qrcode_request_t request;
    char payload[RVT_QRCODE_PAYLOAD_MAX_LEN];
    int ret;

    if (!g_registration.initialized) {
        return -ENODEV;
    }

    if (eca_client_registration_is_registered()) {
        ECA_LOGI("registration qrcode skipped: already registered locally");
        return 0;
    }

    ret = eca_client_registration_check_remote_status();
    if (ret > 0) {
        return 0;
    }
    if (ret == -EACCES) {
        pthread_mutex_lock(&g_registration.lock);
        if (!g_registration.status_auth_logged) {
            g_registration.status_auth_logged = true;
            ECA_LOGW("registration status check skipped: http token is missing or disabled");
        }
        pthread_mutex_unlock(&g_registration.lock);
    } else if (ret < 0) {
        ECA_LOGD("registration status check failed ret=%d", ret);
    } else {
        ECA_LOGI("registration status checked before QR: not registered yet");
    }

    ret = eca_client_registration_build_payload(payload, sizeof(payload));
    if (ret < 0) {
        return ret;
    }

    memset(&request, 0, sizeof(request));
    request.biz = RVT_QRCODE_BIZ_AI_DEVICE;
    request.payload = payload;
    request.title = "设备注册";
    request.primary_text = "手机扫码注册";
    request.secondary_text = "注册成功后自动进入";
    request.hint_text = "请使用手机 App 扫描二维码绑定当前设备。";
    request.ttl_seconds = ECA_CLIENT_REG_QR_TTL_SECONDS;

    ret = rvt_qrcode_show(&request);
    if (ret < 0) {
#ifdef CONFIG_RIVOTEK_PROVISIONING_SERVICE
        eca_client_registration_stop_softap();
#endif
        return ret;
    }

    pthread_mutex_lock(&g_registration.lock);
    g_registration.qrcode_visible = true;
    g_registration.last_status_check_ms = eca_client_registration_now_ms();
    pthread_mutex_unlock(&g_registration.lock);

    return 0;
#else
    return -ENOSYS;
#endif
}

void eca_client_registration_complete(void)
{
#ifdef CONFIG_RIVOTEK_QRCODE_SERVICE
    eca_client_registration_apply_completed();
#endif
}

void eca_client_registration_poll(void)
{
#ifdef CONFIG_RIVOTEK_QRCODE_SERVICE
    bool registered;
    bool qrcode_visible;
    bool completion_handled;

    if (!g_registration.initialized) {
        rvt_qrcode_poll();
        return;
    }

    pthread_mutex_lock(&g_registration.lock);
    registered = g_registration.registered;
    qrcode_visible = g_registration.qrcode_visible;
    completion_handled = g_registration.completion_handled;
    pthread_mutex_unlock(&g_registration.lock);

    if (registered) {
        if (!completion_handled || qrcode_visible) {
            eca_client_registration_apply_completed();
        }

        rvt_qrcode_poll();
        return;
    }

    if (qrcode_visible) {
        eca_client_registration_maybe_check_status(false);
    }

    if (!registered && !qrcode_visible) {
        if (eca_client_registration_show_qrcode() < 0) {
            ECA_LOGD("show registration qrcode failed");
        }
    }

    rvt_qrcode_poll();
#endif
}

bool eca_client_registration_is_registered(void)
{
    bool registered;

    if (!g_registration.lock_initialized) {
        return false;
    }

    pthread_mutex_lock(&g_registration.lock);
    registered = g_registration.registered;
    pthread_mutex_unlock(&g_registration.lock);
    return registered;
}
