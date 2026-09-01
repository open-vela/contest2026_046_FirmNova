#ifndef __VENDOR_ALLWINNERTECH_RIVOTEK_QRCODE_H
#define __VENDOR_ALLWINNERTECH_RIVOTEK_QRCODE_H

#include <stdint.h>
#include <lvgl/lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 二维码 payload 最大长度，业务层传入内容超过该长度会被拒绝显示。 */
#define RVT_QRCODE_PAYLOAD_MAX_LEN 512
/* 二维码弹层标题、说明等显示文本的最大长度。 */
#define RVT_QRCODE_TEXT_MAX_LEN 96

typedef struct rvt_qrcode_ui_metrics {
    /* 当前显示宽度，传 0 时使用 800 基准宽度。 */
    int disp_w;
    /* 当前显示高度，传 0 时使用 480 基准高度。 */
    int disp_h;
    /* 可选 16 号字体；为空时使用 LVGL 默认字体。 */
    const lv_font_t *font_16;
    /* 可选 20 号字体；为空时使用 LVGL 默认字体。 */
    const lv_font_t *font_20;
    /* 可选 28 号字体；为空时使用 LVGL 默认字体。 */
    const lv_font_t *font_28;
    /* 可选 40 号字体；为空时使用 LVGL 默认字体。 */
    const lv_font_t *font_40;
} rvt_qrcode_ui_metrics_t;

typedef enum {
    /* 通用自定义二维码业务，QR service 只负责显示 payload。 */
    RVT_QRCODE_BIZ_CUSTOM = 0,
    /* WiFi 配网二维码业务。 */
    RVT_QRCODE_BIZ_WIFI_PROVISIONING,
    /* AI 设备绑定或 AI 相关业务二维码。 */
    RVT_QRCODE_BIZ_AI_DEVICE,
} rvt_qrcode_biz_t;

typedef struct rvt_qrcode_request {
    /* 业务类型，用于选择默认标题和提示文案。 */
    rvt_qrcode_biz_t biz;
    /* 需要编码进二维码的原始字符串，不能为空。 */
    const char *payload;
    /* 可选标题；为空时按 biz 使用默认标题。 */
    const char *title;
    /* 可选主说明文本，例如热点 SSID。 */
    const char *primary_text;
    /* 可选辅助说明文本，例如热点密码。 */
    const char *secondary_text;
    /* 可选底部提示文案；为空时按 biz 使用默认提示。 */
    const char *hint_text;
    /* 二维码显示倒计时，单位秒；小于等于 0 表示不自动隐藏。 */
    int ttl_seconds;
    /* 非 0 表示只显示状态文案，不渲染二维码图形。 */
    int status_only;
    /* 非 0 表示仍按 ttl_seconds 自动隐藏，但不显示倒计时文本。 */
    int hide_countdown;
} rvt_qrcode_request_t;

/*
 * 函数描述：初始化 QR service，绑定 LVGL 根屏幕和 UI 缩放参数。
 * 入参：
 *   screen - LVGL 根屏幕对象。
 *   ui - UI 尺寸和字体参数，可为空。
 * 返回值：无。
 */
void rvt_qrcode_init(lv_obj_t *screen, const rvt_qrcode_ui_metrics_t *ui);

/*
 * 函数描述：提交二维码显示请求，由 UI 线程轮询时实际创建或刷新弹层。
 * 入参：
 *   request - 二维码显示请求。
 * 返回值：成功返回 0；请求非法返回负 errno。
 */
int rvt_qrcode_show(const rvt_qrcode_request_t *request);

/*
 * 函数描述：提交二维码隐藏请求，由 UI 线程轮询时实际隐藏弹层。
 * 入参：无。
 * 返回值：成功返回 0；未启用二维码能力时返回负 errno。
 */
int rvt_qrcode_hide(void);

/*
 * 函数描述：在 LVGL UI 线程中处理二维码显示、隐藏、倒计时和置顶刷新。
 * 入参：无。
 * 返回值：无。
 */
void rvt_qrcode_poll(void);

#ifdef __cplusplus
}
#endif

#endif
