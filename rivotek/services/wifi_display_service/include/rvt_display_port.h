#ifndef __RVT_DISPLAY_PORT_H__
#define __RVT_DISPLAY_PORT_H__

#include "rvt_wifi_display_types.h"

/*
 * 函数名: rvt_display_port_init
 * 入参: 无
 * 返回值: 0 表示平台解码和渲染通道初始化成功，负值表示失败
 */
int rvt_display_port_init(void);

/*
 * 函数名: rvt_display_port_deinit
 * 入参: 无
 * 返回值: 无
 */
void rvt_display_port_deinit(void);

/*
 * 函数名: rvt_display_port_flush
 * 入参: 无
 * 返回值: 无
 */
void rvt_display_port_flush(void);

/*
 * 函数名: rvt_display_port_submit_jpeg
 * 入参: data JPEG 数据首地址，len JPEG 数据长度
 * 返回值: 0 表示提交成功，负值表示提交失败
 */
int rvt_display_port_submit_jpeg(const uint8_t *data, int len);

/*
 * 函数名: rvt_display_port_get_caps
 * 入参: caps 用于输出平台显示和解码能力
 * 返回值: 0 表示获取成功，负值表示获取失败
 */
int rvt_display_port_get_caps(struct rvt_display_caps *caps);

#endif /* __RVT_DISPLAY_PORT_H__ */
