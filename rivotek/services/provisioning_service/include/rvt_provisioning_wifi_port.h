#ifndef RVT_PROVISIONING_WIFI_PORT_H
#define RVT_PROVISIONING_WIFI_PORT_H

#include "rvt_provisioning_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 函数描述：触发平台 WiFi 回连流程。调用前配网服务已经保存 wapi.conf。
 * 入参：
 *   ssid - 目标 WiFi SSID，仅用于参数校验和日志。
 *   password - 目标 WiFi 密码，仅用于参数校验。
 * 返回值：成功返回 0；触发连接失败返回负 errno。
 */
int rvt_prov_wifi_submit_config(const char *ssid, const char *password);

/*
 * 函数描述：查询 STA WiFi 是否已经拿到可用 IPv4 地址。
 * 入参：无。
 * 返回值：就绪返回 1；未连接、无地址或仍是 SoftAP 地址返回 0。
 */
int rvt_prov_wifi_is_ready(void);

/*
 * 函数描述：启动车机临时 SoftAP，APP 扫码连接后下发真实手机热点信息。
 * 入参：
 *   config - 车机临时 SoftAP 配置。
 * 返回值：成功返回 0；启动 SoftAP 或配置网络失败返回负 errno。
 */
int rvt_prov_wifi_start_softap(const rvt_prov_softap_config_t *config);

/*
 * 函数描述：读取车机 SoftAP 启动后的本机 IPv4 地址，用于二维码 payload。
 * 入参：
 *   ip_buf - 输出 IPv4 字符串缓冲区。
 *   buf_len - 输出缓冲区长度。
 * 返回值：成功返回 0；平台不支持、接口未就绪或参数非法返回负 errno。
 */
int rvt_prov_wifi_get_softap_ip(char *ip_buf, int buf_len);

/*
 * 函数描述：停止车机临时 SoftAP，并恢复到后续 STA 连接所需状态。
 * 入参：无。
 * 返回值：成功返回 0；停止 SoftAP 或恢复状态失败返回负 errno。
 */
int rvt_prov_wifi_stop_softap(void);

#ifdef __cplusplus
}
#endif

#endif
