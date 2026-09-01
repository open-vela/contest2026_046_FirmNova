#include "rvt_wifi_display_network_control.h"
#include "rvt_wifi_display_types.h"
#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <netutils/dhcpd.h>
#include <netutils/netlib.h>
#include <string.h>
#include <unistd.h>
#include <wireless/wapi.h>

#define RVT_R528_WIFI_IFNAME     "wlan0"
#define RVT_R528_AP_SSID         "yadi_12345678"
#define RVT_R528_AP_PSK          "12345678"
#define RVT_R528_AP_IP           "192.168.49.1"
#define RVT_R528_AP_NETMASK      "255.255.255.0"
#define RVT_R528_AP_DHCP_START   "192.168.49.2"
#define RVT_R528_AP_WAIT_RETRY   10
#define RVT_R528_AP_WAIT_MS      200
#define RVT_R528_MODE_RESET_US   (300 * 1000)

static int sap_started;

static int r528_wifi_socket(void)
{
    int sock = wapi_make_socket();

    if (sock < 0)
        RVT_LOGE(RVT_WIFI_DISPLAY_LOG_TAG, "wifi display: wapi socket failed");

    return sock;
}

static int r528_wifi_ifup(int sock)
{
    int ret = wapi_set_ifup(sock, RVT_R528_WIFI_IFNAME);

    if (ret < 0) {
        RVT_LOGE(RVT_WIFI_DISPLAY_LOG_TAG,
                 "wifi display: ifup %s failed, ret=%d",
                 RVT_R528_WIFI_IFNAME, ret);
        return ret;
    }

    return 0;
}

static int r528_wifi_ifdown(int sock)
{
    int ret = wapi_set_ifdown(sock, RVT_R528_WIFI_IFNAME);

    if (ret < 0) {
        RVT_LOGE(RVT_WIFI_DISPLAY_LOG_TAG,
                 "wifi display: ifdown %s failed, ret=%d",
                 RVT_R528_WIFI_IFNAME, ret);
        return ret;
    }

    return 0;
}

static int r528_wifi_prepare_mode(int sock, enum wapi_mode_e mode)
{
    int ret;

    wpa_driver_wext_disconnect(sock, RVT_R528_WIFI_IFNAME);

    ret = r528_wifi_ifdown(sock);
    if (ret < 0)
        return ret;

    usleep(RVT_R528_MODE_RESET_US);

    ret = r528_wifi_ifup(sock);
    if (ret < 0)
        return ret;

    ret = wapi_set_mode(sock, RVT_R528_WIFI_IFNAME, mode);
    if (ret < 0) {
        RVT_LOGE(RVT_WIFI_DISPLAY_LOG_TAG,
                 "wifi display: set mode %d failed, ret=%d", mode, ret);
        return ret;
    }

    return 0;
}

static int r528_wifi_get_ip(struct in_addr *addr)
{
    memset(addr, 0, sizeof(*addr));
    if (netlib_get_ipv4addr(RVT_R528_WIFI_IFNAME, addr) < 0)
        return -1;

    return addr->s_addr != 0 ? 0 : -1;
}

static const char *r528_addr_to_str(struct in_addr addr, char *buf, int len)
{
    const char *ip;

    if (!buf || len <= 0)
        return "";

    ip = inet_ntoa(addr);
    if (!ip) {
        snprintf(buf, len, "0.0.0.0");
    } else {
        snprintf(buf, len, "%s", ip);
    }

    return buf;
}

static int r528_wifi_wait_ip(struct in_addr *addr)
{
    int i;

    for (i = 0; i < RVT_R528_AP_WAIT_RETRY; i++) {
        if (r528_wifi_get_ip(addr) == 0)
            return 0;
        usleep(RVT_R528_AP_WAIT_MS * 1000);
    }

    return -1;
}

static int r528_wifi_set_ap_ip(void)
{
    struct in_addr addr;

    if (inet_aton(RVT_R528_AP_IP, &addr) == 0)
        return -EINVAL;
    if (netlib_set_ipv4addr(RVT_R528_WIFI_IFNAME, &addr) < 0)
        return -errno;

    if (inet_aton(RVT_R528_AP_NETMASK, &addr) == 0)
        return -EINVAL;
    if (netlib_set_ipv4netmask(RVT_R528_WIFI_IFNAME, &addr) < 0)
        return -errno;

    return netlib_ifup(RVT_R528_WIFI_IFNAME);
}

static int r528_wifi_set_psk(int sock, const char *password)
{
    int ret;

    /*
     * 对齐命令行 `wapi psk wlan0 <psk> 3` 的关键动作:
     * 设置 WPA2、CCMP cipher，再写入 PSK。
     */
    ret = wpa_driver_wext_set_auth_param(sock, RVT_R528_WIFI_IFNAME,
                                         IW_AUTH_WPA_VERSION,
                                         IW_AUTH_WPA_VERSION_WPA2);
    if (ret < 0)
        return ret;

    ret = wpa_driver_wext_set_auth_param(sock, RVT_R528_WIFI_IFNAME,
                                         IW_AUTH_CIPHER_PAIRWISE,
                                         IW_AUTH_CIPHER_CCMP);
    if (ret < 0)
        return ret;

    return wpa_driver_wext_set_key_ext(sock, RVT_R528_WIFI_IFNAME,
                                       WPA_ALG_CCMP, password,
                                       strlen(password));
}

static void r528_wifi_config_dhcpd(void)
{
    struct in_addr addr;

    if (inet_aton(RVT_R528_AP_DHCP_START, &addr) != 0)
        dhcpd_set_startip(ntohl(addr.s_addr));
    if (inet_aton(RVT_R528_AP_IP, &addr) != 0) {
        dhcpd_set_routerip(ntohl(addr.s_addr));
        dhcpd_set_dnsip(ntohl(addr.s_addr));
    }
    if (inet_aton(RVT_R528_AP_NETMASK, &addr) != 0)
        dhcpd_set_netmask(ntohl(addr.s_addr));
}

int rvt_wifi_display_sap_start(void)
{
    int sock;
    int ret;

    if (sap_started && rvt_wifi_display_sap_is_active())
        return 0;

    sock = r528_wifi_socket();
    if (sock < 0)
        return -1;

    ret = r528_wifi_prepare_mode(sock, WAPI_MODE_MASTER);
    if (ret < 0) {
        RVT_LOGE(RVT_WIFI_DISPLAY_LOG_TAG,
                 "wifi display sap: prepare ap mode failed, ret=%d", ret);
        goto out;
    }

    ret = r528_wifi_set_psk(sock, RVT_R528_AP_PSK);
    if (ret < 0) {
        RVT_LOGE(RVT_WIFI_DISPLAY_LOG_TAG,
                 "wifi display sap: set psk failed, ret=%d", ret);
        goto out;
    }

    ret = wapi_set_essid(sock, RVT_R528_WIFI_IFNAME, RVT_R528_AP_SSID,
                         WAPI_ESSID_ON);
    if (ret < 0) {
        RVT_LOGE(RVT_WIFI_DISPLAY_LOG_TAG,
                 "wifi display sap: set ssid failed, ret=%d", ret);
        goto out;
    }

    ret = r528_wifi_set_ap_ip();
    if (ret < 0) {
        RVT_LOGE(RVT_WIFI_DISPLAY_LOG_TAG,
                 "wifi display sap: set ap ip failed, ret=%d", ret);
        goto out;
    }

    r528_wifi_config_dhcpd();
    ret = dhcpd_start(RVT_R528_WIFI_IFNAME);
    if (ret < 0) {
        RVT_LOGE(RVT_WIFI_DISPLAY_LOG_TAG,
                 "wifi display sap: dhcpd start failed, ret=%d", ret);
        goto out;
    }

    sap_started = 1;
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "wifi display sap: started, if=%s ssid=%s ip=%s",
             RVT_R528_WIFI_IFNAME, RVT_R528_AP_SSID, RVT_R528_AP_IP);

out:
    close(sock);
    return ret < 0 ? ret : 0;
}

int rvt_wifi_display_sta_prepare(void)
{
    struct in_addr addr;
    int need_mode_prepare;
    int sock = -1;
    int ret;

    need_mode_prepare = sap_started || rvt_wifi_display_sap_is_active();
    if (need_mode_prepare) {
        sock = r528_wifi_socket();
        if (sock < 0)
            return -1;
        dhcpd_stop();
        sap_started = 0;

        ret = r528_wifi_prepare_mode(sock, WAPI_MODE_MANAGED);
        close(sock);
        if (ret < 0) {
            RVT_LOGE(RVT_WIFI_DISPLAY_LOG_TAG,
                     "wifi display sta: prepare sta mode failed, ret=%d", ret);
            return ret;
        }
    } else {
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "wifi display sta: reuse current sta link");
    }

    /*
     * STA 模式不在这里写死手机热点 SSID/密码；板端需先通过 wapi/配置文件
     * 连接手机热点。这里负责刷新 DHCP 并确认 TCP/UDP 可以使用的本机 IP。
     */
    if (need_mode_prepare || r528_wifi_get_ip(&addr) != 0)
        netlib_obtain_ipv4addr(RVT_R528_WIFI_IFNAME);

    if (r528_wifi_wait_ip(&addr) != 0) {
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "wifi display sta: %s has no ipv4 address",
                 RVT_R528_WIFI_IFNAME);
        return -ENETDOWN;
    }

    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG, "wifi display sta: %s ip=%s",
             RVT_R528_WIFI_IFNAME, inet_ntoa(addr));
    rvt_wifi_display_log_network_state("sta_prepare");
    return 0;
}

int rvt_wifi_display_sap_is_active(void)
{
    struct in_addr addr;

    return r528_wifi_get_ip(&addr) == 0 &&
           addr.s_addr == inet_addr(RVT_R528_AP_IP);
}

int rvt_wifi_display_sap_stop(void)
{
    int sock;
    int ret = 0;
    int need_mode_prepare = sap_started || rvt_wifi_display_sap_is_active();

    if (!need_mode_prepare) {
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "wifi display sap: already stopped");
        return 0;
    }

    dhcpd_stop();
    sap_started = 0;

    sock = r528_wifi_socket();
    if (sock < 0)
        return -1;

    ret = r528_wifi_prepare_mode(sock, WAPI_MODE_MANAGED);
    if (ret < 0) {
        RVT_LOGE(RVT_WIFI_DISPLAY_LOG_TAG,
                 "wifi display sap: restore sta mode failed, ret=%d", ret);
    } else {
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "wifi display sap: stopped and restored sta mode");
    }

    close(sock);
    return ret;
}

int rvt_wifi_display_get_tcp_peer_ip(char *ip_buf, int buf_len)
{
    struct in_addr router;
    struct in_addr local;
    struct in_addr netmask;
    char local_ip[16];
    char netmask_ip[16];
    char router_ip[16];
    const char *ip;

    if (!ip_buf || buf_len <= 0)
        return -EINVAL;

    ip_buf[0] = '\0';

    /*
     * R528 作为 STA 连接手机热点时，默认路由网关就是手机热点侧地址。
     * TCP 角色切换后，车机直接连接该地址的 6004 端口。
     */
    memset(&local, 0, sizeof(local));
    memset(&netmask, 0, sizeof(netmask));
    memset(&router, 0, sizeof(router));
    if (netlib_get_ipv4addr(RVT_R528_WIFI_IFNAME, &local) < 0) {
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "wifi display sta: get local ip failed on %s, errno=%d",
                 RVT_R528_WIFI_IFNAME, errno);
    }
    if (netlib_get_ipv4netmask(RVT_R528_WIFI_IFNAME, &netmask) < 0) {
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "wifi display sta: get netmask failed on %s, errno=%d",
                 RVT_R528_WIFI_IFNAME, errno);
    }
    if (netlib_get_dripv4addr(RVT_R528_WIFI_IFNAME, &router) < 0 ||
        router.s_addr == 0) {
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "wifi display sta: get gateway failed on %s, errno=%d local=%s mask=%s",
                 RVT_R528_WIFI_IFNAME,
                 errno,
                 r528_addr_to_str(local, local_ip, sizeof(local_ip)),
                 r528_addr_to_str(netmask, netmask_ip, sizeof(netmask_ip)));
        return -1;
    }

    ip = inet_ntoa(router);
    if (!ip || strlen(ip) + 1 > (size_t)buf_len)
        return -ENOSPC;

    strcpy(ip_buf, ip);
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "wifi display sta: tcp peer gateway=%s local=%s mask=%s",
             r528_addr_to_str(router, router_ip, sizeof(router_ip)),
             r528_addr_to_str(local, local_ip, sizeof(local_ip)),
             r528_addr_to_str(netmask, netmask_ip, sizeof(netmask_ip)));
    return 0;
}

int rvt_wifi_display_get_local_ip(char *ip_buf, int buf_len)
{
    struct in_addr local;
    const char *ip;

    if (!ip_buf || buf_len <= 0)
        return -EINVAL;

    ip_buf[0] = '\0';
    memset(&local, 0, sizeof(local));
    if (netlib_get_ipv4addr(RVT_R528_WIFI_IFNAME, &local) < 0 ||
        local.s_addr == 0) {
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "wifi display sta: get local ip failed on %s, errno=%d",
                 RVT_R528_WIFI_IFNAME, errno);
        return -1;
    }

    ip = inet_ntoa(local);
    if (!ip || strlen(ip) + 1 > (size_t)buf_len)
        return -ENOSPC;

    strcpy(ip_buf, ip);
    return 0;
}

void rvt_wifi_display_log_network_state(const char *reason)
{
    struct in_addr local;
    struct in_addr netmask;
    struct in_addr router;
    char local_ip[16];
    char netmask_ip[16];
    char router_ip[16];
    int local_ret;
    int mask_ret;
    int router_ret;

    memset(&local, 0, sizeof(local));
    memset(&netmask, 0, sizeof(netmask));
    memset(&router, 0, sizeof(router));

    local_ret = netlib_get_ipv4addr(RVT_R528_WIFI_IFNAME, &local);
    mask_ret = netlib_get_ipv4netmask(RVT_R528_WIFI_IFNAME, &netmask);
    router_ret = netlib_get_dripv4addr(RVT_R528_WIFI_IFNAME, &router);

    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "wifi display net[%s]: if=%s ip=%s mask=%s gw=%s ret=%d/%d/%d sap=%d",
             reason ? reason : "",
             RVT_R528_WIFI_IFNAME,
             r528_addr_to_str(local, local_ip, sizeof(local_ip)),
             r528_addr_to_str(netmask, netmask_ip, sizeof(netmask_ip)),
             r528_addr_to_str(router, router_ip, sizeof(router_ip)),
             local_ret,
             mask_ret,
             router_ret,
             sap_started);
}
