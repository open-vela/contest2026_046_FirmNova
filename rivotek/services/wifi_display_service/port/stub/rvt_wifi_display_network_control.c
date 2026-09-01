#include "rvt_wifi_display_network_control.h"
#include "rvt_wifi_display_types.h"

/*
 * 函数名: rvt_wifi_display_sap_start
 * 入参: 无
 * 返回值: 0 表示空适配层不需要启动热点
 */
int rvt_wifi_display_sap_start(void)
{
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG, "wifi display sap: no platform adapter, skip ap start");
    return 0;
}

/*
 * 函数名: rvt_wifi_display_sta_prepare
 * 入参: 无
 * 返回值: 0 表示空适配层不需要检查 STA 连接
 */
int rvt_wifi_display_sta_prepare(void)
{
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG, "wifi display sta: no platform adapter, skip sta check");
    return 0;
}

/*
 * 函数名: rvt_wifi_display_sap_is_active
 * 入参: 无
 * 返回值: 1 表示空适配层认为热点已就绪
 */
int rvt_wifi_display_sap_is_active(void)
{
    return 1;
}

/*
 * 函数名: rvt_wifi_display_sap_stop
 * 入参: 无
 * 返回值: 0 表示空适配层不需要关闭热点
 */
int rvt_wifi_display_sap_stop(void)
{
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG, "wifi display sap: no platform adapter, skip ap stop");
    return 0;
}

/*
 * 函数名: rvt_wifi_display_get_tcp_peer_ip
 * 入参: ip_buf 用于输出 TCP 对端 IPv4 字符串，buf_len 输出缓冲区长度
 * 返回值: 负值表示空适配层没有可用 TCP 对端地址
 */
int rvt_wifi_display_get_tcp_peer_ip(char *ip_buf, int buf_len)
{
    (void)ip_buf;
    (void)buf_len;
    return -1;
}

int rvt_wifi_display_get_local_ip(char *ip_buf, int buf_len)
{
    (void)ip_buf;
    (void)buf_len;
    return -1;
}

void rvt_wifi_display_log_network_state(const char *reason)
{
    (void)reason;
}
