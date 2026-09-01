#include "rvt_provisioning_service.h"
#include "rvt_provisioning_protocol.h"
#include "rvt_provisioning_wifi_port.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sched.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define PROV_LOG(...) dprintf(STDOUT_FILENO, __VA_ARGS__)
#define RVT_PROVISIONING_QR_TTL_SECONDS 120
#define RVT_PROVISIONING_CONNECTING_UI_TTL_SECONDS 90
#define RVT_PROVISIONING_SUCCESS_UI_TTL_SECONDS 2
#define RVT_PROVISIONING_FAILURE_UI_TTL_SECONDS 5
#define RVT_PROVISIONING_WAIT_STEP_MS 500
#define RVT_PROVISIONING_CONFIG_RX_MAX 768
#define RVT_PROVISIONING_PHONE_ID_MAX 64
#define RVT_PROVISIONING_PHONE_RANDOM_MAX 32
#define RVT_PROVISIONING_AUTH_TYPE_MAX 16
#define RVT_PROVISIONING_FLAG_PATH "/data/rvt_provisioned.flag"
#define RVT_PROVISIONING_WAPI_CONFIG_PATH "/data/etc/wifi/wapi.conf"
#define RVT_PROVISIONING_WAPI_CONFIG_DIR "/data/etc/wifi"

/* 配网任务优先级和栈大小允许 Kconfig 覆盖，未配置时使用调试默认值。 */
#ifndef CONFIG_RIVOTEK_PROVISIONING_SERVICE_PRIORITY
#define CONFIG_RIVOTEK_PROVISIONING_SERVICE_PRIORITY 100
#endif

#ifndef CONFIG_RIVOTEK_PROVISIONING_SERVICE_STACKSIZE
#define CONFIG_RIVOTEK_PROVISIONING_SERVICE_STACKSIZE 8192
#endif

/* g_softap_running 表示车机临时 AP 仍在工作。 */
static int g_softap_running;
/* stop 请求由 CLI/UI/API 置位，worker 线程轮询后尽快退出。 */
static volatile int g_stop_requested;
/* 每次 start/stop 都递增 session_id，用于让旧 worker 自动失效，避免异步回调串会话。 */
static volatile unsigned int g_session_id;
/* 当前配网会话快照。回调前会复制一份，避免 UI 侧持有可变全局对象。 */
static rvt_provisioning_session_t g_session;
static rvt_provisioning_event_cb_t g_event_cb;
static void *g_event_user_data;
static rvt_provisioning_network_status_cb_t g_network_status_cb;
static void *g_network_status_user_data;
static int g_provisioned_cache_valid;
static int g_provisioned_cache;

/* APP 连接车机 SoftAP 后，通过 TCP/HTTP form 下发的手机热点配置。 */
typedef struct rvt_provisioning_phone_config {
    char nonce[RVT_PROV_NONCE_LEN + 1];
    /* APP 生成的手机侧标识，只用于日志和状态关联，不等同于手机 MAC。 */
    char phone_id[RVT_PROVISIONING_PHONE_ID_MAX + 1];
    char phone_random[RVT_PROVISIONING_PHONE_RANDOM_MAX + 1];
    char ssid[RVT_PROV_SSID_MAX_LEN + 1];
    char password[64];
    char auth_type[RVT_PROVISIONING_AUTH_TYPE_MAX + 1];
} rvt_provisioning_phone_config_t;

/*
 * 函数描述：读取内存缓存或持久化文件中的配网完成标志。
 * 入参：无。
 * 返回值：已配网返回 1；未配网或读取失败返回 0。
 */
static int provisioning_read_provisioned_flag(void)
{
    if (g_provisioned_cache_valid) {
        return g_provisioned_cache ? 1 : 0;
    }

    if (access(RVT_PROVISIONING_FLAG_PATH, 0) == 0) {
        g_provisioned_cache_valid = 1;
        g_provisioned_cache = 1;
        return 1;
    }

    return 0;
}

/*
 * 函数描述：写入或清除配网完成标志。
 * 入参：
 *   provisioned - 非 0 表示已配网；0 表示未配网。
 * 返回值：成功返回 0；持久化失败返回负 errno。
 */
static int provisioning_write_provisioned_flag(int provisioned)
{
    int ret = 0;
    int fd;

    if (provisioned) {
        static const char value[] = "1\n";

        fd = open(RVT_PROVISIONING_FLAG_PATH,
                  O_WRONLY | O_CREAT | O_TRUNC,
                  0666);
        if (fd < 0) {
            ret = -EIO;
        } else {
            if (write(fd, value, sizeof(value) - 1) !=
                (int)(sizeof(value) - 1)) {
                ret = -EIO;
            }
            fsync(fd);
            close(fd);
        }
    } else {
        if (access(RVT_PROVISIONING_FLAG_PATH, 0) == 0 &&
            unlink(RVT_PROVISIONING_FLAG_PATH) != 0) {
            ret = -EIO;
        }
        if (access(RVT_PROVISIONING_WAPI_CONFIG_PATH, 0) == 0 &&
            unlink(RVT_PROVISIONING_WAPI_CONFIG_PATH) != 0 &&
            ret == 0) {
            ret = -EIO;
        }
    }

    g_provisioned_cache_valid = 1;
    g_provisioned_cache = provisioned ? 1 : 0;
    return ret;
}

/*
 * 函数描述：配网成功后置位配网完成标志，失败只打日志，不影响主流程。
 * 入参：无。
 * 返回值：无。
 */
static void provisioning_mark_success(void)
{
    int ret = provisioning_write_provisioned_flag(1);

    if (ret < 0) {
        PROV_LOG("provisioning: save provisioned flag failed, ret=%d\n",
                 ret);
    }
}

/*
 * 函数描述：把字符串写入 JSON 字符串字段，转义引号、反斜杠和控制字符。
 * 入参：
 *   fd - 目标文件描述符。
 *   value - 原始字符串。
 * 返回值：成功返回 0；写入失败返回负 errno。
 */
static int provisioning_write_json_string(int fd, const char *value)
{
    const char *p;
    char escaped[7];

    if (write(fd, "\"", 1) != 1) {
        return -EIO;
    }

    for (p = value ? value : ""; *p != '\0'; p++) {
        unsigned char c = (unsigned char)*p;

        if (c == '"' || c == '\\') {
            escaped[0] = '\\';
            escaped[1] = (char)c;
            if (write(fd, escaped, 2) != 2) {
                return -EIO;
            }
        } else if (c < 0x20) {
            snprintf(escaped, sizeof(escaped), "\\u%04x", c);
            if (write(fd, escaped, 6) != 6) {
                return -EIO;
            }
        } else if (write(fd, &c, 1) != 1) {
            return -EIO;
        }
    }

    return write(fd, "\"", 1) == 1 ? 0 : -EIO;
}

/*
 * 函数描述：配网成功后保存车机回连所需的 WiFi 配置到 WAPI 配置文件。
 * 入参：
 *   config - APP 下发且已连接成功的目标热点配置。
 * 返回值：成功返回 0；创建目录或写文件失败返回负 errno。
 */
static int provisioning_save_wapi_config(
    const rvt_provisioning_phone_config_t *config)
{
    static const char prefix[] =
        "{\"wlan0\":{\"mode\":2,\"auth\":4,\"cmode\":8,\"alg\":3,\"ssid\":";
    static const char middle[] = ",\"bssid\":\"\",\"psk\":";
    int fd;
    int ret = 0;

    if (!config || config->ssid[0] == '\0' || config->password[0] == '\0') {
        return -EINVAL;
    }

    mkdir("/data/etc", 0777);
    mkdir(RVT_PROVISIONING_WAPI_CONFIG_DIR, 0777);

    fd = open(RVT_PROVISIONING_WAPI_CONFIG_PATH,
              O_WRONLY | O_CREAT | O_TRUNC,
              0666);
    if (fd < 0) {
        return -EIO;
    }

    if (write(fd, prefix, strlen(prefix)) != (ssize_t)strlen(prefix)) {
        ret = -EIO;
    }
    if (ret == 0) {
        ret = provisioning_write_json_string(fd, config->ssid);
    }
    if (ret == 0 && write(fd, middle, strlen(middle)) !=
        (ssize_t)strlen(middle)) {
        ret = -EIO;
    }
    if (ret == 0) {
        ret = provisioning_write_json_string(fd, config->password);
    }
    if (ret == 0 &&
        write(fd, "}}\n", 3) != 3) {
        ret = -EIO;
    }

    fsync(fd);
    close(fd);
    if (ret < 0) {
        unlink(RVT_PROVISIONING_WAPI_CONFIG_PATH);
    }
    return ret;
}

/*
 * 函数描述：默认 UI 通知 hook，未被 UI 模块覆盖时不执行任何操作。
 * 入参：
 *   event - 配网事件类型。
 *   session - 当前配网会话快照。
 * 返回值：无。
 */
__attribute__((weak))
void rvt_provisioning_ui_notify(rvt_provisioning_event_t event,
                                const rvt_provisioning_session_t *session)
{
    (void)event;
    (void)session;
}

/*
 * 函数描述：默认网络状态 hook，未被业务模块覆盖时不执行任何操作。
 * 入参：
 *   info - 网络状态信息。
 * 返回值：无。
 */
__attribute__((weak))
void rvt_provisioning_network_status_notify(
    const rvt_provisioning_network_status_info_t *info)
{
    (void)info;
}

/*
 * 函数描述：分发配网事件给 QR service 回调和可选 UI hook。
 * 入参：
 *   event - 要分发的配网事件。
 * 返回值：无。
 */
static void provisioning_notify(rvt_provisioning_event_t event)
{
    rvt_provisioning_event_cb_t cb = g_event_cb;
    rvt_provisioning_session_t snapshot = g_session;

    if (cb) {
        cb(event, &snapshot, g_event_user_data);
    }

    rvt_provisioning_ui_notify(event, &snapshot);
}

/*
 * 函数描述：对日志中的 phone_id 做脱敏处理，只保留头尾。
 * 入参：
 *   value - 原始 phone_id。
 *   out - 输出脱敏字符串。
 *   out_len - 输出缓冲区长度。
 * 返回值：无。
 */
static void provisioning_mask_id(const char *value, char *out, size_t out_len)
{
    size_t len;

    if (out == NULL || out_len == 0) {
        return;
    }
    if (value == NULL || value[0] == '\0') {
        snprintf(out, out_len, "-");
        return;
    }

    len = strlen(value);
    if (len <= 8) {
        snprintf(out, out_len, "%s", value);
        return;
    }

    snprintf(out, out_len, "%.*s...%.*s",
             4, value, 4, value + len - 4);
}

/*
 * 函数描述：根据当前 SoftAP 会话生成 APP 扫码使用的 v=3 二维码 payload。
 * 入参：
 *   session - 当前配网会话，必须包含nt、 ap_ssid、ap_password、ip、port、nonce。
 *             nt： hotspot 或者 router
 * 返回值：无。
 */
static void provisioning_build_softap_qr_payload(
    rvt_provisioning_session_t *session)
{
    snprintf(session->qr_payload, sizeof(session->qr_payload),
             "rvt://wifi-pair?v=3&m=ap&nt=hotspot&ssid=%s&psk=%s&ip=%s&port=%d&n=%s&exp=%d",
             session->ap_ssid,
             session->ap_password,
             session->ip,
             session->port,
             session->nonce,
             session->ttl_seconds);
}

/*
 * 函数描述：把单个十六进制字符转换为数值。
 * 入参：
 *   c - 待转换字符。
 * 返回值：合法十六进制字符返回 0-15；非法返回 -1。
 */
static int provisioning_hex_value(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/*
 * 函数描述：解码 application/x-www-form-urlencoded 字段值中的 + 和 %XX。
 * 入参：
 *   src - 待解码字符串片段。
 *   src_len - 待解码片段长度。
 *   out - 输出解码结果。
 *   out_len - 输出缓冲区长度。
 * 返回值：成功返回 0；参数非法、编码非法或空间不足返回负 errno。
 */
static int provisioning_url_decode_range(const char *src, int src_len,
                                         char *out, int out_len)
{
    int in_pos = 0;
    int out_pos = 0;

    if (!src || !out || out_len <= 0) {
        return -EINVAL;
    }

    while (in_pos < src_len) {
        char c = src[in_pos++];

        if (out_pos >= out_len - 1) {
            return -ENOSPC;
        }

        if (c == '+') {
            out[out_pos++] = ' ';
            continue;
        }

        if (c == '%' && in_pos + 1 < src_len) {
            int hi = provisioning_hex_value(src[in_pos]);
            int lo = provisioning_hex_value(src[in_pos + 1]);

            if (hi < 0 || lo < 0) {
                return -EINVAL;
            }

            out[out_pos++] = (char)((hi << 4) | lo);
            in_pos += 2;
            continue;
        }

        if ((unsigned char)c < 0x20 || c == 0x7f) {
            return -EINVAL;
        }

        out[out_pos++] = c;
    }

    out[out_pos] = '\0';
    return 0;
}

/*
 * 函数描述：从 GET query 或 POST body 形式的表单字符串中读取指定 key。
 * 入参：
 *   form - 表单字符串。
 *   key - 需要读取的字段名。
 *   out - 输出字段值。
 *   out_len - 输出缓冲区长度。
 * 返回值：成功返回 0；未找到返回 -ENOENT；解析失败返回负 errno。
 */
static int provisioning_form_get(const char *form, const char *key,
                                 char *out, int out_len)
{
    int key_len;
    const char *p;

    if (!form || !key || !out || out_len <= 0) {
        return -EINVAL;
    }

    out[0] = '\0';
    key_len = strlen(key);
    p = form;
    while (*p) {
        const char *eq;
        const char *end;

        while (*p == '&' || *p == '?' || *p == ' ' ||
               *p == '\r' || *p == '\n') {
            p++;
        }
        if (*p == '\0') {
            break;
        }

        eq = strchr(p, '=');
        end = strchr(p, '&');
        if (!eq) {
            break;
        }
        if (end && end < eq) {
            p = end + 1;
            continue;
        }

        if ((int)(eq - p) == key_len && strncmp(p, key, key_len) == 0) {
            const char *value = eq + 1;

            end = strchr(value, '&');
            if (!end) {
                end = value + strlen(value);
            }
            while (end > value &&
                   (end[-1] == '\r' || end[-1] == '\n' || end[-1] == ' ')) {
                end--;
            }

            return provisioning_url_decode_range(value, end - value,
                                                 out, out_len);
        }

        if (!end) {
            break;
        }
        p = end + 1;
    }

    return -ENOENT;
}

/*
 * 函数描述：从原始 TCP/HTTP 请求中定位表单 body 或 GET query。
 * 入参：
 *   request - APP 发来的原始请求文本。
 * 返回值：返回可继续按表单解析的字符串指针。
 */
static const char *provisioning_find_form_body(const char *request)
{
    const char *body;
    const char *query;
    const char *space;

    body = strstr(request, "\r\n\r\n");
    if (body) {
        return body + 4;
    }

    if (strncmp(request, "GET ", 4) == 0) {
        query = strchr(request, '?');
        space = strchr(request + 4, ' ');
        if (query && space && query < space) {
            return query + 1;
        }
    }

    return request;
}

/*
 * 函数描述：检查字符串中是否没有控制字符，避免把非法 SSID/密码写入 WiFi 驱动。
 * 入参：
 *   value - 待检查字符串。
 * 返回值：合法返回 1；为空或含控制字符返回 0。
 */
static int provisioning_has_no_control_chars(const char *value)
{
    int i;

    if (!value) {
        return 0;
    }

    for (i = 0; value[i] != '\0'; i++) {
        unsigned char c = (unsigned char)value[i];

        if (c < 0x20 || c == 0x7f) {
            return 0;
        }
    }

    return 1;
}

/*
 * 函数描述：校验 APP 下发的手机热点配置是否满足协议和 WiFi 基本限制。
 * 入参：
 *   config - APP 下发并解析后的热点配置。
 * 返回值：成功返回 0；nonce 不匹配、字段非法返回负 errno。
 */
static int provisioning_validate_phone_config(
    const rvt_provisioning_phone_config_t *config)
{
    int ssid_len;
    int password_len;

    if (!config) {
        return -EINVAL;
    }

    if (strncmp(config->nonce, g_session.nonce, sizeof(config->nonce)) != 0) {
        return -EACCES;
    }

    ssid_len = strnlen(config->ssid, RVT_PROV_SSID_MAX_LEN + 1);
    if (ssid_len <= 0 || ssid_len > RVT_PROV_SSID_MAX_LEN ||
        !provisioning_has_no_control_chars(config->ssid)) {
        return -EINVAL;
    }

    password_len = strnlen(config->password, sizeof(config->password));
    if (password_len < 8 || password_len > 63 ||
        !provisioning_has_no_control_chars(config->password)) {
        return -EINVAL;
    }

    if (config->phone_id[0] == '\0' ||
        config->phone_random[0] == '\0') {
        return -EINVAL;
    }

    return 0;
}

/*
 * 函数描述：解析 APP 通过 TCP/HTTP form 下发的 v=3 手机热点配置。
 * 入参：
 *   request - 原始请求文本。
 *   config - 输出解析后的手机热点配置。
 * 返回值：成功返回 0；版本不匹配或字段非法返回负 errno。
 */
static int provisioning_parse_config_request(
    const char *request, rvt_provisioning_phone_config_t *config)
{
    const char *form;
    char version[4];
    int ret;

    if (!request || !config) {
        return -EINVAL;
    }

    memset(config, 0, sizeof(*config));
    form = provisioning_find_form_body(request);

    ret = provisioning_form_get(form, "v", version, sizeof(version));
    if (ret < 0 || strcmp(version, "3") != 0) {
        return -EINVAL;
    }

    ret = provisioning_form_get(form, "n", config->nonce,
                               sizeof(config->nonce));
    if (ret < 0) {
        return ret;
    }

    ret = provisioning_form_get(form, "phone_id", config->phone_id,
                               sizeof(config->phone_id));
    if (ret < 0) {
        return ret;
    }

    ret = provisioning_form_get(form, "phone_random", config->phone_random,
                               sizeof(config->phone_random));
    if (ret < 0) {
        return ret;
    }

    ret = provisioning_form_get(form, "ssid", config->ssid,
                               sizeof(config->ssid));
    if (ret < 0) {
        return ret;
    }

    ret = provisioning_form_get(form, "psk", config->password,
                               sizeof(config->password));
    if (ret < 0) {
        return ret;
    }

    ret = provisioning_form_get(form, "auth_type", config->auth_type,
                               sizeof(config->auth_type));
    if (ret < 0) {
        snprintf(config->auth_type, sizeof(config->auth_type), "WPA2_PSK");
    }

    return provisioning_validate_phone_config(config);
}

/*
 * 函数描述：向 APP 返回配置接收结果。
 * 入参：
 *   client - 已 accept 的 TCP 客户端 socket。
 *   status - 状态字符串，例如 accepted 或 error。
 *   code - 结果码，0 表示成功，非 0 表示错误。
 * 返回值：无。
 */
static void provisioning_send_config_response(int client, const char *status,
                                              int code)
{
    char response[64];
    int len;

    len = snprintf(response, sizeof(response), "status=%s&code=%d\n",
                   status ? status : "error", code);
    if (len > 0) {
        send(client, response, len, 0);
    }
}

/*
 * 函数描述：从 APP 的 TCP 连接中读取一条热点配置请求并解析。
 * 入参：
 *   client - 已 accept 的 TCP 客户端 socket。
 *   config - 输出解析后的手机热点配置。
 * 返回值：成功返回 0；超时、无数据或解析失败返回负 errno。
 */
static int provisioning_receive_config(
    int client, rvt_provisioning_phone_config_t *config)
{
    char request[RVT_PROVISIONING_CONFIG_RX_MAX + 1];
    int total = 0;

    memset(request, 0, sizeof(request));
    while (total < RVT_PROVISIONING_CONFIG_RX_MAX) {
        fd_set readfds;
        struct timeval timeout;
        int ret;
        int len;

        FD_ZERO(&readfds);
        FD_SET(client, &readfds);
        timeout.tv_sec = 2;
        timeout.tv_usec = 0;

        ret = select(client + 1, &readfds, NULL, NULL, &timeout);
        if (ret <= 0) {
            return ret == 0 ? -ETIMEDOUT : -errno;
        }

        len = recv(client, request + total,
                   RVT_PROVISIONING_CONFIG_RX_MAX - total, 0);
        if (len <= 0) {
            break;
        }

        total += len;
        request[total] = '\0';
        if (strchr(request, '\n')) {
            break;
        }
    }

    if (total <= 0) {
        return -ENODATA;
    }

    return provisioning_parse_config_request(request, config);
}

/*
 * 函数描述：创建车机 SoftAP 阶段的 TCP 配置服务。
 * 入参：无。
 * 返回值：成功返回监听 socket；失败返回负 errno。
 */
static int provisioning_create_config_server(void)
{
    struct sockaddr_in addr;
    const char *bind_ip = "0.0.0.0";
    int server;
    int opt = 1;
    int ret;

    server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) {
        PROV_LOG("provisioning: config server socket failed errno=%d\n",
                 errno);
        return -errno;
    }

    ret = setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if (ret < 0) {
        PROV_LOG("provisioning: config server reuseaddr failed fd=%d errno=%d\n",
                 server, errno);
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(g_session.port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    ret = bind(server, (struct sockaddr *)&addr, sizeof(addr));
    if (ret < 0) {
        ret = -errno;
        PROV_LOG("provisioning: config server bind failed fd=%d ip=%s port=%d errno=%d\n",
                 server, bind_ip, g_session.port, errno);
        close(server);
        return ret;
    }

    ret = listen(server, 1);
    if (ret < 0) {
        ret = -errno;
        PROV_LOG("provisioning: config server listen failed fd=%d ip=%s port=%d errno=%d\n",
                 server, bind_ip, g_session.port, errno);
        close(server);
        return ret;
    }

    PROV_LOG("provisioning: config server bound ip=%s port=%d fd=%d\n",
             bind_ip, g_session.port, server);
    return server;
}

/*
 * 函数描述：格式化 APP TCP 对端地址，便于日志和状态展示。
 * 入参：
 *   addr - accept 得到的 IPv4 地址。
 *   out - 输出字符串缓冲区。
 *   out_len - 输出缓冲区长度。
 * 返回值：无。
 */
static void provisioning_format_peer_addr(const struct sockaddr_in *addr,
                                          char *out, size_t out_len)
{
    const char *ip;

    if (out == NULL || out_len == 0) {
        return;
    }

    if (addr == NULL) {
        snprintf(out, out_len, "-");
        return;
    }

    ip = inet_ntoa(addr->sin_addr);
    snprintf(out, out_len, "%s:%u", ip ? ip : "-",
             (unsigned int)ntohs(addr->sin_port));
}

/*
 * 函数描述：等待 APP 连接车机 SoftAP 并下发手机热点配置。
 * 入参：
 *   session_id - 启动本 worker 时的会话 ID，用于识别过期任务。
 *   timeout_ms - 等待超时时间，单位毫秒。
 *   phone_config - 输出 APP 下发的手机热点配置。
 * 返回值：成功返回 0；超时、取消或解析失败返回负 errno。
 */
static int provisioning_wait_for_phone_config(
    unsigned int session_id, int timeout_ms,
    rvt_provisioning_phone_config_t *phone_config)
{
    int server;
    int elapsed_ms = 0;
    int ret = -ETIMEDOUT;

    if (!phone_config) {
        return -EINVAL;
    }

    server = provisioning_create_config_server();
    if (server < 0) {
        PROV_LOG("provisioning: config server start failed, ret=%d\n",
                 server);
        return server;
    }

    PROV_LOG("provisioning: config server listening port=%d\n",
             g_session.port);
    while (session_id == g_session_id && !g_stop_requested &&
           elapsed_ms < timeout_ms) {
        fd_set readfds;
        struct timeval timeout;
        int client;
        struct sockaddr_in peer_addr;
        socklen_t peer_len = sizeof(peer_addr);
        char peer_text[sizeof(g_session.peer_addr)];

        FD_ZERO(&readfds);
        FD_SET(server, &readfds);
        timeout.tv_sec = 0;
        timeout.tv_usec = RVT_PROVISIONING_WAIT_STEP_MS * 1000;

        ret = select(server + 1, &readfds, NULL, NULL, &timeout);
        if (ret < 0) {
            ret = -errno;
            break;
        }
        if (ret == 0) {
            elapsed_ms += RVT_PROVISIONING_WAIT_STEP_MS;
            continue;
        }

        memset(&peer_addr, 0, sizeof(peer_addr));
        client = accept(server, (struct sockaddr *)&peer_addr, &peer_len);
        if (client < 0) {
            ret = -errno;
            continue;
        }
        provisioning_format_peer_addr(&peer_addr, peer_text,
                                      sizeof(peer_text));

        {
            rvt_provisioning_phone_config_t received_config;

            ret = provisioning_receive_config(client, &received_config);
            if (ret == 0) {
                char masked_phone_id[24];

                *phone_config = received_config;
                snprintf(g_session.peer_phone_id,
                         sizeof(g_session.peer_phone_id), "%s",
                         received_config.phone_id);
                snprintf(g_session.peer_addr, sizeof(g_session.peer_addr),
                         "%s", peer_text);
                provisioning_send_config_response(client, "accepted", 0);
                close(client);
                provisioning_mask_id(phone_config->phone_id,
                                     masked_phone_id,
                                     sizeof(masked_phone_id));
                PROV_LOG("provisioning: accepted phone config peer=%s phone_id=%s ssid=%s\n",
                         g_session.peer_addr,
                         masked_phone_id,
                         phone_config->ssid);
                close(server);
                return 0;
            }

            provisioning_send_config_response(client, "error", -ret);
            close(client);
            PROV_LOG("provisioning: reject config request peer=%s ret=%d\n",
                     peer_text, ret);
        }
    }

    close(server);
    if (session_id != g_session_id || g_stop_requested) {
        return -ECANCELED;
    }

    return ret < 0 ? ret : -ETIMEDOUT;
}

/*
 * 函数描述：停止车机临时 SoftAP，并清理本服务记录的 SoftAP 状态。
 * 入参：无。
 * 返回值：成功返回 0；停止 SoftAP 或恢复 STA 失败返回负 errno。
 */
static int rvt_provisioning_stop_softap_direct(void)
{
    int ret = 0;

    if (g_softap_running) {
        PROV_LOG("provisioning: stop softap enter\n");
        ret = rvt_prov_wifi_stop_softap();
        PROV_LOG("provisioning: stop softap leave ret=%d\n", ret);
    }

    g_softap_running = 0;
    return ret;
}

/*
 * 函数描述：保存 APP 下发的 WiFi 配置。
 * 入参：
 *   session_id - 启动本 worker 时的会话 ID，用于识别过期任务。
 *   phone_config - APP 下发的手机热点配置。
 * 返回值：成功返回 0；取消或保存配置失败返回负 errno。
 */
static int provisioning_save_phone_hotspot(unsigned int session_id,
    const rvt_provisioning_phone_config_t *phone_config)
{
    int ret;

    if (!phone_config) {
        return -EINVAL;
    }

    if (g_stop_requested || session_id != g_session_id) {
        return -ECANCELED;
    }

    ret = provisioning_save_wapi_config(phone_config);
    if (ret < 0) {
        PROV_LOG("provisioning: save wapi config failed, ret=%d ssid=%s\n",
                 ret,
                 phone_config->ssid);
        return ret;
    }

    PROV_LOG("provisioning: saved wapi config path=%s ssid=%s\n",
             RVT_PROVISIONING_WAPI_CONFIG_PATH,
             phone_config->ssid);
    return 0;
}

/*
 * 函数描述：触发平台 WiFi 回连流程。
 * 入参：
 *   session_id - 启动本 worker 时的会话 ID，用于识别过期任务。
 *   phone_config - APP 下发的手机热点配置。
 * 返回值：成功返回 0；取消或触发连接失败返回负 errno。
 */
static int provisioning_request_phone_hotspot_connect(unsigned int session_id,
    const rvt_provisioning_phone_config_t *phone_config)
{
    if (!phone_config) {
        return -EINVAL;
    }

    if (g_stop_requested || session_id != g_session_id) {
        return -ECANCELED;
    }

    return rvt_prov_wifi_submit_config(phone_config->ssid,
                                       phone_config->password);
}

/*
 * 函数描述：确认平台 WiFi 回连结果。连接动作已经由 submit_config 同步完成。
 * 入参：
 *   session_id - 启动本 worker 时的会话 ID，用于识别过期任务。
 * 返回值：连接就绪返回 0；取消返回 -ECANCELED；未就绪返回 -ENETDOWN。
 */
static int provisioning_confirm_wifi_ready(unsigned int session_id)
{
    if (g_stop_requested || session_id != g_session_id) {
        return -ECANCELED;
    }

    return rvt_prov_wifi_is_ready() ? 0 : -ENETDOWN;
}

/*
 * 函数描述：当前 WiFi 主流程异步任务入口，等待 APP 配置并回连手机热点。
 * 入参：
 *   argc - task_create 传入的参数个数，当前未使用。
 *   argv - task_create 传入的参数列表，当前未使用。
 * 返回值：成功返回 0；失败返回负 errno。任务退出时会调用 _exit。
 */
static int rvt_provisioning_softap_task_main(int argc, char *argv[])
{
    unsigned int session_id = g_session_id;
    int timeout_ms;
    rvt_provisioning_phone_config_t phone_config;
    int ret = -ETIMEDOUT;

    (void)argc;
    (void)argv;

    timeout_ms = g_session.ttl_seconds * 1000;
    memset(&phone_config, 0, sizeof(phone_config));

    ret = provisioning_wait_for_phone_config(session_id, timeout_ms,
                                             &phone_config);
    if (ret == -ECANCELED || session_id != g_session_id || g_stop_requested) {
        _exit(0);
        return 0;
    }

    if (ret == 0) {
        g_session.state = RVT_PROVISIONING_STATE_CONNECTING;
        g_session.result = 0;
        g_session.ttl_seconds = RVT_PROVISIONING_CONNECTING_UI_TTL_SECONDS;
        provisioning_notify(RVT_PROVISIONING_EVENT_CONNECTING);

        ret = provisioning_save_phone_hotspot(session_id, &phone_config);
        if (ret == -ECANCELED || session_id != g_session_id ||
            g_stop_requested) {
            _exit(0);
            return 0;
        }
        if (ret == 0) {
            ret = rvt_provisioning_stop_softap_direct();
        } else {
            PROV_LOG("provisioning: save wifi config failed, ret=%d ssid=%s\n",
                     ret, phone_config.ssid);
            rvt_provisioning_stop_softap_direct();
        }
        if (ret == 0) {
            ret = provisioning_request_phone_hotspot_connect(session_id,
                                                            &phone_config);
        }
        if (ret == 0) {
            PROV_LOG("provisioning: submitted wifi config ssid=%s peer=%s\n",
                     phone_config.ssid,
                     g_session.peer_addr[0] ? g_session.peer_addr : "-");

            ret = provisioning_confirm_wifi_ready(session_id);
            if (ret == -ECANCELED || session_id != g_session_id ||
                g_stop_requested) {
                _exit(0);
                return 0;
            }
            if (ret == 0) {
                g_session.state = RVT_PROVISIONING_STATE_CONNECTED;
                g_session.result = 0;
                g_session.running = 0;
                g_session.ttl_seconds = RVT_PROVISIONING_SUCCESS_UI_TTL_SECONDS;
                provisioning_mark_success();
                provisioning_notify(RVT_PROVISIONING_EVENT_CONNECTED);
                PROV_LOG("provisioning: wifi connected ssid=%s peer=%s\n",
                         phone_config.ssid,
                         g_session.peer_addr[0] ? g_session.peer_addr : "-");
                _exit(0);
                return 0;
            }

            PROV_LOG("provisioning: wifi connect failed ssid=%s ret=%d\n",
                     phone_config.ssid,
                     ret);
        }

        PROV_LOG("provisioning: phone hotspot connect failed, ret=%d ssid=%s\n",
                 ret, phone_config.ssid);
    } else {
        int stop_ret = rvt_provisioning_stop_softap_direct();

        if (ret == 0) {
            ret = stop_ret;
        }
        if (ret == 0) {
            ret = -ETIMEDOUT;
        }
    }

    g_session.result = ret;
    g_session.running = 0;
    g_session.state = RVT_PROVISIONING_STATE_FAILED;
    g_session.ttl_seconds = RVT_PROVISIONING_FAILURE_UI_TTL_SECONDS;
    provisioning_notify(RVT_PROVISIONING_EVENT_FAILED);
    PROV_LOG("provisioning: softap session failed, ret=%d\n", ret);

    _exit(1);
    return ret;
}

/*
 * 函数描述：启动当前 WiFi 主流程，车机生成临时 SoftAP 并展示 v=3 二维码。
 * 入参：无。
 * 返回值：成功返回 0；生成配置、启动 SoftAP 或创建任务失败返回负 errno。
 */
static int rvt_provisioning_start_softap(void)
{
    rvt_prov_softap_config_t config;
    char softap_ip[sizeof(config.ip)];
    int pid;
    int ret;

    ret = rvt_prov_generate_softap_config(&config);
    if (ret < 0) {
        PROV_LOG("provisioning: generate softap config failed, ret=%d\n",
                 ret);
        return ret;
    }

    memset(&g_session, 0, sizeof(g_session));
    g_session.state = RVT_PROVISIONING_STATE_STARTING;
    g_session.ttl_seconds = RVT_PROVISIONING_QR_TTL_SECONDS;
    g_session.running = 1;
    snprintf(g_session.ap_ssid, sizeof(g_session.ap_ssid), "%s",
             config.ssid);
    snprintf(g_session.ap_password, sizeof(g_session.ap_password), "%s",
             config.password);
    snprintf(g_session.nonce, sizeof(g_session.nonce), "%s",
             config.nonce);
    g_session.port = config.port;

    g_stop_requested = 0;
    g_session_id++;

    ret = rvt_prov_wifi_start_softap(&config);
    if (ret < 0) {
        g_session.state = RVT_PROVISIONING_STATE_FAILED;
        g_session.result = ret;
        g_session.running = 0;
        g_session.ttl_seconds = RVT_PROVISIONING_FAILURE_UI_TTL_SECONDS;
        provisioning_notify(RVT_PROVISIONING_EVENT_FAILED);
        PROV_LOG("provisioning: start softap failed, ret=%d ssid=%s\n",
                 ret, g_session.ap_ssid);
        return ret;
    }

    g_softap_running = 1;
    ret = rvt_prov_wifi_get_softap_ip(softap_ip, sizeof(softap_ip));
    if (ret < 0) {
        PROV_LOG("provisioning: get softap ip failed, ret=%d fallback=%s\n",
                 ret, config.ip);
    } else {
        snprintf(config.ip, sizeof(config.ip), "%s", softap_ip);
    }
    snprintf(g_session.ip, sizeof(g_session.ip), "%s", config.ip);
    provisioning_build_softap_qr_payload(&g_session);
    g_session.state = RVT_PROVISIONING_STATE_WAITING_APP;
    PROV_LOG("provisioning: show softap QR ssid=%s ip=%s port=%d\n",
             g_session.ap_ssid,
             g_session.ip,
             g_session.port);
    provisioning_notify(RVT_PROVISIONING_EVENT_QR_SHOW);

    pid = task_create("rvtprov_ap",
                      CONFIG_RIVOTEK_PROVISIONING_SERVICE_PRIORITY,
                      CONFIG_RIVOTEK_PROVISIONING_SERVICE_STACKSIZE,
                      rvt_provisioning_softap_task_main,
                      NULL);
    if (pid < 0) {
        ret = errno ? -errno : -ECHILD;
        rvt_provisioning_stop_softap_direct();
        g_session.state = RVT_PROVISIONING_STATE_FAILED;
        g_session.result = ret;
        g_session.running = 0;
        g_session.ttl_seconds = RVT_PROVISIONING_FAILURE_UI_TTL_SECONDS;
        provisioning_notify(RVT_PROVISIONING_EVENT_FAILED);
        PROV_LOG("provisioning: task_create failed, ret=%d\n", ret);
        return ret;
    }

    return 0;
}

/*
 * 函数描述：配网高层启动接口，根据 type 选择 WiFi 或后续蓝牙配网方式。
 * 入参：
 *   type - 配网方式，当前支持 RVT_PROVISIONING_TYPE_WIFI。
 * 返回值：成功返回 0；已在运行、类型不支持或启动失败返回负 errno。
 */
int rvt_provisioning_start(rvt_provisioning_type_t type)
{
    if (g_session.running || g_softap_running) {
        PROV_LOG("provisioning: already starting or running\n");
        return -EALREADY;
    }

    switch (type) {
    case RVT_PROVISIONING_TYPE_WIFI:
        return rvt_provisioning_start_softap();
    case RVT_PROVISIONING_TYPE_BLUETOOTH:
        PROV_LOG("provisioning: bluetooth provisioning not implemented\n");
        return -ENOSYS;
    default:
        PROV_LOG("provisioning: unsupported provisioning type=%d\n",
                 (int)type);
        return -EINVAL;
    }
}

/*
 * 函数描述：停止当前配网流程，终止 STA 连接或车机 SoftAP，并通知 UI 隐藏二维码。
 * 入参：无。
 * 返回值：成功返回 0；底层停止失败返回负 errno。
 */
int rvt_provisioning_stop(void)
{
    int ret;

    g_stop_requested = 1;
    g_session_id++;
    ret = rvt_provisioning_stop_softap_direct();

    g_session.state = RVT_PROVISIONING_STATE_STOPPED;
    g_session.result = ret;
    g_session.running = 0;
    provisioning_notify(RVT_PROVISIONING_EVENT_STOPPED);
    provisioning_notify(RVT_PROVISIONING_EVENT_QR_HIDE);
    return ret;
}

/*
 * 函数描述：查询配网服务是否处于启动、等待、已连接或 SoftAP 工作状态。
 * 入参：无。
 * 返回值：正在运行返回非 0；空闲返回 0。
 */
int rvt_provisioning_is_running(void)
{
    return g_session.running || g_softap_running;
}

/*
 * 函数描述：获取当前配网会话快照。
 * 入参：
 *   session - 输出会话快照。
 * 返回值：成功返回 0；参数非法返回负 errno。
 */
int rvt_provisioning_get_session(rvt_provisioning_session_t *session)
{
    if (!session) {
        return -EINVAL;
    }

    *session = g_session;
    return 0;
}

/*
 * 函数描述：注册配网事件回调，供 QR service 或其他业务模块接收状态变化。
 * 入参：
 *   cb - 事件回调函数；NULL 表示清除回调。
 *   user_data - 回调透传上下文。
 * 返回值：固定返回 0。
 */
int rvt_provisioning_set_event_callback(rvt_provisioning_event_cb_t cb,
                                        void *user_data)
{
    g_event_cb = cb;
    g_event_user_data = user_data;
    return 0;
}

/*
 * 函数描述：查询当前设备是否曾经成功完成配网。
 * 入参：无。
 * 返回值：曾经成功配网返回 1；未配过网或读取失败返回 0。
 */
int rvt_provisioning_has_provisioned(void)
{
    return provisioning_read_provisioned_flag();
}

/*
 * 函数描述：设置配网完成标志，供恢复出厂、解绑或调试命令重置状态。
 * 入参：
 *   provisioned - 非 0 表示已配网；0 表示未配网。
 * 返回值：成功返回 0；持久化失败返回负 errno。
 */
int rvt_provisioning_set_provisioned(int provisioned)
{
    return provisioning_write_provisioned_flag(provisioned ? 1 : 0);
}

/*
 * 函数描述：根据配网完成标志生成网络状态信息。
 * 入参：
 *   info - 输出网络状态信息。
 * 返回值：成功返回 0；参数非法返回负 errno。
 */
int rvt_provisioning_get_network_status(
    rvt_provisioning_network_status_info_t *info)
{
    int provisioned;

    if (!info) {
        return -EINVAL;
    }

    provisioned = provisioning_read_provisioned_flag();
    if (provisioned) {
        info->status = RVT_PROVISIONING_NETWORK_STATUS_PROVISIONED_UNAVAILABLE;
    } else {
        info->status = RVT_PROVISIONING_NETWORK_STATUS_UNPROVISIONED;
    }

    return 0;
}

/*
 * 函数描述：注册网络状态回调，供 AI 或应用层按需接收。
 * 入参：
 *   cb - 网络状态回调；NULL 表示清除回调。
 *   user_data - 回调透传上下文。
 * 返回值：固定返回 0。
 */
int rvt_provisioning_set_network_status_callback(
    rvt_provisioning_network_status_cb_t cb,
    void *user_data)
{
    g_network_status_cb = cb;
    g_network_status_user_data = user_data;
    return 0;
}

/*
 * 函数描述：通知上层当前网络不可用，并上报未配网或已配网但不可用状态。
 * 入参：无。
 * 返回值：成功返回 0；内部生成状态失败返回负 errno。
 */
int rvt_provisioning_notify_network_unavailable(void)
{
    rvt_provisioning_network_status_cb_t cb = g_network_status_cb;
    rvt_provisioning_network_status_info_t info;
    int ret;

    ret = rvt_provisioning_get_network_status(&info);
    if (ret < 0) {
        return ret;
    }

    if (cb) {
        cb(&info, g_network_status_user_data);
    }
    rvt_provisioning_network_status_notify(&info);

    PROV_LOG("provisioning: network unavailable status=%d\n",
             (int)info.status);
    return 0;
}
