#include <nuttx/config.h>
#include <termios.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <net/if.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <netutils/cJSON.h>
#include <netutils/netlib.h>

#include "eca_client_command_dispatcher.h"
#include "eca_client_device_state.h"
#include "eca_client_log.h"
#include "eca_client_network.h"

#define ECA_CLIENT_CONFIG_PATH_1 "/data/eca_client_config.json"
#define ECA_CLIENT_CONFIG_PATH_2 "/etc/eca_client_config.json"
#define ECA_CLIENT_CONFIG_PATH_3 "/resource/eca/eca_client_config.json"
#define ECA_CLIENT_CONFIG_PATH_4 "/res/eca/eca_client_config.json"

#define ECA_CLIENT_TOPIC_MAX      128
#define ECA_CLIENT_PACKET_MAX     1024
#define ECA_CLIENT_HTTP_BODY_MAX  1024
#define ECA_CLIENT_WIFI_IFNAME    "wlan0"
#define ECA_CLIENT_SOFTAP_PREFIX  "192.168.49."
#define ECA_CLIENT_IO_TIMEOUT_SECONDS 5
#define ECA_CLIENT_NETWORK_THREAD_STACKSIZE 8192

#define ECA_CLIENT_WAPI_CONF_PATH   "/data/etc/wifi/wapi.conf"
#define ECA_CLIENT_CAMERA_UART_DEV  "/dev/uart1"
#define ECA_CLIENT_CAMERA_UART_BAUD 115200
#define ECA_CLIENT_CAMERA_JSON_MAX  256

typedef enum eca_client_mqtt_defer_reason_e {
    ECA_CLIENT_MQTT_DEFER_NONE = 0,
    ECA_CLIENT_MQTT_DEFER_NO_IPV4,
    ECA_CLIENT_MQTT_DEFER_NO_GATEWAY,
} eca_client_mqtt_defer_reason_t;

typedef struct eca_client_net_config_s {
    char device_id[32];
    char device_name[64];
    char claim_token[64];

    bool mqtt_enabled;
    char mqtt_host[64];
    int mqtt_port;
    char mqtt_client_id[64];
    bool mqtt_client_id_auto;
    char mqtt_username[64];
    char mqtt_password[64];
    char mqtt_command_topic[128];
    char mqtt_response_topic[128];
    char mqtt_response_topic_template[128];
    char mqtt_command_topic_template[128];
    int mqtt_keepalive_seconds;

    bool http_enabled;
    char http_base_url[128];
    char http_token[192];

    int heartbeat_interval_seconds;
    int telemetry_interval_seconds;
} eca_client_net_config_t;

typedef struct eca_client_net_state_s {
    eca_client_net_config_t config;
    pthread_t thread;
    pthread_mutex_t publish_lock;
    bool running;
    bool thread_started;
    bool lock_initialized;
    int sockfd;
    eca_client_mqtt_defer_reason_t mqtt_defer_reason;
    bool identity_refresh_requested;
} eca_client_net_state_t;

static eca_client_net_state_t g_net;

/* ── MQTT request-response pending table ──────────────────────── */

typedef struct eca_client_pending_request_s {
    char request_id[ECA_CLIENT_REQUEST_ID_MAX];
    bool active;
    bool fulfilled;
    eca_client_mqtt_response_t response;
} eca_client_pending_request_t;

static pthread_mutex_t g_request_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_request_cond  = PTHREAD_COND_INITIALIZER;
static eca_client_pending_request_t g_pending[ECA_CLIENT_PENDING_REQUESTS];
static unsigned int g_request_counter = 0;

static long long eca_client_now_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
        return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    }

    return (long long)time(NULL) * 1000;
}

static bool eca_client_wifi_ipv4_ready(char *ip, size_t ip_size)
{
    struct ifreq req;
    struct sockaddr_in *addr;
    int fd;
    int ret;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return false;
    }

    memset(&req, 0, sizeof(req));
    snprintf(req.ifr_name, sizeof(req.ifr_name), "%s", ECA_CLIENT_WIFI_IFNAME);
    ret = ioctl(fd, SIOCGIFFLAGS, (unsigned long)&req);
    if (ret < 0 || (req.ifr_flags & IFF_UP) == 0) {
        close(fd);
        return false;
    }

    memset(&req, 0, sizeof(req));
    snprintf(req.ifr_name, sizeof(req.ifr_name), "%s", ECA_CLIENT_WIFI_IFNAME);
    ret = ioctl(fd, SIOCGIFADDR, (unsigned long)&req);
    close(fd);
    if (ret < 0) {
        return false;
    }

    addr = (struct sockaddr_in *)&req.ifr_addr;
    if (addr->sin_addr.s_addr == 0) {
        return false;
    }

    if (ip != NULL && ip_size > 0) {
        inet_ntop(AF_INET, &addr->sin_addr, ip, ip_size);
        if (strncmp(ip, ECA_CLIENT_SOFTAP_PREFIX,
                    strlen(ECA_CLIENT_SOFTAP_PREFIX)) == 0) {
            return false;
        }
    }

    return true;
}

static bool eca_client_wifi_route_ready(char *gateway, size_t gateway_size)
{
    struct in_addr gateway_addr;

    if (netlib_get_dripv4addr(ECA_CLIENT_WIFI_IFNAME, &gateway_addr) < 0 ||
        gateway_addr.s_addr == 0) {
        return false;
    }

    if (gateway != NULL && gateway_size > 0) {
        inet_ntop(AF_INET, &gateway_addr, gateway, gateway_size);
    }

    return true;
}

static bool eca_client_mac_is_valid(const uint8_t *mac, size_t mac_size)
{
    bool all_zero = true;
    bool all_ff = true;
    size_t i;

    if (mac == NULL || mac_size < IFHWADDRLEN) {
        return false;
    }

    for (i = 0; i < IFHWADDRLEN; i++) {
        if (mac[i] != 0x00) {
            all_zero = false;
        }
        if (mac[i] != 0xff) {
            all_ff = false;
        }
    }

    return !all_zero && !all_ff;
}

static int eca_client_load_wifi_mac(uint8_t *mac, size_t mac_size)
{
    struct ifreq req;
    int fd;
    int ret = 0;

    if (mac == NULL || mac_size < IFHWADDRLEN) {
        return -EINVAL;
    }

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return -errno;
    }

    memset(&req, 0, sizeof(req));
    snprintf(req.ifr_name, sizeof(req.ifr_name), "%s", ECA_CLIENT_WIFI_IFNAME);
    if (ioctl(fd, SIOCGIFHWADDR, (unsigned long)&req) < 0) {
        ret = -errno;
    } else {
        memcpy(mac, req.ifr_hwaddr.sa_data, IFHWADDRLEN);
        if (!eca_client_mac_is_valid(mac, mac_size)) {
            ret = -ENODEV;
        }
    }

    close(fd);
    return ret;
}

static int eca_client_format_wifi_mac(char *device_id, size_t device_id_size)
{
    uint8_t mac[IFHWADDRLEN];
    int ret;

    ret = eca_client_load_wifi_mac(mac, sizeof(mac));
    if (ret < 0) {
        return ret;
    }

    snprintf(device_id, device_id_size, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return 0;
}

static void eca_client_config_defaults(eca_client_net_config_t *config)
{
    memset(config, 0, sizeof(*config));
    snprintf(config->device_id, sizeof(config->device_id), "dev_001");
    snprintf(config->device_name, sizeof(config->device_name), "EcaClient");
    snprintf(config->claim_token, sizeof(config->claim_token), "eca-dev-001-claim");

    config->mqtt_enabled = true;
    snprintf(config->mqtt_command_topic_template,
             sizeof(config->mqtt_command_topic_template),
             "eca/devices/{device_id}/commands");
    snprintf(config->mqtt_response_topic_template,
             sizeof(config->mqtt_response_topic_template),
             "eca/devices/{device_id}/responses");
    config->mqtt_keepalive_seconds = 60;

    config->http_enabled = true;

    config->heartbeat_interval_seconds = 30;
    config->telemetry_interval_seconds = 10;
}

static void eca_client_copy_json_string(cJSON *root, const char *key,
                                        char *dst, size_t dst_size)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);

    if (cJSON_IsString(item) && item->valuestring != NULL) {
        snprintf(dst, dst_size, "%s", item->valuestring);
    }
}

static void eca_client_copy_json_bool(cJSON *root, const char *key, bool *dst)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);

    if (cJSON_IsBool(item)) {
        *dst = cJSON_IsTrue(item);
    }
}

static void eca_client_copy_json_int(cJSON *root, const char *key, int *dst)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);

    if (cJSON_IsNumber(item)) {
        *dst = item->valueint;
    }
}

static int eca_client_config_clamp_int(int value, int min_value, int max_value)
{
    if (value < min_value) {
        return min_value;
    }

    if (value > max_value) {
        return max_value;
    }

    return value;
}

static void eca_client_config_normalize(eca_client_net_config_t *config)
{
    int ret;

    ret = eca_client_format_wifi_mac(config->device_id,
                                     sizeof(config->device_id));
    if (ret < 0) {
        if (config->device_id[0] == '\0') {
            snprintf(config->device_id, sizeof(config->device_id), "dev_001");
        }
        ECA_LOGW("wifi mac unavailable for %s, use device_id %s",
                 ECA_CLIENT_WIFI_IFNAME, config->device_id);
    }

    if (config->mqtt_command_topic_template[0] == '\0') {
        snprintf(config->mqtt_command_topic_template,
                 sizeof(config->mqtt_command_topic_template),
                 "eca/devices/{device_id}/commands");
    }
    if (config->mqtt_response_topic_template[0] == '\0') {
        snprintf(config->mqtt_response_topic_template,
                 sizeof(config->mqtt_response_topic_template),
                 "eca/devices/{device_id}/responses");
    }

    config->mqtt_port = eca_client_config_clamp_int(config->mqtt_port, 1, 65535);
    config->mqtt_keepalive_seconds =
        eca_client_config_clamp_int(config->mqtt_keepalive_seconds, 10, 600);
    config->heartbeat_interval_seconds =
        eca_client_config_clamp_int(config->heartbeat_interval_seconds, 5, 3600);
    config->telemetry_interval_seconds =
        eca_client_config_clamp_int(config->telemetry_interval_seconds, 5, 3600);

    if (config->mqtt_client_id[0] == '\0') {
        config->mqtt_client_id_auto = true;
        snprintf(config->mqtt_client_id, sizeof(config->mqtt_client_id),
                 "EcaClient-%s", config->device_id);
    } else {
        config->mqtt_client_id_auto = false;
    }
}

static int eca_client_config_validate(const eca_client_net_config_t *config)
{
    if (config->mqtt_enabled) {
        if (config->mqtt_host[0] == '\0') {
            ECA_LOGE("mqtt.host missing in config");
            return -EINVAL;
        }

        if (config->mqtt_port < 1 || config->mqtt_port > 65535) {
            ECA_LOGE("mqtt.port invalid in config: %d", config->mqtt_port);
            return -EINVAL;
        }
    }

    if (config->http_enabled && config->http_base_url[0] == '\0') {
        ECA_LOGE("http.base_url missing in config");
        return -EINVAL;
    }

    return 0;
}

static char *eca_client_read_file(const char *path)
{
    FILE *fp;
    long size;
    char *data;

    fp = fopen(path, "rb");
    if (fp == NULL) {
        return NULL;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }

    size = ftell(fp);
    if (size <= 0 || size > 16 * 1024) {
        fclose(fp);
        return NULL;
    }

    rewind(fp);
    data = (char *)malloc((size_t)size + 1);
    if (data == NULL) {
        fclose(fp);
        return NULL;
    }

    if (fread(data, 1, (size_t)size, fp) != (size_t)size) {
        free(data);
        fclose(fp);
        return NULL;
    }

    data[size] = '\0';
    fclose(fp);
    return data;
}

static int eca_client_read_wapi_conf(char *ssid, size_t ssid_size,
                                     char *password, size_t password_size)
{
    char *text = NULL;
    cJSON *root = NULL;
    cJSON *wlan0 = NULL;
    cJSON *item = NULL;
    int ret = -1;

    if (ssid == NULL || password == NULL) {
        return -EINVAL;
    }

    ssid[0] = '\0';
    password[0] = '\0';

    text = eca_client_read_file(ECA_CLIENT_WAPI_CONF_PATH);
    if (text == NULL) {
        ECA_LOGW("read wapi.conf failed: %s", ECA_CLIENT_WAPI_CONF_PATH);
        return -ENOENT;
    }

    root = cJSON_Parse(text);
    if (root == NULL) {
        ECA_LOGW("parse wapi.conf failed");
        goto out;
    }

    wlan0 = cJSON_GetObjectItemCaseSensitive(root, "wlan0");
    if (!cJSON_IsObject(wlan0)) {
        ECA_LOGW("wapi.conf missing wlan0 object");
        goto out;
    }

    item = cJSON_GetObjectItemCaseSensitive(wlan0, "ssid");
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        snprintf(ssid, ssid_size, "%s", item->valuestring);
    }

    item = cJSON_GetObjectItemCaseSensitive(wlan0, "psk");
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        snprintf(password, password_size, "%s", item->valuestring);
    }

    if (ssid[0] != '\0') {
        ret = 0;
    } else {
        ECA_LOGW("wapi.conf ssid is empty");
        ret = -EINVAL;
    }

out:
    if (root != NULL) {
        cJSON_Delete(root);
    }
    free(text);
    return ret;
}

static int g_camera_uart_fd = -1;

static int eca_client_uart1_open(speed_t baud)
{
    struct termios tty;
    int fd;

    if (g_camera_uart_fd >= 0) {
        return g_camera_uart_fd;
    }

    fd = open(ECA_CLIENT_CAMERA_UART_DEV, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        ECA_LOGW("open uart %s failed: %d", ECA_CLIENT_CAMERA_UART_DEV, errno);
        return -1;
    }

    if (tcgetattr(fd, &tty) != 0) {
        ECA_LOGW("tcgetattr uart %s failed: %d",
                 ECA_CLIENT_CAMERA_UART_DEV, errno);
        close(fd);
        return -1;
    }

    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag |= CREAD | CLOCAL;
    tty.c_cflag &= ~CRTSCTS;
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_oflag &= ~OPOST;

    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 10;

    cfsetospeed(&tty, baud);
    cfsetispeed(&tty, baud);

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        ECA_LOGW("tcsetattr uart %s failed: %d",
                 ECA_CLIENT_CAMERA_UART_DEV, errno);
        close(fd);
        return -1;
    }

    tcflush(fd, TCIOFLUSH);
    usleep(20 * 1000);
    tcflush(fd, TCIOFLUSH);
    ECA_LOGI("uart %s opened at %d baud 8N1",
             ECA_CLIENT_CAMERA_UART_DEV, (int)baud);
    g_camera_uart_fd = fd;
    return fd;
}

static int eca_client_send_wifi_config_uart(void)
{
    char ssid[64];
    char password[128];
    char mac_str[32];
    char json_buf[ECA_CLIENT_CAMERA_JSON_MAX];
    uint8_t mac[IFHWADDRLEN];
    int fd = -1;
    int ret;
    int json_len;
    size_t total_written = 0;

    ret = eca_client_read_wapi_conf(ssid, sizeof(ssid),
                                    password, sizeof(password));
    if (ret < 0) {
        ECA_LOGW("read wapi conf failed ret=%d", ret);
        return ret;
    }

    ret = eca_client_load_wifi_mac(mac, sizeof(mac));
    if (ret < 0) {
        ECA_LOGW("load wifi mac failed ret=%d", ret);
        snprintf(mac_str, sizeof(mac_str), "00:00:00:00:00:00");
    } else {
        snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }

    json_len = snprintf(json_buf, sizeof(json_buf),
                        "{\"ssid\":\"%s\",\"password\":\"%s\",\"mac\":\"%s\"}\r\n",
                        ssid, password, mac_str);
    if (json_len <= 0 || (size_t)json_len >= sizeof(json_buf)) {
        ECA_LOGW("json buffer too small or format failed");
        return -ENOBUFS;
    }

    fd = eca_client_uart1_open(ECA_CLIENT_CAMERA_UART_BAUD);
    if (fd < 0) {
        return -ENODEV;
    }

    {
        const char preamble[] = "\r\n";
        ssize_t n = write(fd, preamble, sizeof(preamble) - 1);
        if (n > 0) {
            tcdrain(fd);
        }
    }

    while (total_written < (size_t)json_len) {
        ssize_t n = write(fd, json_buf + total_written,
                          (size_t)json_len - total_written);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            ECA_LOGW("uart write failed errno=%d", errno);
            return -errno;
        }
        total_written += (size_t)n;
    }

    tcdrain(fd);

    ECA_LOGI("sent wifi config to camera via uart: %s (%d bytes)",
             json_buf, json_len);
    return 0;
}

static int eca_client_config_load(eca_client_net_config_t *config)
{
    const char *paths[] = {
        ECA_CLIENT_CONFIG_PATH_1,
        ECA_CLIENT_CONFIG_PATH_2,
        ECA_CLIENT_CONFIG_PATH_3,
        ECA_CLIENT_CONFIG_PATH_4,
    };
    char *text = NULL;
    cJSON *root;
    cJSON *device;
    cJSON *mqtt;
    cJSON *http;
    cJSON *intervals;
    size_t i;
    int ret;

    eca_client_config_defaults(config);

    for (i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        text = eca_client_read_file(paths[i]);
        if (text != NULL) {
            ECA_LOGI("loaded config %s", paths[i]);
            break;
        }
    }

    if (text == NULL) {
        ECA_LOGE("network config file not found");
        return -ENOENT;
    }

    root = cJSON_Parse(text);
    free(text);
    if (root == NULL) {
        ECA_LOGE("config json parse failed");
        return -EINVAL;
    }

    device = cJSON_GetObjectItemCaseSensitive(root, "device");
    if (cJSON_IsObject(device)) {
        eca_client_copy_json_string(device, "device_id",
                                    config->device_id, sizeof(config->device_id));
        eca_client_copy_json_string(device, "name",
                                    config->device_name, sizeof(config->device_name));
        eca_client_copy_json_string(device, "claim_token",
                                    config->claim_token, sizeof(config->claim_token));
    }

    mqtt = cJSON_GetObjectItemCaseSensitive(root, "mqtt");
    if (cJSON_IsObject(mqtt)) {
        eca_client_copy_json_bool(mqtt, "enabled", &config->mqtt_enabled);
        eca_client_copy_json_string(mqtt, "host",
                                    config->mqtt_host, sizeof(config->mqtt_host));
        eca_client_copy_json_int(mqtt, "port", &config->mqtt_port);
        eca_client_copy_json_string(mqtt, "client_id",
                                    config->mqtt_client_id, sizeof(config->mqtt_client_id));
        eca_client_copy_json_string(mqtt, "username",
                                    config->mqtt_username, sizeof(config->mqtt_username));
        eca_client_copy_json_string(mqtt, "password",
                                    config->mqtt_password, sizeof(config->mqtt_password));
        eca_client_copy_json_string(mqtt, "command_topic",
                                    config->mqtt_command_topic,
                                    sizeof(config->mqtt_command_topic));
        eca_client_copy_json_string(mqtt, "command_topic_template",
                                    config->mqtt_command_topic_template,
                                    sizeof(config->mqtt_command_topic_template));
        eca_client_copy_json_string(mqtt, "response_topic",
                                    config->mqtt_response_topic,
                                    sizeof(config->mqtt_response_topic));
        eca_client_copy_json_string(mqtt, "response_topic_template",
                                    config->mqtt_response_topic_template,
                                    sizeof(config->mqtt_response_topic_template));
        eca_client_copy_json_int(mqtt, "keepalive_seconds",
                                 &config->mqtt_keepalive_seconds);
    }

    http = cJSON_GetObjectItemCaseSensitive(root, "http");
    if (cJSON_IsObject(http)) {
        eca_client_copy_json_bool(http, "enabled", &config->http_enabled);
        eca_client_copy_json_string(http, "base_url",
                                    config->http_base_url, sizeof(config->http_base_url));
        eca_client_copy_json_string(http, "token",
                                    config->http_token, sizeof(config->http_token));
    }

    intervals = cJSON_GetObjectItemCaseSensitive(root, "intervals");
    if (cJSON_IsObject(intervals)) {
        eca_client_copy_json_int(intervals, "heartbeat_seconds",
                                 &config->heartbeat_interval_seconds);
        eca_client_copy_json_int(intervals, "telemetry_seconds",
                                 &config->telemetry_interval_seconds);
    }

    ret = eca_client_config_validate(config);
    cJSON_Delete(root);
    if (ret < 0) {
        return ret;
    }

    eca_client_config_normalize(config);
    return 0;
}

static void eca_client_expand_device_topic(const char *templ, const char *device_id,
                                           char *out, size_t out_size)
{
    const char *placeholder = "{device_id}";
    const char *p = strstr(templ, placeholder);
    size_t prefix_len;

    if (p == NULL) {
        snprintf(out, out_size, "%s", templ);
        return;
    }

    prefix_len = (size_t)(p - templ);
    if (prefix_len >= out_size) {
        prefix_len = out_size - 1;
    }

    memcpy(out, templ, prefix_len);
    out[prefix_len] = '\0';
    snprintf(out + prefix_len, out_size - prefix_len, "%s%s",
             device_id, p + strlen(placeholder));
}

static void eca_client_get_command_topic(char *out, size_t out_size)
{
    eca_client_net_config_t *config = &g_net.config;

    if (config->mqtt_command_topic[0] != '\0') {
        snprintf(out, out_size, "%s", config->mqtt_command_topic);
        return;
    }

    eca_client_expand_device_topic(config->mqtt_command_topic_template,
                                   config->device_id, out, out_size);
}

static void eca_client_get_response_topic(char *out, size_t out_size)
{
    eca_client_net_config_t *config = &g_net.config;

    if (config->mqtt_response_topic[0] != '\0') {
        snprintf(out, out_size, "%s", config->mqtt_response_topic);
        return;
    }

    eca_client_expand_device_topic(config->mqtt_response_topic_template,
                                   config->device_id, out, out_size);
}

static eca_client_pending_request_t *eca_client_find_pending(
    const char *request_id)
{
    int i;

    for (i = 0; i < ECA_CLIENT_PENDING_REQUESTS; i++) {
        if (g_pending[i].active &&
            strcmp(g_pending[i].request_id, request_id) == 0) {
            return &g_pending[i];
        }
    }

    return NULL;
}

static void eca_client_handle_mqtt_response(const char *payload)
{
    cJSON *root;
    cJSON *item;
    eca_client_pending_request_t *pending;
    const char *request_id;
    const char *status;

    root = cJSON_Parse(payload);
    if (root == NULL) {
        ECA_LOGW("mqtt response parse failed");
        return;
    }

    item = cJSON_GetObjectItemCaseSensitive(root, "request_id");
    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        cJSON_Delete(root);
        return;
    }
    request_id = item->valuestring;

    pthread_mutex_lock(&g_request_lock);
    pending = eca_client_find_pending(request_id);
    if (pending != NULL) {
        snprintf(pending->response.request_id,
                 sizeof(pending->response.request_id), "%s", request_id);
        item = cJSON_GetObjectItemCaseSensitive(root, "status");
        status = (cJSON_IsString(item) && item->valuestring != NULL) ?
            item->valuestring : "ok";
        snprintf(pending->response.status,
                 sizeof(pending->response.status), "%s", status);
        item = cJSON_GetObjectItemCaseSensitive(root, "data");
        if (item != NULL) {
            char *data_str = cJSON_PrintUnformatted(item);
            if (data_str != NULL) {
                snprintf(pending->response.data,
                         sizeof(pending->response.data), "%s", data_str);
                cJSON_free(data_str);
            }
        } else {
            pending->response.data[0] = '\0';
        }
        pending->response.received_at_ms = eca_client_now_ms();
        pending->fulfilled = true;
        pthread_cond_broadcast(&g_request_cond);
        ECA_LOGI("mqtt response matched request_id=%s status=%s",
                 request_id, status);
    } else {
        ECA_LOGD("mqtt response orphan request_id=%s", request_id);
    }
    pthread_mutex_unlock(&g_request_lock);

    cJSON_Delete(root);
}


static int eca_client_network_refresh_identity(void)
{
    char device_id[sizeof(g_net.config.device_id)];
    char old_device_id[sizeof(g_net.config.device_id)];
    char old_client_id[sizeof(g_net.config.mqtt_client_id)];
    bool changed = false;
    int ret;

    ret = eca_client_format_wifi_mac(device_id, sizeof(device_id));
    if (ret < 0) {
        return ret;
    }

    pthread_mutex_lock(&g_net.publish_lock);
    if (strcmp(g_net.config.device_id, device_id) != 0) {
        snprintf(old_device_id, sizeof(old_device_id), "%s",
                 g_net.config.device_id);
        snprintf(old_client_id, sizeof(old_client_id), "%s",
                 g_net.config.mqtt_client_id);
        snprintf(g_net.config.device_id, sizeof(g_net.config.device_id), "%s",
                 device_id);
        if (g_net.config.mqtt_client_id_auto) {
            snprintf(g_net.config.mqtt_client_id,
                     sizeof(g_net.config.mqtt_client_id),
                     "EcaClient-%s", g_net.config.device_id);
        }
        changed = true;
    }
    g_net.identity_refresh_requested = false;
    pthread_mutex_unlock(&g_net.publish_lock);

    if (changed) {
        eca_client_command_dispatcher_update_device_id(device_id);
        ECA_LOGI("network identity refreshed device_id=%s -> %s client_id=%s -> %s",
                 old_device_id, device_id, old_client_id,
                 g_net.config.mqtt_client_id);
    }

    return changed ? 1 : 0;
}

static int eca_client_tcp_connect_addr(const char *host, int port,
                                       const struct sockaddr *addr,
                                       socklen_t addr_len, int family,
                                       int timeout_seconds)
{
    struct timeval tv;
    int fd;
    int flags;
    int ret;
    int last_error = 0;

    fd = socket(family, SOCK_STREAM, 0);
    if (fd < 0) {
        ECA_LOGW("tcp socket failed host=%s port=%d family=%d errno=%d",
                 host, port, family, errno);
        return -1;
    }

    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        last_error = errno;
        close(fd);
        ECA_LOGW("tcp fcntl get failed host=%s port=%d fd=%d errno=%d",
                 host, port, fd, last_error);
        return -1;
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        last_error = errno;
        close(fd);
        ECA_LOGW("tcp fcntl nonblock failed host=%s port=%d fd=%d errno=%d",
                 host, port, fd, last_error);
        return -1;
    }

    ECA_LOGI("tcp connect start host=%s port=%d family=%d fd=%d timeout=%d",
             host, port, family, fd, timeout_seconds);
    ret = connect(fd, addr, addr_len);
    if (ret < 0 && errno == EINPROGRESS) {
        fd_set wfds;

        FD_ZERO(&wfds);
        FD_SET(fd, &wfds);
        tv.tv_sec = timeout_seconds;
        tv.tv_usec = 0;
        ret = select(fd + 1, NULL, &wfds, NULL, &tv);
        ECA_LOGI("tcp connect select host=%s port=%d fd=%d ret=%d errno=%d",
                 host, port, fd, ret, errno);
        if (ret > 0) {
            int error = 0;
            socklen_t len = sizeof(error);

            if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
                last_error = errno;
                ret = -1;
            } else {
                last_error = error;
                ret = error == 0 ? 0 : -1;
            }
        } else {
            last_error = ret == 0 ? ETIMEDOUT : errno;
            ret = -1;
        }
    } else if (ret < 0) {
        last_error = errno;
    }

    fcntl(fd, F_SETFL, flags);
    if (ret == 0) {
        tv.tv_sec = timeout_seconds;
        tv.tv_usec = 0;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        ECA_LOGI("tcp connected host=%s port=%d fd=%d", host, port, fd);
        return fd;
    }

    close(fd);
    ECA_LOGD("tcp connect failed host=%s port=%d fd=%d errno=%d",
             host, port, fd, last_error);
    return -1;
}

static int eca_client_tcp_connect(const char *host, int port, int timeout_seconds)
{
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    struct addrinfo *rp;
    struct sockaddr_in ipv4_addr;
    char port_text[12];
    int fd = -1;
    int ret;

    memset(&ipv4_addr, 0, sizeof(ipv4_addr));
    ipv4_addr.sin_family = AF_INET;
    ipv4_addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &ipv4_addr.sin_addr) == 1) {
        return eca_client_tcp_connect_addr(host, port,
                                           (const struct sockaddr *)&ipv4_addr,
                                           sizeof(ipv4_addr), AF_INET,
                                           timeout_seconds);
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(port_text, sizeof(port_text), "%d", port);

    ret = getaddrinfo(host, port_text, &hints, &result);
    if (ret != 0) {
        ECA_LOGD("tcp getaddrinfo failed host=%s port=%d ret=%d errno=%d",
                 host, port, ret, errno);
        return -1;
    }

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        fd = eca_client_tcp_connect_addr(host, port, rp->ai_addr,
                                         rp->ai_addrlen, rp->ai_family,
                                         timeout_seconds);
        if (fd >= 0) {
            break;
        }
    }

    freeaddrinfo(result);
    return fd;
}

static int eca_client_send_all(int fd, const uint8_t *data, size_t len)
{
    size_t sent = 0;

    while (sent < len) {
        struct timeval tv;
        fd_set wfds;
        ssize_t ret;
        int ready;

        FD_ZERO(&wfds);
        FD_SET(fd, &wfds);
        tv.tv_sec = ECA_CLIENT_IO_TIMEOUT_SECONDS;
        tv.tv_usec = 0;
        ready = select(fd + 1, NULL, &wfds, NULL, &tv);
        if (ready == 0) {
            return -ETIMEDOUT;
        }
        if (ready < 0) {
            return -errno;
        }

        ret = send(fd, data + sent, len - sent, 0);
        if (ret <= 0) {
            return ret == 0 ? -ECONNRESET : -errno;
        }
        sent += (size_t)ret;
    }

    return 0;
}

static int eca_client_recv_all(int fd, uint8_t *data, size_t len)
{
    size_t received = 0;

    while (received < len) {
        struct timeval tv;
        fd_set rfds;
        ssize_t ret;
        int ready;

        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        tv.tv_sec = ECA_CLIENT_IO_TIMEOUT_SECONDS;
        tv.tv_usec = 0;
        ready = select(fd + 1, &rfds, NULL, NULL, &tv);
        if (ready == 0) {
            return -ETIMEDOUT;
        }
        if (ready < 0) {
            return -errno;
        }

        ret = recv(fd, data + received, len - received, 0);
        if (ret <= 0) {
            return ret == 0 ? -ECONNRESET : -errno;
        }
        received += (size_t)ret;
    }

    return 0;
}

static size_t eca_client_mqtt_write_string(uint8_t *out, const char *value)
{
    size_t len = strlen(value);

    out[0] = (uint8_t)((len >> 8) & 0xff);
    out[1] = (uint8_t)(len & 0xff);
    memcpy(out + 2, value, len);
    return len + 2;
}

static size_t eca_client_mqtt_encode_length(uint8_t *out, size_t len)
{
    size_t n = 0;

    do {
        uint8_t digit = len % 128;
        len /= 128;
        if (len > 0) {
            digit |= 0x80;
        }
        out[n++] = digit;
    } while (len > 0 && n < 4);

    return n;
}

static int eca_client_mqtt_read_packet(int fd, uint8_t *type, uint8_t *packet,
                                       size_t packet_size, size_t *packet_len)
{
    uint8_t b;
    size_t remaining = 0;
    size_t multiplier = 1;
    int ret;

    ret = eca_client_recv_all(fd, type, 1);
    if (ret < 0) {
        return ret;
    }

    do {
        ret = eca_client_recv_all(fd, &b, 1);
        if (ret < 0) {
            return ret;
        }
        remaining += (size_t)(b & 127) * multiplier;
        multiplier *= 128;
        if (multiplier > 128 * 128 * 128 * 128) {
            return -EINVAL;
        }
    } while ((b & 128) != 0);

    if (remaining > packet_size) {
        return -EOVERFLOW;
    }

    ret = eca_client_recv_all(fd, packet, remaining);
    if (ret < 0) {
        return ret;
    }

    *packet_len = remaining;
    return 0;
}

static int eca_client_mqtt_connect(int fd, eca_client_net_config_t *config)
{
    uint8_t packet[ECA_CLIENT_PACKET_MAX];
    uint8_t body[ECA_CLIENT_PACKET_MAX];
    size_t pos = 0;
    size_t body_len = 0;
    uint8_t flags = 0x02;
    uint8_t type;
    size_t packet_len;
    int ret;

    body_len += eca_client_mqtt_write_string(body + body_len, "MQTT");
    body[body_len++] = 0x04;
    if (config->mqtt_username[0] != '\0') {
        flags |= 0x80;
        if (config->mqtt_password[0] != '\0') {
            flags |= 0x40;
        }
    }
    body[body_len++] = flags;
    body[body_len++] = (uint8_t)((config->mqtt_keepalive_seconds >> 8) & 0xff);
    body[body_len++] = (uint8_t)(config->mqtt_keepalive_seconds & 0xff);
    body_len += eca_client_mqtt_write_string(body + body_len, config->mqtt_client_id);
    if (config->mqtt_username[0] != '\0') {
        body_len += eca_client_mqtt_write_string(body + body_len, config->mqtt_username);
        if (config->mqtt_password[0] != '\0') {
            body_len += eca_client_mqtt_write_string(body + body_len, config->mqtt_password);
        }
    }

    packet[pos++] = 0x10;
    pos += eca_client_mqtt_encode_length(packet + pos, body_len);
    memcpy(packet + pos, body, body_len);
    pos += body_len;

    ECA_LOGI("mqtt send connect client_id=%s bytes=%u",
             config->mqtt_client_id, (unsigned int)pos);
    ret = eca_client_send_all(fd, packet, pos);
    if (ret < 0) {
        ECA_LOGD("mqtt send connect failed ret=%d", ret);
        return ret;
    }

    ECA_LOGI("mqtt wait connack fd=%d timeout=%d", fd,
             ECA_CLIENT_IO_TIMEOUT_SECONDS);
    ret = eca_client_mqtt_read_packet(fd, &type, packet, sizeof(packet), &packet_len);
    if (ret < 0) {
        ECA_LOGD("mqtt read connack failed ret=%d", ret);
        return ret;
    }

    ECA_LOGI("mqtt connack type=0x%02x len=%u flags=0x%02x rc=%u",
             type, (unsigned int)packet_len,
             packet_len > 0 ? packet[0] : 0,
             packet_len > 1 ? packet[1] : 255);
    if (type != 0x20 || packet_len < 2 || packet[1] != 0) {
        return -ECONNREFUSED;
    }

    return 0;
}

static int eca_client_mqtt_subscribe(int fd, const char *topic)
{
    uint8_t packet[ECA_CLIENT_PACKET_MAX];
    uint8_t body[ECA_CLIENT_PACKET_MAX];
    size_t pos = 0;
    size_t body_len = 0;
    uint8_t type;
    size_t packet_len;
    int ret;

    body[body_len++] = 0;
    body[body_len++] = 1;
    body_len += eca_client_mqtt_write_string(body + body_len, topic);
    body[body_len++] = 0;

    packet[pos++] = 0x82;
    pos += eca_client_mqtt_encode_length(packet + pos, body_len);
    memcpy(packet + pos, body, body_len);
    pos += body_len;

    ECA_LOGI("mqtt subscribe send topic=%s bytes=%u", topic,
             (unsigned int)pos);
    ret = eca_client_send_all(fd, packet, pos);
    if (ret < 0) {
        ECA_LOGW("mqtt subscribe send failed topic=%s ret=%d", topic, ret);
        return ret;
    }

    ret = eca_client_mqtt_read_packet(fd, &type, packet, sizeof(packet), &packet_len);
    if (ret < 0) {
        ECA_LOGW("mqtt subscribe ack read failed topic=%s ret=%d", topic, ret);
        return ret;
    }

    ECA_LOGI("mqtt subscribe ack topic=%s type=0x%02x len=%u rc=%u",
             topic, type, (unsigned int)packet_len,
             packet_len > 2 ? packet[2] : 255);
    if (type != 0x90 || packet_len < 3 || packet[2] == 0x80) {
        return -ECONNREFUSED;
    }

    return 0;
}

static int eca_client_mqtt_publish(int fd, const char *topic, const char *payload)
{
    uint8_t packet[ECA_CLIENT_PACKET_MAX];
    size_t pos = 0;
    size_t topic_len = strlen(topic);
    size_t payload_len = strlen(payload);
    size_t body_len = 2 + topic_len + payload_len;

    if (body_len + 5 > sizeof(packet)) {
        return -E2BIG;
    }

    packet[pos++] = 0x30;
    pos += eca_client_mqtt_encode_length(packet + pos, body_len);
    pos += eca_client_mqtt_write_string(packet + pos, topic);
    memcpy(packet + pos, payload, payload_len);
    pos += payload_len;

    return eca_client_send_all(fd, packet, pos);
}

static int eca_client_mqtt_ping(int fd)
{
    static const uint8_t ping[] = {0xc0, 0x00};

    return eca_client_send_all(fd, ping, sizeof(ping));
}

static int eca_client_mqtt_puback(int fd, uint16_t packet_id)
{
    uint8_t packet[4];

    packet[0] = 0x40;
    packet[1] = 0x02;
    packet[2] = (uint8_t)((packet_id >> 8) & 0xff);
    packet[3] = (uint8_t)(packet_id & 0xff);
    return eca_client_send_all(fd, packet, sizeof(packet));
}

static void eca_client_build_topic(char *out, size_t out_size,
                                   const char *device_id, const char *type)
{
    snprintf(out, out_size, "eca/device/%s/%s/up", device_id, type);
}

static void eca_client_close_mqtt_socket(bool send_disconnect)
{
    pthread_mutex_lock(&g_net.publish_lock);
    if (g_net.sockfd >= 0) {
        if (send_disconnect) {
            static const uint8_t disconnect[] = {0xe0, 0x00};
            eca_client_send_all(g_net.sockfd, disconnect, sizeof(disconnect));
        }
        close(g_net.sockfd);
        g_net.sockfd = -1;
    }
    pthread_mutex_unlock(&g_net.publish_lock);
}

static int eca_client_get_mqtt_socket(void)
{
    int fd;

    pthread_mutex_lock(&g_net.publish_lock);
    fd = g_net.sockfd;
    pthread_mutex_unlock(&g_net.publish_lock);
    return fd;
}

static void eca_client_next_event_id(char *out, size_t out_size,
                                     const char *prefix)
{
    unsigned int id = eca_client_device_state_next_event_counter();

    snprintf(out, out_size, "%s_%llu_%u", prefix, eca_client_now_ms(), id);
}

static void eca_client_next_ack_id(char *out, size_t out_size)
{
    unsigned int id = eca_client_device_state_next_event_counter();

    snprintf(out, out_size, "ack_%llu_%u", eca_client_now_ms(), id);
}

static int eca_client_http_parse_url(const char *url, char *host, size_t host_size,
                                     int *port, char *base_path, size_t path_size)
{
    const char *p = url;
    const char *host_start;
    const char *path_start;
    const char *colon;
    size_t host_len;

    if (strncmp(p, "http://", 7) == 0) {
        p += 7;
    } else {
        return -EINVAL;
    }

    host_start = p;
    path_start = strchr(host_start, '/');
    if (path_start == NULL) {
        path_start = host_start + strlen(host_start);
        if (path_size > 0) {
            base_path[0] = '\0';
        }
    } else {
        snprintf(base_path, path_size, "%s", path_start);
    }

    colon = memchr(host_start, ':', (size_t)(path_start - host_start));
    if (colon != NULL) {
        host_len = (size_t)(colon - host_start);
        *port = atoi(colon + 1);
    } else {
        host_len = (size_t)(path_start - host_start);
        *port = 80;
    }

    if (host_len == 0 || host_len >= host_size) {
        return -EINVAL;
    }

    memcpy(host, host_start, host_len);
    host[host_len] = '\0';
    return 0;
}

static int eca_client_http_read_response_body(int fd, char *body,
                                              size_t body_size,
                                              int *status_code)
{
    char response[ECA_CLIENT_HTTP_BODY_MAX + 512];
    char *header_end;
    char *status;
    size_t total = 0;
    int code = 0;

    if (body == NULL || body_size == 0) {
        return -EINVAL;
    }

    while (total < sizeof(response) - 1) {
        ssize_t len = recv(fd, response + total, sizeof(response) - 1 - total, 0);

        if (len < 0) {
            return -errno;
        }
        if (len == 0) {
            break;
        }
        total += (size_t)len;
    }

    response[total] = '\0';
    status = strchr(response, ' ');
    if (status != NULL) {
        code = atoi(status + 1);
    }
    if (status_code != NULL) {
        *status_code = code;
    }
    if (code < 200 || code >= 300) {
        return -EIO;
    }

    header_end = strstr(response, "\r\n\r\n");
    if (header_end == NULL) {
        return -EINVAL;
    }
    header_end += 4;
    snprintf(body, body_size, "%s", header_end);
    return 0;
}

static int eca_client_http_post_event(const char *type, const char *payload)
{
    eca_client_net_config_t *config = &g_net.config;
    char host[80];
    char base_path[80];
    char path[160];
    char request[ECA_CLIENT_HTTP_BODY_MAX + 512];
    int port;
    int fd;
    int ret;

    if (!config->http_enabled || config->http_token[0] == '\0') {
        return 0;
    }

    if (strcmp(type, "voice_keyword") == 0) {
        return 0;
    }

    ret = eca_client_http_parse_url(config->http_base_url, host, sizeof(host),
                                    &port, base_path, sizeof(base_path));
    if (ret < 0) {
        return ret;
    }

    if (strcmp(type, "telemetry") == 0) {
        snprintf(path, sizeof(path), "%s/api/v1/devices/%s/telemetry/ingest",
                 base_path, config->device_id);
    } else if (strcmp(type, "heartbeat") == 0) {
        snprintf(path, sizeof(path), "%s/api/v1/devices/%s/heartbeat/ingest",
                 base_path, config->device_id);
    } else if (strcmp(type, "alert") == 0) {
        snprintf(path, sizeof(path), "%s/api/v1/devices/%s/alerts/ingest",
                 base_path, config->device_id);
    } else if (strcmp(type, "ack") == 0) {
        snprintf(path, sizeof(path), "%s/api/v1/devices/%s/acks/ingest",
                 base_path, config->device_id);
    } else {
        return -EINVAL;
    }

    snprintf(request, sizeof(request),
             "POST %s HTTP/1.1\r\n"
             "Host: %s:%d\r\n"
             "Authorization: Bearer %s\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %u\r\n"
             "Connection: close\r\n"
             "\r\n"
             "%s",
             path, host, port, config->http_token, (unsigned int)strlen(payload), payload);

    fd = eca_client_tcp_connect(host, port, 3);
    if (fd < 0) {
        return -errno;
    }

    ret = eca_client_send_all(fd, (const uint8_t *)request, strlen(request));
    close(fd);
    return ret;
}

static void eca_client_publish_event(const char *type, const char *payload)
{
    char topic[ECA_CLIENT_TOPIC_MAX];

    pthread_mutex_lock(&g_net.publish_lock);
    if (g_net.sockfd >= 0 && g_net.config.mqtt_enabled) {
        eca_client_build_topic(topic, sizeof(topic), g_net.config.device_id, type);
        if (eca_client_mqtt_publish(g_net.sockfd, topic, payload) < 0) {
            ECA_LOGD("mqtt publish %s failed topic=%s", type, topic);
            close(g_net.sockfd);
            g_net.sockfd = -1;
        } else {
            ECA_LOGI("mqtt publish %s topic=%s", type, topic);
        }
    } else if (g_net.config.mqtt_enabled) {
        ECA_LOGD("mqtt publish %s skipped: not connected", type);
    }
    pthread_mutex_unlock(&g_net.publish_lock);

    if (eca_client_http_post_event(type, payload) < 0) {
        ECA_LOGD("http post %s failed", type);
    }
}

static void eca_client_json_escape(const char *src, char *dst, size_t dst_size)
{
    size_t pos = 0;

    if (dst_size == 0) {
        return;
    }

    while (src != NULL && *src != '\0' && pos + 1 < dst_size) {
        char ch = *src++;

        if ((ch == '"' || ch == '\\') && pos + 2 < dst_size) {
            dst[pos++] = '\\';
            dst[pos++] = ch;
        } else if (ch == '\n' && pos + 2 < dst_size) {
            dst[pos++] = '\\';
            dst[pos++] = 'n';
        } else if (ch == '\r' && pos + 2 < dst_size) {
            dst[pos++] = '\\';
            dst[pos++] = 'r';
        } else if (ch == '\t' && pos + 2 < dst_size) {
            dst[pos++] = '\\';
            dst[pos++] = 't';
        } else if ((unsigned char)ch >= 0x20) {
            dst[pos++] = ch;
        }
    }

    dst[pos] = '\0';
}

static void eca_client_publish_heartbeat(void)
{
    char event_id[64];
    char payload[ECA_CLIENT_HTTP_BODY_MAX];
    long long now = eca_client_now_ms();

    eca_client_next_event_id(event_id, sizeof(event_id), "evt_heartbeat");
    snprintf(payload, sizeof(payload),
             "{\"schema_version\":1,\"event_id\":\"%s\",\"device_id\":\"%s\","
             "\"type\":\"heartbeat\",\"timestamp\":%lld,\"payload\":{"
             "\"firmware_version\":\"0.1.0\",\"model_version\":\"0.1.0\","
             "\"config_version\":1,\"network\":\"wifi\",\"uptime_seconds\":%lld}}",
             event_id, g_net.config.device_id, now, now / 1000);
    eca_client_publish_event("heartbeat", payload);
}

static void eca_client_publish_telemetry(void)
{
    eca_client_device_snapshot_t snapshot;
    char event_id[64];
    char payload[ECA_CLIENT_HTTP_BODY_MAX];
    long long now = eca_client_now_ms();

    eca_client_device_state_get(&snapshot);
    eca_client_next_event_id(event_id, sizeof(event_id), "evt_telemetry");
    snprintf(payload, sizeof(payload),
             "{\"schema_version\":1,\"event_id\":\"%s\",\"device_id\":\"%s\","
             "\"type\":\"telemetry\",\"timestamp\":%lld,\"payload\":{"
             "\"battery\":%d,\"volume\":%d,\"temperature\":%.1f,"
             "\"humidity\":%.1f,\"network\":\"wifi\"}}",
             event_id, g_net.config.device_id, now, snapshot.battery,
             snapshot.volume, (double)snapshot.temperature,
             (double)snapshot.humidity);
    ECA_LOGI("telemetry device_id=%s temperature=%.1f humidity=%.1f",
             g_net.config.device_id, (double)snapshot.temperature,
             (double)snapshot.humidity);
    eca_client_publish_event("telemetry", payload);
}

static void eca_client_publish_ack(const char *command_id, const char *status,
                                   const char *error, void *arg)
{
    char event_id[64];
    char ack_id[64];
    char payload[ECA_CLIENT_HTTP_BODY_MAX];
    char command_id_json[80];
    char status_json[24];
    char error_json[160];
    long long now = eca_client_now_ms();

    (void)arg;

    eca_client_next_event_id(event_id, sizeof(event_id), "evt_ack");
    eca_client_next_ack_id(ack_id, sizeof(ack_id));
    eca_client_json_escape(command_id, command_id_json, sizeof(command_id_json));
    eca_client_json_escape(status, status_json, sizeof(status_json));

    if (error == NULL) {
        snprintf(payload, sizeof(payload),
                 "{\"schema_version\":1,\"ack_id\":\"%s\",\"event_id\":\"%s\","
                 "\"device_id\":\"%s\",\"type\":\"ack\",\"timestamp\":%lld,"
                 "\"ref_id\":\"%s\",\"ref_type\":\"command\",\"status\":\"%s\","
                 "\"error\":null}",
                 ack_id, event_id, g_net.config.device_id, now, command_id_json,
                 status_json);
    } else {
        eca_client_json_escape(error, error_json, sizeof(error_json));
        snprintf(payload, sizeof(payload),
                 "{\"schema_version\":1,\"ack_id\":\"%s\",\"event_id\":\"%s\","
                 "\"device_id\":\"%s\",\"type\":\"ack\",\"timestamp\":%lld,"
                 "\"ref_id\":\"%s\",\"ref_type\":\"command\",\"status\":\"%s\","
                 "\"error\":\"%s\"}",
                 ack_id, event_id, g_net.config.device_id, now, command_id_json,
                 status_json, error_json);
    }

    eca_client_publish_event("ack", payload);
}

int eca_client_network_publish_voice_keyword(const char *keyword,
                                             int confidence_percent,
                                             const char *source)
{
    char event_id[64];
    char payload[ECA_CLIENT_HTTP_BODY_MAX];
    char keyword_json[32];
    char source_json[32];
    long long now;

    if (keyword == NULL || keyword[0] == '\0') {
        return -EINVAL;
    }

    now = eca_client_now_ms();
    eca_client_next_event_id(event_id, sizeof(event_id), "evt_voice_keyword");
    eca_client_json_escape(keyword, keyword_json, sizeof(keyword_json));
    eca_client_json_escape(source != NULL ? source : "dsp",
                           source_json, sizeof(source_json));

    if (confidence_percent >= 0 && confidence_percent <= 100) {
        snprintf(payload, sizeof(payload),
                 "{\"schema_version\":1,\"event_id\":\"%s\",\"device_id\":\"%s\","
                 "\"type\":\"voice_keyword\",\"timestamp\":%lld,\"payload\":{"
                 "\"keyword\":\"%s\",\"confidence\":%d,\"source\":\"%s\"}}",
                 event_id, g_net.config.device_id, now, keyword_json,
                 confidence_percent, source_json);
    } else {
        snprintf(payload, sizeof(payload),
                 "{\"schema_version\":1,\"event_id\":\"%s\",\"device_id\":\"%s\","
                 "\"type\":\"voice_keyword\",\"timestamp\":%lld,\"payload\":{"
                 "\"keyword\":\"%s\",\"source\":\"%s\"}}",
                 event_id, g_net.config.device_id, now, keyword_json,
                 source_json);
    }

    eca_client_publish_event("voice_keyword", payload);
    return 0;
}

int eca_client_network_publish_emergency_alert(void)
{
    char event_id[64];
    char payload[ECA_CLIENT_HTTP_BODY_MAX];
    long long now = eca_client_now_ms();

    eca_client_next_event_id(event_id, sizeof(event_id), "evt_alert");
    snprintf(payload, sizeof(payload),
             "{\"schema_version\":1,\"event_id\":\"%s\",\"device_id\":\"%s\","
             "\"type\":\"alert\",\"timestamp\":%lld,\"payload\":{"
             "\"alert_type\":\"emergency_button\",\"level\":\"critical\","
             "\"description\":\"紧急求助已触发\",\"source\":\"device_button\"}}",
             event_id, g_net.config.device_id, now);

    eca_client_publish_event("alert", payload);
    return 0;
}

static int eca_client_process_mqtt_packet(int fd)
{
    uint8_t type;
    uint8_t packet[ECA_CLIENT_PACKET_MAX];
    char topic[ECA_CLIENT_TOPIC_MAX];
    size_t packet_len;
    uint16_t topic_len;
    size_t payload_offset;
    size_t topic_copy_len;
    uint8_t qos;
    uint16_t packet_id = 0;
    int ret;

    ret = eca_client_mqtt_read_packet(fd, &type, packet, sizeof(packet) - 1,
                                      &packet_len);
    if (ret < 0) {
        return ret;
    }

    if ((type >> 4) == 3 && packet_len >= 2) {
        qos = (uint8_t)((type & 0x06) >> 1);
        topic_len = ((uint16_t)packet[0] << 8) | packet[1];
        payload_offset = 2 + topic_len;
        if (payload_offset > packet_len) {
            ECA_LOGW("mqtt publish malformed topic_len=%u packet_len=%u",
                     topic_len, (unsigned int)packet_len);
            return -EINVAL;
        }

        topic_copy_len = topic_len;
        if (topic_copy_len >= sizeof(topic)) {
            topic_copy_len = sizeof(topic) - 1;
        }
        memcpy(topic, &packet[2], topic_copy_len);
        topic[topic_copy_len] = '\0';

        if (qos == 1) {
            if (payload_offset + 2 > packet_len) {
                ECA_LOGW("mqtt publish malformed qos1 topic=%s packet_len=%u",
                         topic, (unsigned int)packet_len);
                return -EINVAL;
            }
            packet_id = ((uint16_t)packet[payload_offset] << 8) |
                        packet[payload_offset + 1];
            payload_offset += 2;
        } else if (qos > 1) {
            ECA_LOGW("unsupported mqtt publish qos %u", qos);
            return 0;
        }

        if (payload_offset <= packet_len) {
            char response_topic[ECA_CLIENT_TOPIC_MAX];
            packet[packet_len] = '\0';
            ECA_LOGI("mqtt publish received topic=%s qos=%u payload_len=%u payload=%s",
                     topic, qos, (unsigned int)(packet_len - payload_offset),
                     (const char *)&packet[payload_offset]);
            eca_client_get_response_topic(response_topic,
                                          sizeof(response_topic));
            if (strcmp(topic, response_topic) == 0) {
                eca_client_handle_mqtt_response(
                    (const char *)&packet[payload_offset]);
            } else {
                ret = eca_client_command_dispatcher_enqueue(
                    (const char *)&packet[payload_offset]);
                if (ret < 0) {
                    ECA_LOGW("enqueue command failed %d", ret);
                    return ret;
                }
            }
            if (qos == 1) {
                pthread_mutex_lock(&g_net.publish_lock);
                if (g_net.sockfd >= 0) {
                    eca_client_mqtt_puback(g_net.sockfd, packet_id);
                }
                pthread_mutex_unlock(&g_net.publish_lock);
            }
        }
    } else if ((type >> 4) == 13) {
        return 0;
    }

    return 0;
}

static void *eca_client_network_thread(void *arg)
{
    eca_client_net_config_t *config = &g_net.config;
    long long next_heartbeat = 0;
    long long next_telemetry = 0;
    char command_topic[ECA_CLIENT_TOPIC_MAX];
    bool wifi_was_ready = false;
    int camera_uart_sent = 0;

    (void)arg;
    eca_client_get_command_topic(command_topic, sizeof(command_topic));
    ECA_LOGI("network thread started mqtt=%d host=%s port=%d command_topic=%s telemetry=%ds heartbeat=%ds",
             config->mqtt_enabled ? 1 : 0, config->mqtt_host,
             config->mqtt_port, command_topic,
             config->telemetry_interval_seconds,
             config->heartbeat_interval_seconds);

    while (g_net.running) {
        long long now;
        char ip[INET_ADDRSTRLEN];
        char gateway[INET_ADDRSTRLEN];

        if (config->mqtt_enabled && eca_client_get_mqtt_socket() < 0) {
            int fd;
            int identity_ret;
            int mqtt_ret;
            int sub_ret;

            if (!eca_client_wifi_ipv4_ready(ip, sizeof(ip))) {
                if (g_net.mqtt_defer_reason != ECA_CLIENT_MQTT_DEFER_NO_IPV4) {
                    g_net.mqtt_defer_reason = ECA_CLIENT_MQTT_DEFER_NO_IPV4;
                    ECA_LOGW("mqtt connect deferred: %s has no ipv4 address",
                             ECA_CLIENT_WIFI_IFNAME);
                }
                if (wifi_was_ready) {
                    wifi_was_ready = false;
                    camera_uart_sent = 0;
                    ECA_LOGI("wifi disconnected, reset camera uart send flag");
                }
                sleep(1);
                continue;
            }

            if (!eca_client_wifi_route_ready(gateway, sizeof(gateway))) {
                if (g_net.mqtt_defer_reason != ECA_CLIENT_MQTT_DEFER_NO_GATEWAY) {
                    g_net.mqtt_defer_reason = ECA_CLIENT_MQTT_DEFER_NO_GATEWAY;
                    ECA_LOGW("mqtt connect deferred: %s has no default gateway ip=%s",
                             ECA_CLIENT_WIFI_IFNAME, ip);
                }
                sleep(1);
                continue;
            }

            identity_ret = eca_client_network_refresh_identity();
            if (identity_ret < 0) {
                ECA_LOGW("network identity refresh skipped ret=%d",
                         identity_ret);
            }
            eca_client_get_command_topic(command_topic, sizeof(command_topic));
            g_net.mqtt_defer_reason = ECA_CLIENT_MQTT_DEFER_NONE;
            ECA_LOGI("wifi ready %s ip=%s gateway=%s",
                     ECA_CLIENT_WIFI_IFNAME, ip, gateway);

            if (!wifi_was_ready) {
                wifi_was_ready = true;
                camera_uart_sent = eca_client_send_wifi_config_uart();
                if (camera_uart_sent < 0) {
                    ECA_LOGW("send wifi config to camera failed ret=%d",
                             camera_uart_sent);
                } else {
                    ECA_LOGI("send wifi config to camera success");
                }
            }

            ECA_LOGI("mqtt connect try host=%s port=%d client_id=%s",
                     config->mqtt_host, config->mqtt_port,
                     config->mqtt_client_id);
            fd = eca_client_tcp_connect(config->mqtt_host, config->mqtt_port, 5);
            mqtt_ret = fd >= 0 ? eca_client_mqtt_connect(fd, config) : fd;
            sub_ret = mqtt_ret == 0 ?
                eca_client_mqtt_subscribe(fd, command_topic) : mqtt_ret;
            if (sub_ret == 0) {
                char response_topic[ECA_CLIENT_TOPIC_MAX];
                eca_client_get_response_topic(response_topic,
                                              sizeof(response_topic));
                sub_ret = eca_client_mqtt_subscribe(fd, response_topic);
            }

            if (fd >= 0 && mqtt_ret == 0 && sub_ret == 0) {
                pthread_mutex_lock(&g_net.publish_lock);
                g_net.sockfd = fd;
                pthread_mutex_unlock(&g_net.publish_lock);
                ECA_LOGI("mqtt connected %s:%d topic=%s",
                         config->mqtt_host, config->mqtt_port, command_topic);
                next_heartbeat = 0;
                next_telemetry = 0;
            } else {
                if (fd >= 0) {
                    close(fd);
                }
                ECA_LOGW("mqtt connect failed host=%s port=%d fd=%d mqtt_ret=%d sub_ret=%d topic=%s",
                         config->mqtt_host, config->mqtt_port, fd, mqtt_ret,
                         sub_ret, command_topic);
                sleep(3);
                continue;
            }
        }

        now = eca_client_now_ms();
        if (now >= next_heartbeat) {
            eca_client_publish_heartbeat();
            next_heartbeat = now + (long long)config->heartbeat_interval_seconds * 1000;
        }

        if (now >= next_telemetry) {
            eca_client_publish_telemetry();
            next_telemetry = now + (long long)config->telemetry_interval_seconds * 1000;
        }

        if (eca_client_get_mqtt_socket() >= 0) {
            fd_set rfds;
            struct timeval tv;
            int fd = eca_client_get_mqtt_socket();
            int ret;

            if (fd < 0) {
                continue;
            }

            FD_ZERO(&rfds);
            FD_SET(fd, &rfds);
            tv.tv_sec = 1;
            tv.tv_usec = 0;
            ret = select(fd + 1, &rfds, NULL, NULL, &tv);
            if (ret > 0 && FD_ISSET(fd, &rfds)) {
                if (eca_client_process_mqtt_packet(fd) < 0) {
                    eca_client_close_mqtt_socket(false);
                }
            } else if (ret == 0) {
                pthread_mutex_lock(&g_net.publish_lock);
                if (g_net.sockfd >= 0) {
                    if (eca_client_mqtt_ping(g_net.sockfd) < 0) {
                        close(g_net.sockfd);
                        g_net.sockfd = -1;
                    }
                }
                pthread_mutex_unlock(&g_net.publish_lock);
            }
        } else {
            sleep(1);
        }
    }

    eca_client_close_mqtt_socket(true);

    return NULL;
}

int eca_client_network_start(eca_client_model_t *model)
{
    char command_topic[ECA_CLIENT_TOPIC_MAX];
    pthread_attr_t attr;
    bool attr_initialized = false;
    int ret;

    (void)model;
    if (g_net.thread_started) {
        ECA_LOGW("network start skipped: already started");
        return 0;
    }

    memset(&g_net, 0, sizeof(g_net));
    g_net.sockfd = -1;

    ret = pthread_mutex_init(&g_net.publish_lock, NULL);
    if (ret != 0) {
        ECA_LOGE("network mutex init failed ret=%d", ret);
        return -ret;
    }
    g_net.lock_initialized = true;

    ret = eca_client_config_load(&g_net.config);
    if (ret < 0) {
        ECA_LOGE("network config load failed ret=%d", ret);
        pthread_mutex_destroy(&g_net.publish_lock);
        g_net.lock_initialized = false;
        return ret;
    }

    eca_client_get_command_topic(command_topic, sizeof(command_topic));
    ECA_LOGI("network config device_id=%s mqtt=%d %s:%d client_id=%s command_topic=%s http=%d %s telemetry=%ds",
             g_net.config.device_id, g_net.config.mqtt_enabled ? 1 : 0,
             g_net.config.mqtt_host, g_net.config.mqtt_port,
             g_net.config.mqtt_client_id, command_topic,
             g_net.config.http_enabled ? 1 : 0, g_net.config.http_base_url,
             g_net.config.telemetry_interval_seconds);

    ret = eca_client_command_dispatcher_start(g_net.config.device_id,
                                              eca_client_publish_ack, NULL);
    if (ret < 0) {
        ECA_LOGE("command dispatcher start failed ret=%d", ret);
        pthread_mutex_destroy(&g_net.publish_lock);
        g_net.lock_initialized = false;
        return ret;
    }

    ret = pthread_attr_init(&attr);
    if (ret == 0) {
        attr_initialized = true;
        ret = pthread_attr_setstacksize(&attr,
                                        ECA_CLIENT_NETWORK_THREAD_STACKSIZE);
    }
    if (ret != 0) {
        ECA_LOGE("network thread attr init failed ret=%d", ret);
        eca_client_command_dispatcher_stop();
        pthread_mutex_destroy(&g_net.publish_lock);
        g_net.lock_initialized = false;
        return -ret;
    }

    g_net.running = true;
    ret = pthread_create(&g_net.thread, &attr, eca_client_network_thread, NULL);
    if (attr_initialized) {
        pthread_attr_destroy(&attr);
    }
    if (ret != 0) {
        ECA_LOGE("network thread create failed ret=%d", ret);
        g_net.running = false;
        eca_client_command_dispatcher_stop();
        pthread_mutex_destroy(&g_net.publish_lock);
        g_net.lock_initialized = false;
        return -ret;
    }

    g_net.thread_started = true;
    ECA_LOGI("network started");
    return 0;
}

void eca_client_network_stop(void)
{
    if (!g_net.thread_started) {
        return;
    }

    g_net.running = false;
    pthread_join(g_net.thread, NULL);
    g_net.thread_started = false;

    eca_client_command_dispatcher_stop();
    if (g_net.lock_initialized) {
        pthread_mutex_destroy(&g_net.publish_lock);
        g_net.lock_initialized = false;
    }
}

void eca_client_network_notify_wifi_ready(void)
{
    int fd = -1;

    if (!g_net.thread_started || !g_net.lock_initialized) {
        return;
    }

    pthread_mutex_lock(&g_net.publish_lock);
    g_net.identity_refresh_requested = true;
    g_net.mqtt_defer_reason = ECA_CLIENT_MQTT_DEFER_NONE;
    if (g_net.sockfd >= 0) {
        fd = g_net.sockfd;
        g_net.sockfd = -1;
    }
    pthread_mutex_unlock(&g_net.publish_lock);

    if (fd >= 0) {
        close(fd);
    }
    ECA_LOGI("wifi ready notified, mqtt identity refresh scheduled");
}

int eca_client_network_get_registration_info(eca_client_registration_info_t *info)
{
    eca_client_net_config_t config;
    eca_client_net_config_t *source;
    char current_device_id[32];
    int ret;

    if (info == NULL) {
        return -EINVAL;
    }

    if (g_net.thread_started) {
        source = &g_net.config;
    } else {
        ret = eca_client_config_load(&config);
        if (ret < 0) {
            return ret;
        }
        source = &config;
    }

    ret = eca_client_format_wifi_mac(current_device_id,
                                     sizeof(current_device_id));
    if (ret < 0) {
        ECA_LOGW("wifi mac unavailable for registration ret=%d", ret);
        return ret;
    }

    memset(info, 0, sizeof(*info));
    snprintf(info->device_id, sizeof(info->device_id), "%s",
             current_device_id);
    snprintf(info->device_name, sizeof(info->device_name), "%s",
             source->device_name);
    snprintf(info->claim_token, sizeof(info->claim_token), "%s",
             source->claim_token);
    snprintf(info->http_base_url, sizeof(info->http_base_url), "%s",
             source->http_base_url);

    return 0;
}

int eca_client_network_get_registration_status(
    eca_client_registration_status_t *status)
{
    eca_client_net_config_t config;
    eca_client_net_config_t *source;
    char host[80];
    char base_path[80];
    char path[192];
    char request[ECA_CLIENT_HTTP_BODY_MAX + 512];
    char body[ECA_CLIENT_HTTP_BODY_MAX];
    cJSON *root;
    cJSON *item;
    int port;
    int fd;
    int http_status = 0;
    int ret;

    if (status == NULL) {
        return -EINVAL;
    }

    if (g_net.thread_started) {
        source = &g_net.config;
    } else {
        ret = eca_client_config_load(&config);
        if (ret < 0) {
            return ret;
        }
        source = &config;
    }

    if (!source->http_enabled || source->http_token[0] == '\0') {
        return -EACCES;
    }

    ret = eca_client_format_wifi_mac(source->device_id,
                                     sizeof(source->device_id));
    if (ret < 0) {
        ECA_LOGW("wifi mac unavailable for registration status ret=%d", ret);
        return ret;
    }

    ret = eca_client_http_parse_url(source->http_base_url, host, sizeof(host),
                                    &port, base_path, sizeof(base_path));
    if (ret < 0) {
        return ret;
    }

    snprintf(path, sizeof(path),
             "%s/api/v1/devices/%s/registration-status",
             base_path, source->device_id);
    snprintf(request, sizeof(request),
             "GET %s HTTP/1.0\r\n"
             "Host: %s:%d\r\n"
             "Authorization: Bearer %s\r\n"
             "Accept: application/json\r\n"
             "Connection: close\r\n"
             "\r\n",
             path, host, port, source->http_token);

    fd = eca_client_tcp_connect(host, port, 3);
    if (fd < 0) {
        return fd;
    }

    ret = eca_client_send_all(fd, (const uint8_t *)request, strlen(request));
    if (ret == 0) {
        ret = eca_client_http_read_response_body(fd, body, sizeof(body),
                                                 &http_status);
    } else {
        http_status = 0;
    }
    close(fd);
    if (ret < 0) {
        ECA_LOGD("registration status HTTP failed ret=%d status=%d",
                 ret, http_status);
        return ret;
    }

    root = cJSON_Parse(body);
    if (root == NULL) {
        return -EINVAL;
    }

    memset(status, 0, sizeof(*status));
    item = cJSON_GetObjectItemCaseSensitive(root, "device_id");
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        snprintf(status->device_id, sizeof(status->device_id), "%s",
                 item->valuestring);
    } else {
        snprintf(status->device_id, sizeof(status->device_id), "%s",
                 source->device_id);
    }

    item = cJSON_GetObjectItemCaseSensitive(root, "registered");
    if (cJSON_IsBool(item)) {
        status->registered = cJSON_IsTrue(item);
    }
    item = cJSON_GetObjectItemCaseSensitive(root, "bound");
    if (cJSON_IsBool(item)) {
        status->bound = cJSON_IsTrue(item);
    }
    item = cJSON_GetObjectItemCaseSensitive(root, "bound_to_current_user");
    if (cJSON_IsBool(item)) {
        status->bound_to_current_user = cJSON_IsTrue(item);
    }
    item = cJSON_GetObjectItemCaseSensitive(root, "available_for_claim");
    if (cJSON_IsBool(item)) {
        status->available_for_claim = cJSON_IsTrue(item);
    }
    item = cJSON_GetObjectItemCaseSensitive(root, "claim_status");
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        snprintf(status->claim_status, sizeof(status->claim_status), "%s",
                 item->valuestring);
    }

    cJSON_Delete(root);
    return 0;
}

/* ── MQTT request-response public API ───────────────────────────── */

int eca_client_mqtt_request(const char *query_type,
                            const char *extra_json,
                            int timeout_s,
                            eca_client_mqtt_response_t *response_out)
{
    eca_client_pending_request_t *slot = NULL;
    char request_id[ECA_CLIENT_REQUEST_ID_MAX];
    char topic[ECA_CLIENT_TOPIC_MAX];
    char payload[ECA_CLIENT_PACKET_MAX];
    unsigned int counter;
    long long now;
    long long deadline_ms;
    struct timespec ts;
    int ret;
    int i;

    if (query_type == NULL || response_out == NULL) {
        return -EINVAL;
    }

    if (timeout_s <= 0) {
        timeout_s = ECA_CLIENT_REQUEST_TIMEOUT_S;
    }

    pthread_mutex_lock(&g_request_lock);
    for (i = 0; i < ECA_CLIENT_PENDING_REQUESTS; i++) {
        if (!g_pending[i].active) {
            slot = &g_pending[i];
            break;
        }
    }
    if (slot == NULL) {
        pthread_mutex_unlock(&g_request_lock);
        ECA_LOGW("mqtt request slots full");
        return -EBUSY;
    }

    counter = ++g_request_counter;
    snprintf(request_id, sizeof(request_id), "req_%llu_%u",
             eca_client_now_ms(), counter);

    memset(slot, 0, sizeof(*slot));
    snprintf(slot->request_id, sizeof(slot->request_id), "%s", request_id);
    slot->active = true;
    slot->fulfilled = false;
    pthread_mutex_unlock(&g_request_lock);

    /* Build request payload */

    if (extra_json != NULL && extra_json[0] != '\0') {
        snprintf(payload, sizeof(payload),
                 "{\"request_id\":\"%s\",\"type\":\"%s\","
                 "\"device_id\":\"%s\",\"timestamp\":%lld,%s}",
                 request_id, query_type, g_net.config.device_id,
                 eca_client_now_ms(), extra_json);
    } else {
        snprintf(payload, sizeof(payload),
                 "{\"request_id\":\"%s\",\"type\":\"%s\","
                 "\"device_id\":\"%s\",\"timestamp\":%lld}",
                 request_id, query_type, g_net.config.device_id,
                 eca_client_now_ms());
    }

    /* Publish request */

    eca_client_build_topic(topic, sizeof(topic), g_net.config.device_id,
                           "request");

    pthread_mutex_lock(&g_net.publish_lock);
    if (g_net.sockfd < 0 || !g_net.config.mqtt_enabled) {
        pthread_mutex_unlock(&g_net.publish_lock);
        pthread_mutex_lock(&g_request_lock);
        slot->active = false;
        pthread_mutex_unlock(&g_request_lock);
        ECA_LOGW("mqtt request skipped: not connected");
        return -ENOTCONN;
    }
    ret = eca_client_mqtt_publish(g_net.sockfd, topic, payload);
    if (ret < 0) {
        close(g_net.sockfd);
        g_net.sockfd = -1;
        pthread_mutex_unlock(&g_net.publish_lock);
        pthread_mutex_lock(&g_request_lock);
        slot->active = false;
        pthread_mutex_unlock(&g_request_lock);
        ECA_LOGW("mqtt request publish failed ret=%d", ret);
        return ret;
    }
    pthread_mutex_unlock(&g_net.publish_lock);

    ECA_LOGI("mqtt request sent request_id=%s type=%s topic=%s",
             request_id, query_type, topic);

    /* Wait for response */

    now = eca_client_now_ms();
    deadline_ms = now + (long long)timeout_s * 1000;

    pthread_mutex_lock(&g_request_lock);
    while (!slot->fulfilled) {
        now = eca_client_now_ms();
        if (now >= deadline_ms) {
            slot->active = false;
            pthread_mutex_unlock(&g_request_lock);
            ECA_LOGW("mqtt request timeout request_id=%s type=%s",
                     request_id, query_type);
            return -ETIMEDOUT;
        }
        ts.tv_sec = (time_t)(deadline_ms / 1000);
        ts.tv_nsec = (long)((deadline_ms % 1000) * 1000000);
        ret = pthread_cond_timedwait(&g_request_cond, &g_request_lock, &ts);
        if (ret != 0 && ret != ETIMEDOUT) {
            slot->active = false;
            pthread_mutex_unlock(&g_request_lock);
            return -ret;
        }
    }

    /* Response arrived */

    *response_out = slot->response;
    slot->active = false;
    pthread_mutex_unlock(&g_request_lock);

    ECA_LOGI("mqtt response received request_id=%s status=%s",
             response_out->request_id, response_out->status);
    return 0;
}

int eca_client_mqtt_request_plans(eca_client_mqtt_response_t *resp,
                                  int timeout_s)
{
    return eca_client_mqtt_request("get_plans", NULL, timeout_s, resp);
}

int eca_client_mqtt_request_alerts(eca_client_mqtt_response_t *resp,
                                   int timeout_s)
{
    return eca_client_mqtt_request("get_alerts", NULL, timeout_s, resp);
}

int eca_client_mqtt_request_config(eca_client_mqtt_response_t *resp,
                                   int timeout_s)
{
    return eca_client_mqtt_request("get_config", NULL, timeout_s, resp);
}

int eca_client_mqtt_request_broadcast_templates(
    eca_client_mqtt_response_t *resp, int timeout_s)
{
    return eca_client_mqtt_request("get_broadcast_templates", NULL,
                                   timeout_s, resp);
}
