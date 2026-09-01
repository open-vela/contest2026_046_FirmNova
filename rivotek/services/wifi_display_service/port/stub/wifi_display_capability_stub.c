#include "rvt_display_port.h"
#include <string.h>

/*
 * 函数名: rvt_display_port_get_caps
 * 入参: caps 用于输出默认显示和解码能力
 * 返回值: 0 表示获取成功，负值表示获取失败
 */
int rvt_display_port_get_caps(struct rvt_display_caps *caps)
{
    if (!caps)
        return -1;

    memset(caps, 0, sizeof(*caps));
    caps->width = 800;
    caps->height = 480;
    caps->codec_mask = 0;
    return 0;
}

/*
 * 函数名: rvt_display_port_init
 * 入参: 无
 * 返回值: 0 表示空适配层初始化成功
 */
int rvt_display_port_init(void)
{
    return 0;
}

/*
 * 函数名: rvt_display_port_deinit
 * 入参: 无
 * 返回值: 无
 */
void rvt_display_port_deinit(void)
{
}

/*
 * 函数名: rvt_display_port_flush
 * 入参: 无
 * 返回值: 无
 */
void rvt_display_port_flush(void)
{
}

/*
 * 函数名: rvt_display_port_submit_jpeg
 * 入参: data JPEG 数据首地址，len JPEG 数据长度
 * 返回值: 固定返回负值，表示当前平台未启用解码和渲染能力
 */
int rvt_display_port_submit_jpeg(const uint8_t *data, int len)
{
    static int warned = 0;

    (void)data;
    (void)len;

    if (!warned) {
        warned = 1;
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG, "display: no platform adapter, decode/render disabled");
    }

    return -1;
}
