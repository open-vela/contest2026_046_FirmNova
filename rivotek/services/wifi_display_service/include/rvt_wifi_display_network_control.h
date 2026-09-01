#ifndef __RVT_WIFI_DISPLAY_NETWORK_CONTROL_H__
#define __RVT_WIFI_DISPLAY_NETWORK_CONTROL_H__

/*
 * 函数名: rvt_wifi_display_sap_start
 * 入参: 无
 * 返回值: 0 表示热点已开启或启动成功，负值表示启动失败
 */
int rvt_wifi_display_sap_start(void);

/*
 * 函数名: rvt_wifi_display_sta_prepare
 * 入参: 无
 * 返回值: 0 表示 STA 已连接可用于投屏，负值表示未连接或模式设置失败
 */
int rvt_wifi_display_sta_prepare(void);

/*
 * 函数名: rvt_wifi_display_sap_is_active
 * 入参: 无
 * 返回值: 1 表示投屏热点已开启，0 表示未开启
 */
int rvt_wifi_display_sap_is_active(void);

/*
 * 函数名: rvt_wifi_display_sap_stop
 * 入参: 无
 * 返回值: 0 表示热点已关闭或关闭成功，负值表示关闭失败
 */
int rvt_wifi_display_sap_stop(void);

/*
 * 函数名: rvt_wifi_display_get_tcp_peer_ip
 * 入参: ip_buf 用于输出 TCP 对端 IPv4 字符串，buf_len 输出缓冲区长度
 * 返回值: 0 表示获取成功，负值表示当前平台或链路无法获取
 */
int rvt_wifi_display_get_tcp_peer_ip(char *ip_buf, int buf_len);

/*
 * 函数名: rvt_wifi_display_get_local_ip
 * 入参: ip_buf 用于输出当前投屏链路本机 IPv4 字符串，buf_len 输出缓冲区长度
 * 返回值: 0 表示获取成功，负值表示当前平台或链路无法获取
 */
int rvt_wifi_display_get_local_ip(char *ip_buf, int buf_len);

/*
 * 函数名: rvt_wifi_display_log_network_state
 * 入参: reason 打印触发原因
 * 返回值: 无
 */
void rvt_wifi_display_log_network_state(const char *reason);

#endif
