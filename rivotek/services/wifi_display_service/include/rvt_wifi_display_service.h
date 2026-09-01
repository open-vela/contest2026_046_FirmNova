#ifndef __RVT_WIFI_DISPLAY_SERVICE_H__
#define __RVT_WIFI_DISPLAY_SERVICE_H__

#include "rvt_wifi_display_api.h"
#include "rvt_wifi_display_types.h"
#include "rvt_wifi_display_osal.h"
#include <stdint.h>
#include <sys/socket.h>

#define PORT_UDP       5004
#define PORT_TCP       6004
#define MAX_PACKET     65536

typedef enum {
    FORMAT_JPEG,
    FORMAT_H264,
} video_format_t;

typedef struct {
    int width;
    int height;
    uint8_t *yuv_data;
} frame_t;

typedef enum {
    RVT_WIFI_DISPLAY_LINK_STA = 0,
    RVT_WIFI_DISPLAY_LINK_SAP = 1,
} rvt_wifi_display_link_mode_t;

/*
 * 函数名: rvt_tcp_channel_init
 * 入参: 无
 * 返回值: 0 表示 TCP 通道启动成功，负值表示启动失败
 */
int rvt_tcp_channel_init(void);

/*
 * 函数名: rvt_tcp_channel_stop
 * 入参: 无
 * 返回值: 无
 */
void rvt_tcp_channel_stop(void);

/*
 * 函数名: rvt_tcp_channel_send_command
 * 入参: cmd 需要通过 TCP 发给手机端的一行命令，调用者负责携带换行符
 * 返回值: 0 表示发送成功，负值表示 TCP 未连接或发送失败
 */
int rvt_tcp_channel_send_command(const char *cmd);

/*
 * 函数名: rvt_tcp_channel_has_client
 * 入参: 无
 * 返回值: 1 表示手机端控制通道已建立并完成心跳绑定，0 表示尚未建立
 */
int rvt_tcp_channel_has_client(void);

/*
 * 函数名: rvt_wifi_display_discovery_start
 * 入参: 无
 * 返回值: 0 表示发现服务启动成功，负值表示启动失败
 */
int rvt_wifi_display_discovery_start(void);

/*
 * 函数名: rvt_wifi_display_discovery_stop
 * 入参: 无
 * 返回值: 无
 */
void rvt_wifi_display_discovery_stop(void);

/*
 * 函数名: udp_receiver_init
 * 入参: 无
 * 返回值: 0 表示 UDP 接收通道启动成功，负值表示启动失败
 */
int udp_receiver_init(void);

/*
 * 函数名: udp_receiver_stop
 * 入参: 无
 * 返回值: 无
 */
void udp_receiver_stop(void);

/*
 * 函数名: udp_receiver_clear_stream
 * 入参: 无
 * 返回值: 无
 */
void udp_receiver_clear_stream(void);

/*
 * 函数名: rvt_wifi_display_get_caps
 * 入参: caps 用于输出车机端显示和解码能力
 * 返回值: 0 表示获取成功，负值表示获取失败
 */
int rvt_wifi_display_get_caps(struct rvt_display_caps *caps);

/*
 * 函数名: rvt_wifi_display_get_stream_config
 * 入参: config 用于输出当前投屏数据流配置
 * 返回值: 0 表示获取成功，负值表示获取失败
 */
int rvt_wifi_display_get_stream_config(struct rvt_wifi_display_stream_config *config);

/*
 * 函数名: rvt_wifi_display_set_stream_resolution
 * 入参: width 数据流宽度像素，height 数据流高度像素
 * 返回值: 0 表示设置成功，负值表示参数无效
 */
int rvt_wifi_display_set_stream_resolution(int width, int height);

/*
 * 函数名: rvt_wifi_display_set_bitrate
 * 入参: bitrate_kbps 目标码率，单位 kbps
 * 返回值: 0 表示设置成功，负值表示参数无效
 */
int rvt_wifi_display_set_bitrate(int bitrate_kbps);

/*
 * 函数名: rvt_wifi_display_set_codec_mask
 * 入参: codec_mask 支持的编码格式掩码
 * 返回值: 0 表示设置成功，负值表示设置失败
 */
int rvt_wifi_display_set_codec_mask(uint32_t codec_mask);

/*
 * 函数名: get_udp_statistics
 * 入参: total 用于输出接收包数量，lost 用于输出丢包或丢帧数量
 * 返回值: 无
 */
void get_udp_statistics(uint32_t *total, uint32_t *lost);

/*
 * 函数名: rvt_wifi_display_cmd_start
 * 入参: argc 参数数量，argv 参数列表，支持 start_wifi_display [sta|sap]
 * 返回值: 0 表示执行成功，负值表示失败
 */
int rvt_wifi_display_cmd_start(int argc, char *argv[]);

/*
 * 函数名: rvt_wifi_display_cmd_stop
 * 入参: argc 参数数量，argv 参数列表，支持 stop_wifi_display
 * 返回值: 0 表示执行成功，负值表示失败
 */
int rvt_wifi_display_cmd_stop(int argc, char *argv[]);

/*
 * 函数名: rvt_wifi_display_cmd_screen_size
 * 入参: argc 参数数量，argv 参数列表，支持 wifi_display_screen_size <width> <height>
 * 返回值: 0 表示执行成功，负值表示失败
 */
int rvt_wifi_display_cmd_screen_size(int argc, char *argv[]);

/*
 * 函数名: rvt_wifi_display_cmd_rotation
 * 入参: argc 参数数量，argv 参数列表，支持 wifi_display_rotation <0|90|180|270>
 * 返回值: 0 表示执行成功，负值表示失败
 */
int rvt_wifi_display_cmd_rotation(int argc, char *argv[]);

#endif
