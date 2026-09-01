#ifndef __RVT_WIFI_DISPLAY_TYPES_H__
#define __RVT_WIFI_DISPLAY_TYPES_H__

#include <stdint.h>
#include "rivotek/rivotek_log.h"

#define RVT_DISPLAY_CODEC_JPEG  (1U << 0)
#define RVT_DISPLAY_CODEC_MJPEG (1U << 1)
#define RVT_DISPLAY_CODEC_H264  (1U << 2)
#define RVT_DISPLAY_CODEC_PNG   (1U << 3)

/* Android 端 UDP 自定义包头格式，车机端按该格式拆包组帧。 */
#define RVT_WIFI_DISPLAY_UDP_SYNC0       0xAB
#define RVT_WIFI_DISPLAY_UDP_SYNC1       0x01
#define RVT_WIFI_DISPLAY_UDP_HEADER_LEN  16
#define RVT_WIFI_DISPLAY_UDP_FLAG_SOF    0x01
#define RVT_WIFI_DISPLAY_UDP_FLAG_EOF    0x02

#ifndef RVT_WIFI_DISPLAY_LOG_TAG
#define RVT_WIFI_DISPLAY_LOG_TAG  "wifi_display"
#endif

#ifndef RVT_MAP_LOG_TAG
#define RVT_MAP_LOG_TAG  "map_service"
#endif

#ifndef RVT_UDP_LOG_TAG
#define RVT_UDP_LOG_TAG  "udp_receiver"
#endif

/*
 * wifi_display_service/map_service 只依赖 rivotek_log.h 作为日志后端。
 * 平台侧适配 RT-Thread/OpenVela 时，只需要调整 rivotek_log.h 的实现。
 */

struct rvt_display_caps {
    /* 车机屏幕宽度，单位像素。 */
    int width;
    /* 车机屏幕高度，单位像素。 */
    int height;
    /* 物理宽度，单位毫米；未知时为 0。 */
    int width_mm;
    /* 物理高度，单位毫米；未知时为 0。 */
    int height_mm;
    /* 支持的编码格式掩码，使用 RVT_DISPLAY_CODEC_* 组合。 */
    uint32_t codec_mask;
};

struct rvt_wifi_display_stream_config {
    /* 手机端投屏编码输出宽度。 */
    int width;
    /* 手机端投屏编码输出高度。 */
    int height;
    /* 手机端编码旋转角度，只支持 0/90/180/270。 */
    int rotation;
    /* 目标码率，单位 kbps；0 表示使用发送端默认值。 */
    int bitrate_kbps;
    /* 当前希望手机端使用的编码格式掩码。 */
    uint32_t codec_mask;
};

#endif
