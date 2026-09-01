#include "rvt_wifi_autoconnect.h"
#include <nuttx/config.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netutils/cJSON.h>
#include <netutils/netlib.h>
#include <nuttx/wireless/wireless.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wireless/wapi.h>
#ifdef RVT_CONNECTIVITY_WITH_WIFI_DISPLAY
#include "rvt_wifi_display_api.h"
#endif
#define RVT_CONN_WIFI_IFNAME              "wlan0"
#define RVT_CONN_WAPI_CONFIG_PATH         "/data/etc/wifi/wapi.conf"
#define RVT_CONN_AP_IP                    "192.168.49.1"
#define RVT_CONN_RETRY_COUNT              3
#define RVT_CONN_DHCP_TRIES              3
#define RVT_CONN_DHCP_RETRY_DELAY_US      (2 * 1000 * 1000)
#define RVT_CONN_READY_WAIT_TRIES         10
#define RVT_CONN_READY_WAIT_US            (500 * 1000)
#define RVT_CONN_SCAN_WAIT_TRIES          25
#define RVT_CONN_SCAN_WAIT_US             (200 * 1000)
#define RVT_CONN_MODE_RESET_WAIT_US       (300 * 1000)
#define RVT_CONN_ASSOC_WAIT_US            (3 * 1000 * 1000)
#define RVT_CONN_LOG(...)                 dprintf(STDOUT_FILENO, __VA_ARGS__)
#ifndef CONFIG_RIVOTEK_CONNECTIVITY_SERVICE_PRIORITY
#define CONFIG_RIVOTEK_CONNECTIVITY_SERVICE_PRIORITY 100
#endif
#ifndef CONFIG_RIVOTEK_CONNECTIVITY_SERVICE_STACKSIZE
#define CONFIG_RIVOTEK_CONNECTIVITY_SERVICE_STACKSIZE 8192
#endif
#define RVT_CONN_DISPLAY_TASK_PRIORITY    CONFIG_RIVOTEK_CONNECTIVITY_SERVICE_PRIORITY
#define RVT_CONN_DISPLAY_TASK_STACK       CONFIG_RIVOTEK_CONNECTIVITY_SERVICE_STACKSIZE
#ifdef CONFIG_RIVOTEK_DEBUG_PRESET_WAPI_CONF
#define RVT_CONN_DEBUG_WAPI_SRC           "/resource/etc/wifi/wapi_debug.conf"
#define RVT_CONN_DEBUG_WAPI_DST_DIR       "/data/etc/wifi"
#endif
typedef struct rvt_conn_wifi_config {
    char ssid[33];
    char psk[64];
    char bssid[18];
} rvt_conn_wifi_config_t;
static const int g_retry_delay_seconds[RVT_CONN_RETRY_COUNT] = {1, 20, 40};
#ifdef RVT_CONNECTIVITY_WITH_WIFI_DISPLAY
static volatile int g_display_start_pending;
#endif
/*
 * 函数描述：对日志中的 WiFi 密码做脱敏显示。
 * 入参：
 *   password - 原始 WiFi 密码。
 *   out - 输出脱敏字符串缓冲区。
 *   out_len - 输出缓冲区长度。
 * 返回值：返回 out；参数非法时返回空字符串常量。
 */
static const char *conn_mask_password(const char *password, char *out,
                                      int out_len)
{
    int len;
    if (!out || out_len <= 0) {
        return "";
    }
    if (!password) {
        snprintf(out, out_len, "null");
        return out;
    }
    len = strlen(password);
    if (len <= 4) {
        snprintf(out, out_len, "****(len=%d)", len);
    } else {
        snprintf(out, out_len, "%c%c****%c%c(len=%d)",
                 password[0], password[1],
                 password[len - 2], password[len - 1], len);
    }
    return out;
}
/*
 * 函数描述：从 JSON 对象中读取指定字符串字段并拷贝到输出缓冲区。
 * 入参：
 *   root - JSON 对象。
 *   name - 字段名。
 *   out - 输出字符串缓冲区。
 *   out_len - 输出缓冲区长度。
 * 返回值：成功返回 0；参数非法、字段不存在或缓冲区不足返回负 errno。
 */
static int conn_copy_json_string(cJSON *root, const char *name,
                                 char *out, size_t out_len)
{
    cJSON *item;
    if (!root || !name || !out || out_len == 0) {
        return -EINVAL;
    }
    out[0] = '\0';
    item = cJSON_GetObjectItemCaseSensitive(root, name);
    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        return -ENOENT;
    }
    if (strlen(item->valuestring) >= out_len) {
        return -ENOSPC;
    }
    snprintf(out, out_len, "%s", item->valuestring);
    return 0;
}
/*
 * 函数描述：读取文本文件内容到缓冲区并补 '\0' 结尾。
 * 入参：
 *   path - 文件路径。
 *   buf - 输出缓冲区。
 *   buf_len - 输出缓冲区长度。
 * 返回值：成功返回 0；打开、读取失败或文件为空返回负 errno。
 */
static int conn_read_file(const char *path, char *buf, size_t buf_len)
{
    int fd;
    ssize_t nread;
    struct stat st;
    if (!path || !buf || buf_len == 0) {
        return -EINVAL;
    }
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        return -errno;
    }
    memset(buf, 0, buf_len);
    if (fstat(fd, &st) == 0 && st.st_size <= 0) {
        close(fd);
        return -ENODATA;
    }
    nread = read(fd, buf, buf_len - 1);
    close(fd);
    if (nread <= 0) {
        return nread == 0 ? -ENODATA : -errno;
    }
    buf[nread] = '\0';
    return 0;
}
#ifdef CONFIG_RIVOTEK_DEBUG_PRESET_WAPI_CONF
/*
 * 函数描述：把调试预置的 wapi_debug.conf 拷贝到运行时 wapi.conf。
 * 入参：无。
 * 返回值：完成复制返回 1；目标文件已存在返回 0；源文件不存在或复制失败返回负 errno。
 */
static int conn_copy_debug_preset_config(void)
{
    int src_fd = -1;
    int dst_fd = -1;
    char buf[256];
    ssize_t nread;
    if (access(RVT_CONN_WAPI_CONFIG_PATH, F_OK) == 0) {
        return 0;
    }
    src_fd = open(RVT_CONN_DEBUG_WAPI_SRC, O_RDONLY);
    if (src_fd < 0) {
        return -errno;
    }
    mkdir("/data/etc", 0777);
    mkdir(RVT_CONN_DEBUG_WAPI_DST_DIR, 0777);
    dst_fd = open(RVT_CONN_WAPI_CONFIG_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (dst_fd < 0) {
        close(src_fd);
        return -errno;
    }
    while ((nread = read(src_fd, buf, sizeof(buf))) > 0) {
        ssize_t nwritten = 0;
        while (nwritten < nread) {
            ssize_t ret = write(dst_fd, buf + nwritten, nread - nwritten);
            if (ret <= 0) {
                int err = ret < 0 ? errno : EIO;
                close(src_fd);
                close(dst_fd);
                unlink(RVT_CONN_WAPI_CONFIG_PATH);
                return -err;
            }
            nwritten += ret;
        }
    }
    if (nread < 0) {
        int err = errno;
        close(src_fd);
        close(dst_fd);
        unlink(RVT_CONN_WAPI_CONFIG_PATH);
        return -err;
    }
    close(src_fd);
    close(dst_fd);
    return 1;
}
#endif
/*
 * 函数描述：读取并校验 /data/etc/wifi/wapi.conf 中保存的 WiFi 配置。
 * 入参：
 *   config - 输出 WiFi 配置。
 * 返回值：成功返回 0；配置缺失、JSON 格式错误或 SSID/PSK 非法返回负 errno。
 */
static int conn_load_config(rvt_conn_wifi_config_t *config)
{
    char json[512];
    cJSON *root = NULL;
    cJSON *wlan = NULL;
    int ret;
    if (!config) {
        return -EINVAL;
    }
    memset(config, 0, sizeof(*config));
    ret = conn_read_file(RVT_CONN_WAPI_CONFIG_PATH, json, sizeof(json));
    if (ret < 0) {
        RVT_CONN_LOG("[wifi] read config failed path=%s ret=%d\n",
                     RVT_CONN_WAPI_CONFIG_PATH, ret);
        return ret;
    }
    root = cJSON_Parse(json);
    if (!root) {
        RVT_CONN_LOG("[wifi] parse config failed path=%s\n",
                     RVT_CONN_WAPI_CONFIG_PATH);
        return -EINVAL;
    }
    wlan = cJSON_GetObjectItemCaseSensitive(root, RVT_CONN_WIFI_IFNAME);
    if (!cJSON_IsObject(wlan)) {
        cJSON_Delete(root);
        return -EINVAL;
    }
    ret = conn_copy_json_string(wlan, "ssid", config->ssid,
                                sizeof(config->ssid));
    if (ret == 0) {
        ret = conn_copy_json_string(wlan, "psk", config->psk,
                                    sizeof(config->psk));
    }
    if (ret == 0) {
        conn_copy_json_string(wlan, "bssid", config->bssid,
                              sizeof(config->bssid));
    }
    cJSON_Delete(root);
    if (ret < 0) {
        return ret;
    }
    if (config->ssid[0] == '\0' || config->psk[0] == '\0') {
        return -EINVAL;
    }
    if (strlen(config->psk) < 8 || strlen(config->psk) > 63) {
        return -EINVAL;
    }
    return 0;
}
/*
 * 函数描述：检查字符串是否为合法 BSSID 格式。
 * 入参：
 *   bssid - BSSID 字符串，格式为 xx:xx:xx:xx:xx:xx。
 * 返回值：合法返回 1；为空或非法返回 0。
 */
static int conn_is_valid_bssid(const char *bssid)
{
    unsigned int mac[6];
    if (!bssid || bssid[0] == '\0') {
        return 0;
    }
    return sscanf(bssid, "%02x:%02x:%02x:%02x:%02x:%02x",
                  &mac[0], &mac[1], &mac[2],
                  &mac[3], &mac[4], &mac[5]) == 6;
}
/*
 * 函数描述：读取 wlan0 当前 IPv4 地址。
 * 入参：
 *   addr - 输出 IPv4 地址。
 * 返回值：成功且地址非 0 返回 0；读取失败或地址为空返回负 errno。
 */
static int conn_get_ip(struct in_addr *addr)
{
    memset(addr, 0, sizeof(*addr));
    if (netlib_get_ipv4addr(RVT_CONN_WIFI_IFNAME, addr) < 0) {
        return -errno;
    }
    return addr->s_addr != 0 ? 0 : -ENETDOWN;
}
/*
 * 函数描述：读取 wlan0 当前默认网关地址。
 * 入参：
 *   addr - 输出 IPv4 网关地址。
 * 返回值：成功且地址非 0 返回 0；读取失败或地址为空返回负 errno。
 */
static int conn_get_gateway(struct in_addr *addr)
{
    memset(addr, 0, sizeof(*addr));
    if (netlib_get_dripv4addr(RVT_CONN_WIFI_IFNAME, addr) < 0) {
        return -errno;
    }
    return addr->s_addr != 0 ? 0 : -ENETDOWN;
}
/*
 * 函数描述：清理 wlan0 上一次 AP/STA 残留的 IP、掩码和网关。
 * 入参：无。
 * 返回值：成功返回 0；任一 netlib 设置失败返回首个负 errno。
 */
static int conn_clear_ip_state(void)
{
    struct in_addr zero;
    int ret = 0;
    memset(&zero, 0, sizeof(zero));
    if (netlib_set_ipv4addr(RVT_CONN_WIFI_IFNAME, &zero) < 0) {
        ret = -errno;
    }
    if (netlib_set_ipv4netmask(RVT_CONN_WIFI_IFNAME, &zero) < 0 &&
        ret == 0) {
        ret = -errno;
    }
    if (netlib_set_dripv4addr(RVT_CONN_WIFI_IFNAME, &zero) < 0 && ret == 0) {
        ret = -errno;
    }
    return ret;
}
/*
 * 函数描述：如果投屏服务正在运行，则在 WiFi 重连前主动停止投屏。
 * 入参：无。
 * 返回值：无。
 */
static void conn_stop_display_if_running(void)
{
#ifdef RVT_CONNECTIVITY_WITH_WIFI_DISPLAY
    if (rvt_wifi_display_is_running()) {
        RVT_CONN_LOG("[wifi] stop wifi display before reconnect\n");
        rvt_wifi_display_stop();
    }
#endif
}

/*
 * 函数描述：创建 WAPI 操作用 socket。
 * 入参：无。
 * 返回值：成功返回 socket fd；失败返回负 errno。
 */
static int conn_wifi_socket(void)
{
    int sock = wapi_make_socket();

    if (sock < 0) {
        RVT_CONN_LOG("[wifi] wapi socket failed ret=%d\n", sock);
    }
    return sock;
}

/*
 * 函数描述：通过 WAPI 清理当前 STA 连接状态。
 * 入参：无。
 * 返回值：无。
 */
static void conn_disconnect_wapi(void)
{
    int sock = conn_wifi_socket();

    if (sock < 0) {
        return;
    }
    wpa_driver_wext_disconnect(sock, RVT_CONN_WIFI_IFNAME);
    close(sock);
}

/*
 * 函数描述：通过 WAPI 将 wlan0 拉起并切换到 STA 管理模式。
 * 入参：无。
 * 返回值：成功返回 0；创建 socket、ifup 或设置模式失败返回负 errno。
 */
static int conn_prepare_sta_wapi(void)
{
    int sock;
    int ret;

    conn_stop_display_if_running();

    sock = conn_wifi_socket();
    if (sock < 0) {
        return sock;
    }

    wpa_driver_wext_disconnect(sock, RVT_CONN_WIFI_IFNAME);

    ret = wapi_set_ifdown(sock, RVT_CONN_WIFI_IFNAME);
    if (ret < 0) {
        RVT_CONN_LOG("[wifi] ifdown %s failed ret=%d\n",
                     RVT_CONN_WIFI_IFNAME, ret);
        close(sock);
        return ret;
    }
    usleep(RVT_CONN_MODE_RESET_WAIT_US);

    ret = wapi_set_ifup(sock, RVT_CONN_WIFI_IFNAME);
    if (ret < 0) {
        RVT_CONN_LOG("[wifi] ifup %s failed ret=%d\n",
                     RVT_CONN_WIFI_IFNAME, ret);
        close(sock);
        return ret;
    }

    ret = wapi_set_mode(sock, RVT_CONN_WIFI_IFNAME, WAPI_MODE_MANAGED);
    if (ret < 0) {
        RVT_CONN_LOG("[wifi] set managed mode failed ret=%d\n", ret);
        close(sock);
        return ret;
    }

    close(sock);
    conn_clear_ip_state();
    return 0;
}

/*
 * 函数描述：通过 WAPI 触发一次指定 SSID 扫描并判断目标热点是否存在。
 * 入参：
 *   ssid - 目标 SSID。
 *   attempt - 当前扫描序号，从 0 开始，仅用于日志。
 * 返回值：扫描到目标 SSID 返回 0；扫描失败、超时或未扫描到返回负 errno。
 */
static int conn_scan_saved_ssid_wapi(const char *ssid, int attempt)
{
    struct wapi_list_s list;
    struct wapi_scan_info_s *info;
    int sock;
    int i;
    int ret;

    if (!ssid || ssid[0] == '\0') {
        return -EINVAL;
    }

    sock = conn_wifi_socket();
    if (sock < 0) {
        return sock;
    }

    RVT_CONN_LOG("[wifi] scanning saved ssid, attempt %d/%d\n",
                 attempt + 1, RVT_CONN_RETRY_COUNT);
    ret = wapi_escan_init(sock, RVT_CONN_WIFI_IFNAME,
                          IW_SCAN_TYPE_ACTIVE, ssid);
    if (ret < 0) {
        RVT_CONN_LOG("[wifi] scan start failed ret=%d ssid=%s\n", ret, ssid);
        close(sock);
        return ret;
    }

    for (i = 0; i < RVT_CONN_SCAN_WAIT_TRIES; i++) {
        ret = wapi_scan_stat(sock, RVT_CONN_WIFI_IFNAME);
        if (ret == 0) {
            break;
        }
        if (ret < 0) {
            RVT_CONN_LOG("[wifi] scan status failed ret=%d ssid=%s\n",
                         ret, ssid);
            close(sock);
            return ret;
        }
        usleep(RVT_CONN_SCAN_WAIT_US);
    }

    if (ret != 0) {
        RVT_CONN_LOG("[wifi] scan wait timeout ssid=%s\n", ssid);
        close(sock);
        return -ETIMEDOUT;
    }

    memset(&list, 0, sizeof(list));
    ret = wapi_scan_coll(sock, RVT_CONN_WIFI_IFNAME, &list);
    close(sock);
    if (ret < 0) {
        RVT_CONN_LOG("[wifi] collect scan result failed ret=%d ssid=%s\n",
                     ret, ssid);
        return ret;
    }

    for (info = list.head.scan; info; info = info->next) {
        if (info->has_essid && strcmp(info->essid, ssid) == 0) {
            if (info->has_rssi) {
                RVT_CONN_LOG("[wifi] saved ssid found ssid=%s rssi=%d\n",
                             ssid, info->rssi);
            } else {
                RVT_CONN_LOG("[wifi] saved ssid found ssid=%s\n", ssid);
            }
            wapi_scan_coll_free(&list);
            return 0;
        }
    }

    wapi_scan_coll_free(&list);
    RVT_CONN_LOG("[wifi] saved ssid not found ssid=%s\n", ssid);
    return -ENOENT;
}

/*
 * 函数描述：通过 WAPI/WEXT 使用保存的 SSID/PSK/BSSID 发起 STA 连接。
 * 入参：
 *   config - 已校验的 WiFi 配置。
 * 返回值：配置并触发连接成功返回 0；WAPI/WEXT 调用失败返回负 errno。
 */
static int conn_connect_saved_wifi_wapi(const rvt_conn_wifi_config_t *config)
{
    struct ether_addr ap;
    int sock;
    int ret;

    if (!config) {
        return -EINVAL;
    }

    sock = conn_wifi_socket();
    if (sock < 0) {
        return sock;
    }

    ret = wpa_driver_wext_set_auth_param(sock, RVT_CONN_WIFI_IFNAME,
                                         IW_AUTH_WPA_VERSION,
                                         IW_AUTH_WPA_VERSION_WPA2);
    if (ret < 0) {
        RVT_CONN_LOG("[wifi] set WPA2 failed ret=%d\n", ret);
        close(sock);
        return ret;
    }

    ret = wpa_driver_wext_set_auth_param(sock, RVT_CONN_WIFI_IFNAME,
                                         IW_AUTH_CIPHER_PAIRWISE,
                                         IW_AUTH_CIPHER_CCMP);
    if (ret < 0) {
        RVT_CONN_LOG("[wifi] set CCMP failed ret=%d\n", ret);
        close(sock);
        return ret;
    }

    ret = wpa_driver_wext_set_key_ext(sock, RVT_CONN_WIFI_IFNAME,
                                      WPA_ALG_CCMP, config->psk,
                                      strlen(config->psk));
    if (ret < 0) {
        RVT_CONN_LOG("[wifi] set psk failed ret=%d ssid=%s\n",
                     ret, config->ssid);
        close(sock);
        return ret;
    }

    if (conn_is_valid_bssid(config->bssid)) {
        memset(&ap, 0, sizeof(ap));
        sscanf(config->bssid, "%2hhx:%2hhx:%2hhx:%2hhx:%2hhx:%2hhx",
               &ap.ether_addr_octet[0], &ap.ether_addr_octet[1],
               &ap.ether_addr_octet[2], &ap.ether_addr_octet[3],
               &ap.ether_addr_octet[4], &ap.ether_addr_octet[5]);
        RVT_CONN_LOG("[wifi] connecting ssid=%s bssid=%s\n",
                     config->ssid, config->bssid);
        ret = wapi_set_essid(sock, RVT_CONN_WIFI_IFNAME, config->ssid,
                             WAPI_ESSID_DELAY_ON);
        if (ret == 0) {
            ret = wapi_set_ap(sock, RVT_CONN_WIFI_IFNAME, &ap);
        }
    } else {
        RVT_CONN_LOG("[wifi] connecting ssid=%s\n", config->ssid);
        ret = wapi_set_essid(sock, RVT_CONN_WIFI_IFNAME, config->ssid,
                             WAPI_ESSID_ON);
    }

    close(sock);
    if (ret < 0) {
        RVT_CONN_LOG("[wifi] connect trigger failed ret=%d ssid=%s\n",
                     ret, config->ssid);
        return ret;
    }

    usleep(RVT_CONN_ASSOC_WAIT_US);
    return 0;
}
/*
 * 函数描述：为 wlan0 触发 DHCP 获取地址。
 * 入参：无。
 * 返回值：成功返回 0；多次 DHCP 失败返回负 errno。
 */
static int conn_obtain_ip(void)
{
    int i;
    int ret = -ETIMEDOUT;
    for (i = 0; i < RVT_CONN_DHCP_TRIES; i++) {
        RVT_CONN_LOG("[wifi] DHCP renew try %d\n", i + 1);
        ret = netlib_obtain_ipv4addr(RVT_CONN_WIFI_IFNAME);
        if (ret == 0) {
            return 0;
        }
        RVT_CONN_LOG("[wifi] DHCP renew failed try=%d ret=%d\n", i + 1, ret);
        if (i + 1 < RVT_CONN_DHCP_TRIES) {
            usleep(RVT_CONN_DHCP_RETRY_DELAY_US);
        }
    }
    return ret < 0 ? ret : -ETIMEDOUT;
}
/*
 * 函数描述：判断 wlan0 是否具备 STA 业务可用的 IP 和网关。
 * 入参：无。
 * 返回值：就绪返回 1；未连接、无地址、无网关或仍是 SoftAP 地址返回 0。
 */
int rvt_wifi_autoconnect_is_ready(void)
{
    struct in_addr local;
    struct in_addr router;
    struct in_addr ap_addr;
    memset(&ap_addr, 0, sizeof(ap_addr));
    inet_aton(RVT_CONN_AP_IP, &ap_addr);
    if (conn_get_ip(&local) < 0 || local.s_addr == 0 ||
        local.s_addr == ap_addr.s_addr) {
        return 0;
    }
    if (conn_get_gateway(&router) < 0 || router.s_addr == 0) {
        return 0;
    }
    return 1;
}
/*
 * 函数描述：短时间轮询等待 wlan0 进入 STA 业务就绪状态。
 * 入参：无。
 * 返回值：就绪返回 0；等待超时返回 -ETIMEDOUT。
 */
static int conn_wait_ready(void)
{
    int i;
    for (i = 0; i < RVT_CONN_READY_WAIT_TRIES; i++) {
        if (rvt_wifi_autoconnect_is_ready()) {
            return 0;
        }
        usleep(RVT_CONN_READY_WAIT_US);
    }
    return -ETIMEDOUT;
}
/*
 * 函数描述：WiFi 就绪后按需启动投屏服务。
 * 入参：无。
 * 返回值：投屏启动结果；投屏已运行时返回 0。
 */
#ifdef RVT_CONNECTIVITY_WITH_WIFI_DISPLAY
static int conn_start_display_task(int argc, char *argv[])
{
    int ret;

    (void)argc;
    (void)argv;

    ret = 0;
    if (!rvt_wifi_display_is_running()) {
        ret = rvt_wifi_display_start();
        RVT_CONN_LOG("[wifi] start wifi display ret=%d\n", ret);
    } else {
        RVT_CONN_LOG("[wifi] wifi display already running\n");
    }

    g_display_start_pending = 0;
    _exit(ret < 0 ? 1 : 0);
    return ret;
}

/*
 * 函数描述：WiFi 就绪后异步触发投屏服务，避免阻塞配网成功状态上报。
 * 入参：无。
 * 返回值：成功提交启动任务或投屏已运行返回 0；创建任务失败返回负 errno。
 */
static int conn_start_display_if_ready(void)
{
    int pid;

    if (rvt_wifi_display_is_running()) {
        RVT_CONN_LOG("[wifi] wifi display already running\n");
        return 0;
    }

    if (g_display_start_pending) {
        RVT_CONN_LOG("[wifi] wifi display start already pending\n");
        return 0;
    }

    g_display_start_pending = 1;
    pid = task_create("rvtdisp_boot",
                      RVT_CONN_DISPLAY_TASK_PRIORITY,
                      RVT_CONN_DISPLAY_TASK_STACK,
                      conn_start_display_task,
                      NULL);
    if (pid < 0) {
        g_display_start_pending = 0;
        RVT_CONN_LOG("[wifi] start wifi display task failed pid=%d\n", pid);
        return pid;
    }

    RVT_CONN_LOG("[wifi] start wifi display task pid=%d\n", pid);
    return 0;
}
#else
/*
 * 函数描述：未启用投屏服务时的空实现。
 * 入参：无。
 * 返回值：固定返回 0。
 */
static int conn_start_display_if_ready(void)
{
    return 0;
}
#endif
/*
 * 函数描述：将 IPv4 地址转换成字符串并写入指定缓冲区。
 * 入参：
 *   addr - IPv4 地址。
 *   buf - 输出字符串缓冲区。
 *   len - 输出缓冲区长度。
 * 返回值：返回 buf；参数非法时返回空字符串常量。
 */
static const char *conn_addr_to_str(struct in_addr addr, char *buf, size_t len)
{
    const char *ip;
    if (!buf || len == 0) {
        return "";
    }
    ip = inet_ntoa(addr);
    if (!ip) {
        snprintf(buf, len, "0.0.0.0");
    } else {
        snprintf(buf, len, "%s", ip);
    }
    return buf;
}
/*
 * 函数描述：执行保存 WiFi 配置的 STA 回连流程。
 * 入参：无。
 * 返回值：成功返回 0；配置缺失、扫描不到目标、连接或 DHCP 失败返回负 errno。
 */
int rvt_wifi_autoconnect_start(void)
{
    rvt_conn_wifi_config_t config;
    char masked[32];
    struct in_addr local;
    struct in_addr router;
    char local_ip[16];
    char router_ip[16];
    int attempt;
    int ret;
    RVT_CONN_LOG("[wifi] start_wifi\n");
#ifdef CONFIG_RIVOTEK_DEBUG_PRESET_WAPI_CONF
    ret = conn_copy_debug_preset_config();
    if (ret > 0) {
        RVT_CONN_LOG("[wifi] debug preset copied\n");
    } else if (ret < 0) {
        RVT_CONN_LOG("[wifi] debug preset copy skipped, ret=%d\n", ret);
    }
#endif
    ret = conn_load_config(&config);
    if (ret < 0) {
        RVT_CONN_LOG("[wifi] no valid saved wifi config, ret=%d\n", ret);
        return ret;
    }
    RVT_CONN_LOG("[wifi] saved config ssid=%s bssid=%s psk=%s\n",
                 config.ssid,
                 config.bssid[0] ? config.bssid : "-",
                 conn_mask_password(config.psk, masked, sizeof(masked)));
    ret = conn_prepare_sta_wapi();
    if (ret < 0) {
        RVT_CONN_LOG("[wifi] prepare sta failed ret=%d\n", ret);
        return ret;
    }
    for (attempt = 0; attempt < RVT_CONN_RETRY_COUNT; attempt++) {
        if (g_retry_delay_seconds[attempt] > 0) {
            if (attempt == 0) {
                RVT_CONN_LOG("[wifi] wait %ds before first scan\n",
                             g_retry_delay_seconds[attempt]);
            } else {
                RVT_CONN_LOG("[wifi] saved ssid not found or connect failed, retry in %ds\n",
                             g_retry_delay_seconds[attempt]);
            }
            sleep(g_retry_delay_seconds[attempt]);
        }
        ret = conn_scan_saved_ssid_wapi(config.ssid, attempt);
        if (ret < 0) {
            if (attempt + 1 >= RVT_CONN_RETRY_COUNT) {
                RVT_CONN_LOG("[wifi] saved ssid not found after retries, ret=%d\n",
                             ret);
                return ret;
            }
            continue;
        }

        ret = conn_connect_saved_wifi_wapi(&config);
        if (ret < 0) {
            if (attempt + 1 >= RVT_CONN_RETRY_COUNT) {
                RVT_CONN_LOG("[wifi] reconnect failed after retries, ret=%d\n",
                             ret);
                return ret;
            }
            continue;
        }
        ret = conn_obtain_ip();
        if (ret < 0) {
            RVT_CONN_LOG("[wifi] DHCP failed ret=%d\n", ret);
            if (attempt + 1 >= RVT_CONN_RETRY_COUNT) {
                return ret;
            }
            conn_disconnect_wapi();
            conn_clear_ip_state();
            continue;
        }
        ret = conn_wait_ready();
        if (ret < 0) {
            RVT_CONN_LOG("[wifi] wifi ready wait failed ret=%d\n", ret);
            if (attempt + 1 >= RVT_CONN_RETRY_COUNT) {
                return ret;
            }
            conn_disconnect_wapi();
            conn_clear_ip_state();
            continue;
        }
        memset(&local, 0, sizeof(local));
        memset(&router, 0, sizeof(router));
        conn_get_ip(&local);
        conn_get_gateway(&router);
        RVT_CONN_LOG("[wifi] wifi ready ip=%s gw=%s\n",
                     conn_addr_to_str(local, local_ip, sizeof(local_ip)),
                     conn_addr_to_str(router, router_ip, sizeof(router_ip)));
        conn_start_display_if_ready();
        RVT_CONN_LOG("[wifi] wifi startup done\n");
        return 0;
    }
    return -ETIMEDOUT;
}
