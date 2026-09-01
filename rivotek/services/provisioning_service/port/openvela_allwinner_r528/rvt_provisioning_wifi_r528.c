#include "rvt_provisioning_wifi_port.h"

#include <nuttx/config.h>

#ifdef CONFIG_RIVOTEK_CONNECTIVITY_SERVICE
#include "rvt_wifi_autoconnect.h"
#endif

#include <arpa/inet.h>
#include <errno.h>
#include <net/if.h>
#include <netutils/dhcpd.h>
#include <netutils/netlib.h>
#include <nuttx/wireless/wireless.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <wireless/wapi.h>

#define RVT_PROV_WIFI_IFNAME             "wlan0"
#ifndef CONFIG_RIVOTEK_CONNECTIVITY_SERVICE
#define RVT_PROV_DHCP_WAIT_US            (500 * 1000)
#define RVT_PROV_DHCP_TRIES              20
#endif
#define RVT_PROV_AP_NETMASK              "255.255.255.0"
#define RVT_PROV_AP_DHCP_START           "192.168.49.2"
#define RVT_PROV_AP_CHANNEL              6
#define RVT_PROV_MODE_RESET_WAIT_US      (300 * 1000)
#define RVT_PROV_STA_MODE_WAIT_US        (300 * 1000)
#define RVT_PROV_SOFTAP_CLIENT_LEAVE_US  (1500 * 1000)
#define RVT_PROV_SOFTAP_STOP_SETTLE_US   (500 * 1000)
#define RVT_PROV_SCAN_WAIT_TRIES         25
#define RVT_PROV_SCAN_WAIT_US            (200 * 1000)
#define RVT_PROV_ASSOC_WAIT_US           (3 * 1000 * 1000)
#define PROV_LOG(...) dprintf(STDOUT_FILENO, __VA_ARGS__)

/* 本文件只保存本适配层启动的 SoftAP 状态，不代表系统 WiFi 全局状态。 */
static int g_softap_started;

/*
 * 函数描述：对日志中的 WiFi 密码做脱敏显示。
 * 入参：
 *   password - 原始 WiFi 密码。
 *   out - 输出脱敏字符串。
 *   out_len - 输出缓冲区长度。
 * 返回值：返回 out；参数非法时返回空字符串常量。
 */
static const char *wifi_mask_password(const char *password, char *out,
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
                 password[0],
                 password[1],
                 password[len - 2],
                 password[len - 1],
                 len);
    }

    return out;
}

/*
 * 函数描述：创建 WAPI 操作用 socket。
 * 入参：无。
 * 返回值：成功返回 socket fd；失败返回负值。
 */
static int wifi_socket(void)
{
    int sock = wapi_make_socket();

    if (sock < 0) {
        PROV_LOG("provisioning: wapi socket failed, ret=%d\n", sock);
    }

    return sock;
}

/*
 * 函数描述：将配网使用的 wlan0 拉起。
 * 入参：
 *   sock - WAPI socket。
 * 返回值：成功返回 0；失败返回负值。
 */
static int wifi_ifup(int sock)
{
    int ret = wapi_set_ifup(sock, RVT_PROV_WIFI_IFNAME);

    if (ret < 0) {
        PROV_LOG("provisioning: ifup %s failed, ret=%d\n",
                 RVT_PROV_WIFI_IFNAME, ret);
    }

    return ret;
}

/*
 * 函数描述：将配网使用的 wlan0 关闭，用于释放 Realtek 当前 AP/STA 模式。
 * 入参：
 *   sock - WAPI socket。
 * 返回值：成功返回 0；失败返回负值。
 */
static int wifi_ifdown(int sock)
{
    int ret = wapi_set_ifdown(sock, RVT_PROV_WIFI_IFNAME);

    if (ret < 0) {
        PROV_LOG("provisioning: ifdown %s failed, ret=%d\n",
                 RVT_PROV_WIFI_IFNAME, ret);
    }

    return ret;
}

/*
 * 函数描述：通过 WAPI/WEXT 清理 wlan0 当前关联状态。
 * 入参：
 *   sock - WAPI socket。
 * 返回值：无。
 */
static void prov_wapi_disconnect(int sock)
{
    if (sock >= 0) {
        wpa_driver_wext_disconnect(sock, RVT_PROV_WIFI_IFNAME);
    }
}

/*
 * 函数描述：释放当前 WiFi 模式后，通过 WAPI 将 wlan0 切到目标模式。
 * 入参：
 *   mode - 目标 WAPI 模式。
 * 返回值：成功返回 socket fd，调用者负责 close；失败返回负值。
 */
static int wifi_prepare_mode(enum wapi_mode_e mode)
{
    int sock;
    int ret;
    const char *mode_name = mode == WAPI_MODE_MASTER ? "AP" : "STA";

    sock = wifi_socket();
    if (sock < 0) {
        return sock;
    }

    prov_wapi_disconnect(sock);

    ret = wifi_ifdown(sock);
    if (ret < 0) {
        close(sock);
        return ret;
    }
    usleep(RVT_PROV_MODE_RESET_WAIT_US);

    ret = wifi_ifup(sock);
    if (ret < 0) {
        close(sock);
        return ret;
    }

    ret = wapi_set_mode(sock, RVT_PROV_WIFI_IFNAME, mode);
    if (ret < 0) {
        PROV_LOG("provisioning: set %s mode failed, ret=%d\n",
                 mode_name, ret);
        close(sock);
        return ret;
    }

    return sock;
}

/*
 * 函数描述：通过 WAPI 将 wlan0 设置为 STA 管理模式。
 * 入参：无。
 * 返回值：成功返回 0；创建 socket、ifup 或设置模式失败返回负值。
 */
static int wifi_prepare_sta_mode(void)
{
    int sock = wifi_prepare_mode(WAPI_MODE_MANAGED);

    if (sock < 0) {
        return sock;
    }

    close(sock);
    usleep(RVT_PROV_STA_MODE_WAIT_US);
    return 0;
}

/*
 * 函数描述：通过 WAPI 将 wlan0 拉起并切换到 SoftAP 模式。
 * 入参：无。
 * 返回值：成功返回 socket fd，调用者负责 close；失败返回负值。
 */
static int wifi_prepare_ap_mode(void)
{
    return wifi_prepare_mode(WAPI_MODE_MASTER);
}

/*
 * 函数描述：通过 WAPI/WEXT 设置 WPA2-PSK/CCMP 密码。
 * 入参：
 *   sock - WAPI socket。
 *   password - WPA2-PSK 密码。
 * 返回值：成功返回 0；设置认证、加密或密钥失败返回负值。
 */
static int wifi_set_wpa2_psk(int sock, const char *password)
{
    int ret;

    ret = wpa_driver_wext_set_auth_param(sock, RVT_PROV_WIFI_IFNAME,
                                         IW_AUTH_WPA_VERSION,
                                         IW_AUTH_WPA_VERSION_WPA2);
    if (ret < 0) {
        return ret;
    }

    ret = wpa_driver_wext_set_auth_param(sock, RVT_PROV_WIFI_IFNAME,
                                         IW_AUTH_CIPHER_PAIRWISE,
                                         IW_AUTH_CIPHER_CCMP);
    if (ret < 0) {
        return ret;
    }

    return wpa_driver_wext_set_key_ext(sock, RVT_PROV_WIFI_IFNAME,
                                       WPA_ALG_CCMP, password,
                                       strlen(password));
}

/*
 * 函数描述：尽力把 wlan0 锁定到指定信道。
 * 入参：
 *   sock - WAPI socket。
 *   channel - 目标 WiFi 信道。
 * 返回值：成功返回 0；驱动不支持或设置失败返回负值，调用者可选择忽略。
 */
static int wifi_try_set_channel(int sock, int channel)
{
    double freq = 0;
    int ret;

    ret = wapi_chan2freq(sock, RVT_PROV_WIFI_IFNAME, channel, &freq);
    if (ret < 0) {
        PROV_LOG("provisioning: channel %d lookup skipped, ret=%d\n",
                 channel, ret);
        return ret;
    }

    ret = wapi_set_freq(sock, RVT_PROV_WIFI_IFNAME, freq, WAPI_FREQ_FIXED);
    if (ret < 0) {
        PROV_LOG("provisioning: set channel %d failed, ret=%d\n",
                 channel, ret);
    }

    return ret;
}

/*
 * 函数描述：读取 wlan0 当前 IPv4 地址。
 * 入参：
 *   addr - 输出 IPv4 地址。
 * 返回值：成功且地址非 0 返回 0；读取失败或未获取地址返回负 errno。
 */
static int wifi_get_ip(struct in_addr *addr)
{
    memset(addr, 0, sizeof(*addr));
    if (netlib_get_ipv4addr(RVT_PROV_WIFI_IFNAME, addr) < 0) {
        return -errno;
    }

    return addr->s_addr != 0 ? 0 : -ENETDOWN;
}

/*
 * 函数描述：读取车机 SoftAP 启动后的本机 IPv4 地址，用于二维码 payload。
 * 入参：
 *   ip_buf - 输出 IPv4 字符串缓冲区。
 *   buf_len - 输出缓冲区长度。
 * 返回值：成功返回 0；接口未就绪或参数非法返回负 errno。
 */
int rvt_prov_wifi_get_softap_ip(char *ip_buf, int buf_len)
{
    struct in_addr addr;
    const char *ip;

    if (!ip_buf || buf_len <= 0) {
        return -EINVAL;
    }

    if (wifi_get_ip(&addr) < 0) {
        return -ENETDOWN;
    }

    ip = inet_ntoa(addr);
    if (!ip || ip[0] == '\0') {
        return -ENETDOWN;
    }

    snprintf(ip_buf, buf_len, "%s", ip);
    return 0;
}

#ifndef CONFIG_RIVOTEK_CONNECTIVITY_SERVICE
/*
 * 函数描述：启动 DHCP 客户端并等待 wlan0 获得非 SoftAP 地址。
 * 入参：无。
 * 返回值：成功返回 0；DHCP 超时或读取地址失败返回负 errno。
 */
static int wifi_obtain_ip(void)
{
    struct in_addr addr;
    struct in_addr ap_addr;
    int i;
    int ret;

    memset(&ap_addr, 0, sizeof(ap_addr));
    inet_aton(RVT_PROV_AP_IP_FALLBACK, &ap_addr);

    netlib_obtain_ipv4addr(RVT_PROV_WIFI_IFNAME);
    for (i = 0; i < RVT_PROV_DHCP_TRIES; i++) {
        ret = wifi_get_ip(&addr);
        if (ret == 0 && addr.s_addr != ap_addr.s_addr) {
            PROV_LOG("provisioning: %s ip=%s\n",
                     RVT_PROV_WIFI_IFNAME, inet_ntoa(addr));
            return 0;
        } else if (ret == 0) {
            PROV_LOG("provisioning: ignore stale AP ip=%s\n",
                     inet_ntoa(addr));
        }
        usleep(RVT_PROV_DHCP_WAIT_US);
    }

    return -ETIMEDOUT;
}

/*
 * 函数描述：通过 WAPI 主动扫描一次目标 SSID，确认目标热点当前可见。
 * 入参：
 *   ssid - 目标 WiFi SSID。
 * 返回值：扫描到目标 SSID 返回 0；扫描失败、超时或未发现返回负 errno。
 */
static int prov_wapi_scan_target_ssid(const char *ssid)
{
    struct wapi_list_s list;
    struct wapi_scan_info_s *info;
    int sock;
    int i;
    int ret;

    if (!ssid || ssid[0] == '\0') {
        return -EINVAL;
    }

    sock = wifi_socket();
    if (sock < 0) {
        return sock;
    }

    PROV_LOG("provisioning: scan target ssid=%s\n", ssid);
    ret = wapi_escan_init(sock, RVT_PROV_WIFI_IFNAME,
                          IW_SCAN_TYPE_ACTIVE, ssid);
    if (ret < 0) {
        PROV_LOG("provisioning: scan start failed, ret=%d ssid=%s\n",
                 ret, ssid);
        close(sock);
        return ret;
    }

    for (i = 0; i < RVT_PROV_SCAN_WAIT_TRIES; i++) {
        ret = wapi_scan_stat(sock, RVT_PROV_WIFI_IFNAME);
        if (ret == 0) {
            break;
        }
        if (ret < 0) {
            PROV_LOG("provisioning: scan status failed, ret=%d ssid=%s\n",
                     ret, ssid);
            close(sock);
            return ret;
        }
        usleep(RVT_PROV_SCAN_WAIT_US);
    }

    if (ret != 0) {
        PROV_LOG("provisioning: scan wait timeout ssid=%s\n", ssid);
        close(sock);
        return -ETIMEDOUT;
    }

    memset(&list, 0, sizeof(list));
    ret = wapi_scan_coll(sock, RVT_PROV_WIFI_IFNAME, &list);
    close(sock);
    if (ret < 0) {
        PROV_LOG("provisioning: scan collect failed, ret=%d ssid=%s\n",
                 ret, ssid);
        return ret;
    }

    for (info = list.head.scan; info; info = info->next) {
        if (info->has_essid && strcmp(info->essid, ssid) == 0) {
            if (info->has_rssi) {
                PROV_LOG("provisioning: target ssid found ssid=%s rssi=%d\n",
                         ssid, info->rssi);
            } else {
                PROV_LOG("provisioning: target ssid found ssid=%s\n", ssid);
            }
            wapi_scan_coll_free(&list);
            return 0;
        }
    }

    wapi_scan_coll_free(&list);
    PROV_LOG("provisioning: target ssid not found ssid=%s\n", ssid);
    return -ENOENT;
}

/*
 * 函数描述：通过 WAPI/WEXT 触发 STA 连接目标热点。
 * 入参：
 *   ssid - 目标 WiFi SSID。
 *   password - WPA2-PSK 密码。
 * 返回值：连接触发成功返回 0；WAPI/WEXT 调用失败返回负值。
 */
static int prov_wapi_connect_sta(const char *ssid, const char *password)
{
    int sock;
    int ret;

    sock = wifi_socket();
    if (sock < 0) {
        return sock;
    }

    ret = wifi_set_wpa2_psk(sock, password);
    if (ret < 0) {
        PROV_LOG("provisioning: set STA psk failed, ret=%d ssid=%s\n",
                 ret, ssid);
        close(sock);
        return ret;
    }

    PROV_LOG("provisioning: connect STA ssid=%s\n", ssid);
    ret = wapi_set_essid(sock, RVT_PROV_WIFI_IFNAME, ssid, WAPI_ESSID_ON);
    close(sock);
    if (ret < 0) {
        PROV_LOG("provisioning: set STA essid failed, ret=%d ssid=%s\n",
                 ret, ssid);
        return ret;
    }

    usleep(RVT_PROV_ASSOC_WAIT_US);
    return 0;
}
#endif

/*
 * 函数描述：配置车机 SoftAP 的静态 IP 和子网掩码，并拉起接口。
 * 入参：
 *   ip - SoftAP 网关 IP。
 * 返回值：成功返回 0；IP 非法或 netlib 设置失败返回负 errno。
 */
static int wifi_set_ap_ip(const char *ip)
{
    struct in_addr addr;

    if (!ip || inet_aton(ip, &addr) == 0) {
        return -EINVAL;
    }
    if (netlib_set_ipv4addr(RVT_PROV_WIFI_IFNAME, &addr) < 0) {
        return -errno;
    }

    if (inet_aton(RVT_PROV_AP_NETMASK, &addr) == 0) {
        return -EINVAL;
    }
    if (netlib_set_ipv4netmask(RVT_PROV_WIFI_IFNAME, &addr) < 0) {
        return -errno;
    }

    return netlib_ifup(RVT_PROV_WIFI_IFNAME);
}

/*
 * 函数描述：配置车机 SoftAP 阶段 DHCPD 的地址池、网关、DNS 和掩码。
 * 入参：
 *   ip - SoftAP 网关 IP。
 * 返回值：无。
 */
static void wifi_config_dhcpd(const char *ip)
{
    struct in_addr addr;

    if (inet_aton(RVT_PROV_AP_DHCP_START, &addr) != 0) {
        dhcpd_set_startip(ntohl(addr.s_addr));
    }
    if (ip && inet_aton(ip, &addr) != 0) {
        dhcpd_set_routerip(ntohl(addr.s_addr));
        dhcpd_set_dnsip(ntohl(addr.s_addr));
    }
    if (inet_aton(RVT_PROV_AP_NETMASK, &addr) != 0) {
        dhcpd_set_netmask(ntohl(addr.s_addr));
    }
}

/*
 * 函数描述：触发平台 WiFi 回连流程。调用前配网服务已经保存 wapi.conf。
 * 入参：
 *   ssid - 目标 WiFi SSID，仅用于参数校验和日志。
 *   password - 目标 WiFi 密码，仅用于参数校验。
 * 返回值：成功返回 0；连接失败返回负 errno。
 */
int rvt_prov_wifi_submit_config(const char *ssid, const char *password)
{
    char masked[32];
    int ret;

    if (!ssid || !password || ssid[0] == '\0' || password[0] == '\0') {
        return -EINVAL;
    }
    if (strnlen(ssid, RVT_PROV_SSID_MAX_LEN + 1) > RVT_PROV_SSID_MAX_LEN ||
        strlen(password) < 8 || strlen(password) > 63) {
        return -EINVAL;
    }

    PROV_LOG("provisioning: submit wifi config ssid=%s ssid_len=%d psk=%s\n",
             ssid,
             (int)strlen(ssid),
             wifi_mask_password(password, masked, sizeof(masked)));

#ifdef CONFIG_RIVOTEK_CONNECTIVITY_SERVICE
    ret = rvt_wifi_autoconnect_start();
    if (ret < 0) {
        PROV_LOG("provisioning: start wifi failed, ret=%d ssid=%s\n",
                 ret, ssid);
        return ret;
    }
#else
    ret = wifi_prepare_sta_mode();
    if (ret < 0) {
        PROV_LOG("provisioning: prepare STA connect failed, ret=%d\n", ret);
        return ret;
    }

    ret = prov_wapi_scan_target_ssid(ssid);
    if (ret < 0) {
        PROV_LOG("provisioning: target ssid scan failed, ret=%d ssid=%s\n",
                 ret, ssid);
        return ret;
    }

    ret = prov_wapi_connect_sta(ssid, password);
    if (ret < 0) {
        PROV_LOG("provisioning: wapi connect phone hotspot failed, ret=%d ssid=%s\n",
                 ret, ssid);
        return ret;
    }

    ret = wifi_obtain_ip();
    if (ret < 0) {
        PROV_LOG("provisioning: phone hotspot dhcp failed, ret=%d\n", ret);
        return ret;
    }
#endif

    return 0;
}

/*
 * 函数描述：查询 STA 是否已经获得非 SoftAP IPv4 地址。
 * 入参：无。
 * 返回值：就绪返回 1；未就绪返回 0。
 */
int rvt_prov_wifi_is_ready(void)
{
#ifdef CONFIG_RIVOTEK_CONNECTIVITY_SERVICE
    return rvt_wifi_autoconnect_is_ready();
#else
    struct in_addr addr;
    struct in_addr ap_addr;

    memset(&ap_addr, 0, sizeof(ap_addr));
    inet_aton(RVT_PROV_AP_IP_FALLBACK, &ap_addr);

    return wifi_get_ip(&addr) == 0 && addr.s_addr != ap_addr.s_addr;
#endif
}

/*
 * 函数描述：启动车机临时 SoftAP，配置静态 IP 并启动 DHCPD。
 * 入参：
 *   config - SoftAP SSID、密码、IP 和端口配置。
 * 返回值：成功返回 0；参数非法、启动 AP 或 DHCPD 失败返回负 errno。
 */
int rvt_prov_wifi_start_softap(const rvt_prov_softap_config_t *config)
{
    char masked[32];
    int sock;
    int ret;

    if (!config || config->ssid[0] == '\0' ||
        config->password[0] == '\0' || config->ip[0] == '\0') {
        return -EINVAL;
    }

    if (g_softap_started) {
        dhcpd_stop();
        g_softap_started = 0;
    }

    PROV_LOG("provisioning: start softap request ssid=%s ssid_len=%d psk=%s ip=%s\n",
             config->ssid,
             (int)strlen(config->ssid),
             wifi_mask_password(config->password, masked, sizeof(masked)),
             config->ip);

    sock = wifi_prepare_ap_mode();
    if (sock < 0) {
        return sock;
    }

    wifi_try_set_channel(sock, RVT_PROV_AP_CHANNEL);

    ret = wifi_set_wpa2_psk(sock, config->password);
    if (ret < 0) {
        PROV_LOG("provisioning: set AP psk failed, ret=%d ssid=%s psk=%s\n",
                 ret, config->ssid,
                 wifi_mask_password(config->password, masked, sizeof(masked)));
        close(sock);
        wifi_prepare_sta_mode();
        return ret;
    }

    ret = wapi_set_essid(sock, RVT_PROV_WIFI_IFNAME, config->ssid,
                         WAPI_ESSID_ON);
    close(sock);
    if (ret < 0) {
        PROV_LOG("provisioning: set AP essid failed, ret=%d ssid=%s\n",
                 ret, config->ssid);
        wifi_prepare_sta_mode();
        return ret;
    }

    ret = wifi_set_ap_ip(config->ip);
    if (ret < 0) {
        PROV_LOG("provisioning: set AP ip failed, ret=%d\n", ret);
        wifi_prepare_sta_mode();
        return ret;
    }

    wifi_config_dhcpd(config->ip);
    ret = dhcpd_start(RVT_PROV_WIFI_IFNAME);
    if (ret < 0) {
        PROV_LOG("provisioning: dhcpd start failed, ret=%d\n", ret);
        wifi_prepare_sta_mode();
        return ret;
    }

    g_softap_started = 1;
    PROV_LOG("provisioning: softap started wapi, ssid=%s psk=%s ip=%s channel=%d\n",
             config->ssid,
             wifi_mask_password(config->password, masked, sizeof(masked)),
             config->ip,
             RVT_PROV_AP_CHANNEL);
    return 0;
}

/*
 * 函数描述：停止车机临时 SoftAP，并恢复 wlan0 到 STA 管理模式。
 * 入参：无。
 * 返回值：成功返回 0；恢复 STA 失败返回负 errno。
 */
int rvt_prov_wifi_stop_softap(void)
{
#ifndef CONFIG_RIVOTEK_CONNECTIVITY_SERVICE
    int ret = 0;
#endif

    if (g_softap_started) {
        dhcpd_stop();
        g_softap_started = 0;
        /*
         * APP 刚收到 accepted 响应后通常还处于车机 SoftAP 关联态。
         * 先留出客户端断开和驱动 station 清理时间，避免马上 ifdown
         * 触发 Realtek AP deinit 时释放刚完成握手的 STA。
         */
        usleep(RVT_PROV_SOFTAP_CLIENT_LEAVE_US);
    }

    usleep(RVT_PROV_SOFTAP_STOP_SETTLE_US);
#ifdef CONFIG_RIVOTEK_CONNECTIVITY_SERVICE
    PROV_LOG("provisioning: softap stopped\n");
    return 0;
#else
    ret = wifi_prepare_sta_mode();
    PROV_LOG("provisioning: softap stopped, prepare STA ret=%d\n", ret);
    return ret < 0 ? ret : 0;
#endif
}
