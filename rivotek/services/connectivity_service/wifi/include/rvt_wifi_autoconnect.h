#ifndef RVT_WIFI_AUTOCONNECT_H
#define RVT_WIFI_AUTOCONNECT_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 函数描述：根据 /data/etc/wifi/wapi.conf 启动 wlan0 STA 回连流程。
 * 入参：无。
 * 返回值：成功返回 0；配置缺失、连接失败或 DHCP 失败返回负 errno。
 */
int rvt_wifi_autoconnect_start(void);

/*
 * 函数描述：查询 wlan0 是否已经获得可用于 STA 业务的 IPv4 地址。
 * 入参：无。
 * 返回值：就绪返回 1；未连接、无地址或仍是 SoftAP 地址返回 0。
 */
int rvt_wifi_autoconnect_is_ready(void);

#ifdef __cplusplus
}
#endif

#endif