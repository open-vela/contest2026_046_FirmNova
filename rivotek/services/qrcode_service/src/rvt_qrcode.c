#include <nuttx/config.h>

#include "rvt_qrcode.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define RVT_QRCODE_BASE_W 800
#define RVT_QRCODE_BASE_H 480

#if LV_USE_QRCODE

#include <pthread.h>

typedef enum {
    /* UI 线程下一次 poll 时显示二维码。 */
    RVT_QRCODE_ACTION_SHOW = 0,
    /* UI 线程下一次 poll 时隐藏二维码。 */
    RVT_QRCODE_ACTION_HIDE,
} rvt_qrcode_action_t;

/* UI 线程内部使用的二维码显示请求副本，避免业务线程持有的字符串失效。 */
typedef struct rvt_qrcode_display {
    rvt_qrcode_biz_t biz;
    char payload[RVT_QRCODE_PAYLOAD_MAX_LEN];
    char title[RVT_QRCODE_TEXT_MAX_LEN];
    char primary_text[RVT_QRCODE_TEXT_MAX_LEN];
    char secondary_text[RVT_QRCODE_TEXT_MAX_LEN];
    char hint_text[RVT_QRCODE_TEXT_MAX_LEN];
    int ttl_seconds;
    int status_only;
    int hide_countdown;
} rvt_qrcode_display_t;

static lv_obj_t *g_qrcode_screen;
static rvt_qrcode_ui_metrics_t g_qrcode_ui = {
    .disp_w = RVT_QRCODE_BASE_W,
    .disp_h = RVT_QRCODE_BASE_H,
};

static int32_t rvt_qrcode_scale_axis(int32_t value, int32_t current,
                                     int32_t base)
{
    return (int32_t)((int64_t)value * current / base);
}

static void rvt_qrcode_set_font(lv_obj_t *obj, const lv_font_t *font)
{
    if (obj != NULL && font != NULL) {
        lv_obj_set_style_text_font(obj, font, 0);
    }
}

/* 按接入应用传入的当前分辨率等比缩放 QR 弹层尺寸。 */
#define RVT_QRCODE_W(value) \
    rvt_qrcode_scale_axis((value), g_qrcode_ui.disp_w, RVT_QRCODE_BASE_W)
#define RVT_QRCODE_H(value) \
    rvt_qrcode_scale_axis((value), g_qrcode_ui.disp_h, RVT_QRCODE_BASE_H)
#define RVT_QRCODE_R(value) RVT_QRCODE_H(value)
#define RVT_QRCODE_COLOR_TEXT lv_color_hex(0xFFFFFF)
#define RVT_QRCODE_COLOR_ACCENT lv_color_hex(0xFF5722)

/* show/hide 可由业务线程调用；实际 LVGL 操作只在 rvt_qrcode_poll 所在线程执行。 */
static pthread_mutex_t g_qrcode_lock = PTHREAD_MUTEX_INITIALIZER;
static bool g_qrcode_pending;
static rvt_qrcode_action_t g_qrcode_pending_action;
static rvt_qrcode_display_t g_qrcode_pending_request;
/* 当前正在屏幕上显示的请求，用于倒计时和刷新前景层级。 */
static rvt_qrcode_display_t g_qrcode_display_request;
static bool g_qrcode_visible;
static uint32_t g_qrcode_start_tick;
static int g_qrcode_last_remaining = -1;
static lv_obj_t *g_qrcode_overlay;
static lv_obj_t *g_qrcode_left_panel;
static lv_obj_t *g_qrcode_right_panel;
static lv_obj_t *g_qrcode_obj;
static lv_obj_t *g_qrcode_title_label;
static lv_obj_t *g_qrcode_primary_label;
static lv_obj_t *g_qrcode_secondary_label;
static lv_obj_t *g_qrcode_countdown_label;
static lv_obj_t *g_qrcode_hint_label;

/*
 * 函数描述：按当前屏幕宽度缩放 800x480 基准 UI 尺寸。
 * 入参：
 *   value - 800 宽度基准下的尺寸。
 * 返回值：缩放后的像素尺寸，最小为 1。
 */
static int32_t rvt_qrcode_w(int32_t value)
{
    int32_t scaled = (value * g_qrcode_ui.disp_w) / RVT_QRCODE_BASE_W;

    return scaled > 0 ? scaled : 1;
}

/*
 * 函数描述：按当前屏幕高度缩放 800x480 基准 UI 尺寸。
 * 入参：
 *   value - 480 高度基准下的尺寸。
 * 返回值：缩放后的像素尺寸，最小为 1。
 */
static int32_t rvt_qrcode_h(int32_t value)
{
    int32_t scaled = (value * g_qrcode_ui.disp_h) / RVT_QRCODE_BASE_H;

    return scaled > 0 ? scaled : 1;
}

/*
 * 函数描述：根据业务类型返回默认二维码标题。
 * 入参：
 *   biz - 二维码业务类型。
 * 返回值：标题字符串常量。
 */
static const char *rvt_qrcode_default_title(rvt_qrcode_biz_t biz)
{
    switch (biz) {
        case RVT_QRCODE_BIZ_WIFI_PROVISIONING:
            return "WiFi 配网";
        case RVT_QRCODE_BIZ_AI_DEVICE:
            return "设备绑定";
        case RVT_QRCODE_BIZ_CUSTOM:
        default:
            return "二维码";
    }
}

/*
 * 函数描述：根据业务类型返回默认二维码提示文案。
 * 入参：
 *   biz - 二维码业务类型。
 * 返回值：提示文案字符串常量。
 */
static const char *rvt_qrcode_default_hint(rvt_qrcode_biz_t biz)
{
    switch (biz) {
        case RVT_QRCODE_BIZ_WIFI_PROVISIONING:
            return "请用手机 App 扫描二维码，或打开 WiFi 手动连接。";
        case RVT_QRCODE_BIZ_AI_DEVICE:
            return "请使用手机 App 扫描二维码完成绑定。";
        case RVT_QRCODE_BIZ_CUSTOM:
        default:
            return "请使用手机 App 扫描二维码。";
    }
}

/*
 * 函数描述：安全复制可为空的文本字段。
 * 入参：
 *   dst - 输出缓冲区。
 *   dst_len - 输出缓冲区长度。
 *   src - 输入字符串，可为 NULL。
 * 返回值：无。
 */
static void rvt_qrcode_copy_text(char *dst, size_t dst_len, const char *src)
{
    if (dst == NULL || dst_len == 0) {
        return;
    }

    if (src == NULL) {
        src = "";
    }

    snprintf(dst, dst_len, "%s", src);
}

/*
 * 函数描述：校验外部二维码请求，并复制成 UI 线程内部显示请求。
 * 入参：
 *   request - 外部业务传入的二维码请求。
 *   display - 输出内部显示请求副本。
 * 返回值：成功返回 0；参数非法或 payload 过长返回负 errno。
 */
static int rvt_qrcode_prepare_display_request(
    const rvt_qrcode_request_t *request,
    rvt_qrcode_display_t *display)
{
    size_t payload_len;

    if (request == NULL || display == NULL) {
        return -EINVAL;
    }

    if (!request->status_only) {
        if (request->payload == NULL) {
            return -EINVAL;
        }
        payload_len = strlen(request->payload);
        if (payload_len == 0) {
            return -EINVAL;
        }
        if (payload_len >= RVT_QRCODE_PAYLOAD_MAX_LEN) {
            return -ENOSPC;
        }
    }

    memset(display, 0, sizeof(*display));
    display->biz = request->biz;
    rvt_qrcode_copy_text(display->payload,
                         sizeof(display->payload),
                         request->payload);
    rvt_qrcode_copy_text(display->title,
                         sizeof(display->title),
                         request->title ?
                             request->title :
                             rvt_qrcode_default_title(request->biz));
    rvt_qrcode_copy_text(display->primary_text,
                         sizeof(display->primary_text),
                         request->primary_text);
    rvt_qrcode_copy_text(display->secondary_text,
                         sizeof(display->secondary_text),
                         request->secondary_text);
    rvt_qrcode_copy_text(display->hint_text,
                         sizeof(display->hint_text),
                         request->hint_text ?
                             request->hint_text :
                             rvt_qrcode_default_hint(request->biz));
    display->ttl_seconds = request->ttl_seconds;
    display->status_only = request->status_only;
    display->hide_countdown = request->hide_countdown;

    return 0;
}

/*
 * 函数描述：初始化 QR service，记录 LVGL 根屏幕和 UI 缩放参数。
 * 入参：
 *   screen - LVGL 根屏幕对象。
 *   ui - UI 缩放和字体参数，可为 NULL。
 * 返回值：无。
 */
void rvt_qrcode_init(lv_obj_t *screen, const rvt_qrcode_ui_metrics_t *ui)
{
    g_qrcode_screen = screen;

    if (ui != NULL) {
        g_qrcode_ui = *ui;
    }

    if (g_qrcode_ui.disp_w <= 0) {
        g_qrcode_ui.disp_w = RVT_QRCODE_BASE_W;
    }
    if (g_qrcode_ui.disp_h <= 0) {
        g_qrcode_ui.disp_h = RVT_QRCODE_BASE_H;
    }
}

/*
 * 函数描述：提交二维码显示请求。该函数只入队，不直接操作 LVGL。
 * 入参：
 *   request - 二维码 payload、标题、说明文本和倒计时配置。
 * 返回值：成功返回 0；请求非法返回负 errno。
 */
int rvt_qrcode_show(const rvt_qrcode_request_t *request)
{
    rvt_qrcode_display_t display;
    int ret;

    ret = rvt_qrcode_prepare_display_request(request, &display);
    if (ret < 0) {
        return ret;
    }

    pthread_mutex_lock(&g_qrcode_lock);
    g_qrcode_pending_action = RVT_QRCODE_ACTION_SHOW;
    g_qrcode_pending_request = display;
    g_qrcode_pending = true;
    pthread_mutex_unlock(&g_qrcode_lock);

    return 0;
}

/*
 * 函数描述：提交二维码隐藏请求。该函数只入队，不直接操作 LVGL。
 * 入参：无。
 * 返回值：固定返回 0。
 */
int rvt_qrcode_hide(void)
{
    pthread_mutex_lock(&g_qrcode_lock);
    g_qrcode_pending_action = RVT_QRCODE_ACTION_HIDE;
    g_qrcode_pending = true;
    pthread_mutex_unlock(&g_qrcode_lock);

    return 0;
}

/*
 * 函数描述：返回两个 32 位整数中的较小值。
 * 入参：
 *   a - 第一个值。
 *   b - 第二个值。
 * 返回值：a 和 b 中较小的一个。
 */
static int32_t rvt_qrcode_min_i32(int32_t a, int32_t b)
{
    return a < b ? a : b;
}

/*
 * 函数描述：设置二维码弹层中的标签文本，并按文本是否为空自动隐藏。
 * 入参：
 *   label - LVGL 标签对象。
 *   text - 标签文本，可为 NULL。
 *   wrap - 是否启用自动换行。
 * 返回值：无。
 */
static void rvt_qrcode_set_label(lv_obj_t *label, const char *text, bool wrap)
{
    if (label == NULL) {
        return;
    }

    if (text == NULL || text[0] == '\0') {
        lv_label_set_text(label, "");
        lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_label_set_long_mode(label, wrap ? LV_LABEL_LONG_WRAP : LV_LABEL_LONG_CLIP);
    lv_label_set_text(label, text);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
}

/*
 * 函数描述：更新二维码倒计时标签，避免同一秒重复刷新。
 * 入参：
 *   remaining - 剩余秒数。
 * 返回值：无。
 */
static void rvt_qrcode_update_countdown(int remaining)
{
    char text[40];

    if (remaining < 0) {
        remaining = 0;
    }

    if (remaining == g_qrcode_last_remaining) {
        return;
    }

    g_qrcode_last_remaining = remaining;
    snprintf(text, sizeof(text), "剩余 %d 秒", remaining);
    if (g_qrcode_countdown_label != NULL) {
        lv_label_set_text(g_qrcode_countdown_label, text);
    }
}

/*
 * 函数描述：创建顶层二维码遮罩、卡片、二维码对象和说明文本控件。
 * 入参：无。
 * 返回值：无。
 */
static void rvt_qrcode_create_overlay(void)
{
    lv_obj_t *parent;
    lv_obj_t *card;
    lv_obj_t *content;
    int32_t qr_size;

    if (g_qrcode_overlay != NULL) {
        return;
    }

    parent = lv_layer_top();
    if (parent == NULL) {
        parent = g_qrcode_screen;
    }
    if (parent == NULL) {
        return;
    }

    g_qrcode_overlay = lv_obj_create(parent);
    lv_obj_remove_style_all(g_qrcode_overlay);
    lv_obj_set_size(g_qrcode_overlay,
                    g_qrcode_ui.disp_w,
                    g_qrcode_ui.disp_h);
    lv_obj_align(g_qrcode_overlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(g_qrcode_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(g_qrcode_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(g_qrcode_overlay, LV_OPA_70, 0);

    card = lv_obj_create(g_qrcode_overlay);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, RVT_QRCODE_W(620), RVT_QRCODE_H(352));
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x111820), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_100, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x3B4652), 0);
    lv_obj_set_style_border_width(card, RVT_QRCODE_W(2), 0);
    lv_obj_set_style_radius(card, RVT_QRCODE_R(8), 0);
    lv_obj_set_style_pad_all(card, RVT_QRCODE_W(22), 0);

    content = lv_obj_create(card);
    lv_obj_remove_style_all(content);
    lv_obj_set_size(content, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(content,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(content, RVT_QRCODE_W(26), 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    g_qrcode_left_panel = lv_obj_create(content);
    lv_obj_remove_style_all(g_qrcode_left_panel);
    lv_obj_set_size(g_qrcode_left_panel, RVT_QRCODE_W(250), RVT_QRCODE_H(300));
    lv_obj_set_flex_flow(g_qrcode_left_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_qrcode_left_panel,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(g_qrcode_left_panel, RVT_QRCODE_H(8), 0);
    lv_obj_clear_flag(g_qrcode_left_panel, LV_OBJ_FLAG_SCROLLABLE);

    qr_size = rvt_qrcode_min_i32(RVT_QRCODE_W(190), RVT_QRCODE_H(190));
    g_qrcode_obj = lv_qrcode_create(g_qrcode_left_panel);
    if (g_qrcode_obj == NULL) {
        lv_obj_del(g_qrcode_overlay);
        g_qrcode_overlay = NULL;
        return;
    }

    lv_qrcode_set_size(g_qrcode_obj, qr_size);
    lv_qrcode_set_dark_color(g_qrcode_obj, lv_color_hex(0x111820));
    lv_qrcode_set_light_color(g_qrcode_obj, lv_color_hex(0xFFFFFF));
    lv_qrcode_set_quiet_zone(g_qrcode_obj, true);
    lv_obj_set_style_border_color(g_qrcode_obj, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(g_qrcode_obj, RVT_QRCODE_W(8), 0);

    g_qrcode_primary_label = lv_label_create(g_qrcode_left_panel);
    lv_obj_set_width(g_qrcode_primary_label, RVT_QRCODE_W(250));
    lv_obj_set_style_text_color(g_qrcode_primary_label,
                                RVT_QRCODE_COLOR_TEXT,
                                0);
    rvt_qrcode_set_font(g_qrcode_primary_label, g_qrcode_ui.font_16);
    lv_obj_set_style_text_align(g_qrcode_primary_label,
                                LV_TEXT_ALIGN_CENTER,
                                0);
    lv_label_set_long_mode(g_qrcode_primary_label, LV_LABEL_LONG_CLIP);
    lv_label_set_text(g_qrcode_primary_label, "");
    lv_obj_add_flag(g_qrcode_primary_label, LV_OBJ_FLAG_HIDDEN);

    g_qrcode_secondary_label = lv_label_create(g_qrcode_left_panel);
    lv_obj_set_width(g_qrcode_secondary_label, RVT_QRCODE_W(250));
    lv_obj_set_style_text_color(g_qrcode_secondary_label,
                                RVT_QRCODE_COLOR_TEXT,
                                0);
    rvt_qrcode_set_font(g_qrcode_secondary_label, g_qrcode_ui.font_16);
    lv_obj_set_style_text_align(g_qrcode_secondary_label,
                                LV_TEXT_ALIGN_CENTER,
                                0);
    lv_label_set_long_mode(g_qrcode_secondary_label, LV_LABEL_LONG_CLIP);
    lv_label_set_text(g_qrcode_secondary_label, "");
    lv_obj_add_flag(g_qrcode_secondary_label, LV_OBJ_FLAG_HIDDEN);

    g_qrcode_right_panel = lv_obj_create(content);
    lv_obj_remove_style_all(g_qrcode_right_panel);
    lv_obj_set_size(g_qrcode_right_panel, RVT_QRCODE_W(294), RVT_QRCODE_H(300));
    lv_obj_set_flex_flow(g_qrcode_right_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_qrcode_right_panel,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_gap(g_qrcode_right_panel, RVT_QRCODE_H(18), 0);
    lv_obj_clear_flag(g_qrcode_right_panel, LV_OBJ_FLAG_SCROLLABLE);

    g_qrcode_title_label = lv_label_create(g_qrcode_right_panel);
    lv_obj_set_width(g_qrcode_title_label, RVT_QRCODE_W(294));
    lv_obj_set_style_text_color(g_qrcode_title_label, RVT_QRCODE_COLOR_TEXT, 0);
    rvt_qrcode_set_font(g_qrcode_title_label, g_qrcode_ui.font_40);
    lv_obj_set_style_text_align(g_qrcode_title_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_text(g_qrcode_title_label, "二维码");

    g_qrcode_countdown_label = lv_label_create(g_qrcode_right_panel);
    lv_obj_set_width(g_qrcode_countdown_label, RVT_QRCODE_W(294));
    lv_obj_set_style_text_color(g_qrcode_countdown_label,
                                RVT_QRCODE_COLOR_ACCENT,
                                0);
    rvt_qrcode_set_font(g_qrcode_countdown_label, g_qrcode_ui.font_28);
    lv_obj_set_style_text_align(g_qrcode_countdown_label,
                                LV_TEXT_ALIGN_LEFT,
                                0);
    lv_label_set_text(g_qrcode_countdown_label, "");
    lv_obj_add_flag(g_qrcode_countdown_label, LV_OBJ_FLAG_HIDDEN);

    g_qrcode_hint_label = lv_label_create(g_qrcode_right_panel);
    lv_obj_set_width(g_qrcode_hint_label, RVT_QRCODE_W(294));
    lv_obj_set_style_text_color(g_qrcode_hint_label, lv_color_hex(0xD7DEE6), 0);
    rvt_qrcode_set_font(g_qrcode_hint_label, g_qrcode_ui.font_20);
    lv_obj_set_style_text_align(g_qrcode_hint_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(g_qrcode_hint_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(g_qrcode_hint_label, "");

    lv_obj_add_flag(g_qrcode_overlay, LV_OBJ_FLAG_HIDDEN);
}

/*
 * 函数描述：隐藏二维码弹层并重置倒计时状态。
 * 入参：无。
 * 返回值：无。
 */
static void rvt_qrcode_hide_overlay(void)
{
    g_qrcode_visible = false;
    g_qrcode_last_remaining = -1;

    if (g_qrcode_overlay != NULL) {
        lv_obj_add_flag(g_qrcode_overlay, LV_OBJ_FLAG_HIDDEN);
    }
}

/*
 * 函数描述：把内部显示请求渲染到 LVGL 顶层二维码弹层。
 * 入参：
 *   display - 已校验和复制过的二维码显示请求。
 * 返回值：无。
 */
static void rvt_qrcode_show_overlay(const rvt_qrcode_display_t *display)
{
    bool status_only;

    if (display == NULL || (!display->status_only && display->payload[0] == '\0')) {
        return;
    }

    rvt_qrcode_create_overlay();
    if (g_qrcode_overlay == NULL || g_qrcode_obj == NULL) {
        return;
    }

    g_qrcode_display_request = *display;
    g_qrcode_start_tick = lv_tick_get();
    g_qrcode_last_remaining = -1;
    status_only = display->status_only ? true : false;

    if (g_qrcode_left_panel != NULL) {
        if (status_only) {
            lv_obj_add_flag(g_qrcode_left_panel, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(g_qrcode_left_panel, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (g_qrcode_right_panel != NULL) {
        lv_obj_set_width(g_qrcode_right_panel,
                         status_only ? RVT_QRCODE_W(520) : RVT_QRCODE_W(294));
        lv_obj_set_flex_align(g_qrcode_right_panel,
                              LV_FLEX_ALIGN_CENTER,
                              status_only ? LV_FLEX_ALIGN_CENTER :
                                            LV_FLEX_ALIGN_START,
                              status_only ? LV_FLEX_ALIGN_CENTER :
                                            LV_FLEX_ALIGN_START);
    }

    if (!status_only) {
        lv_qrcode_update(g_qrcode_obj,
                         g_qrcode_display_request.payload,
                         strlen(g_qrcode_display_request.payload));
    }
    rvt_qrcode_set_label(g_qrcode_title_label,
                         g_qrcode_display_request.title,
                         false);
    rvt_qrcode_set_label(g_qrcode_primary_label,
                         g_qrcode_display_request.primary_text,
                         false);
    rvt_qrcode_set_label(g_qrcode_secondary_label,
                         g_qrcode_display_request.secondary_text,
                         false);
    rvt_qrcode_set_label(g_qrcode_hint_label,
                         g_qrcode_display_request.hint_text,
                         true);

    if (g_qrcode_title_label != NULL) {
        lv_obj_set_width(g_qrcode_title_label,
                         status_only ? RVT_QRCODE_W(520) : RVT_QRCODE_W(294));
        lv_obj_set_style_text_align(g_qrcode_title_label,
                                    status_only ? LV_TEXT_ALIGN_CENTER :
                                                  LV_TEXT_ALIGN_LEFT,
                                    0);
    }
    if (g_qrcode_countdown_label != NULL) {
        lv_obj_set_width(g_qrcode_countdown_label,
                         status_only ? RVT_QRCODE_W(520) : RVT_QRCODE_W(294));
        lv_obj_set_style_text_align(g_qrcode_countdown_label,
                                    status_only ? LV_TEXT_ALIGN_CENTER :
                                                  LV_TEXT_ALIGN_LEFT,
                                    0);
    }
    if (g_qrcode_hint_label != NULL) {
        lv_obj_set_width(g_qrcode_hint_label,
                         status_only ? RVT_QRCODE_W(520) : RVT_QRCODE_W(294));
        lv_obj_set_style_text_align(g_qrcode_hint_label,
                                    status_only ? LV_TEXT_ALIGN_CENTER :
                                                  LV_TEXT_ALIGN_LEFT,
                                    0);
    }

    if (g_qrcode_display_request.ttl_seconds > 0) {
        if (g_qrcode_countdown_label != NULL &&
            !g_qrcode_display_request.hide_countdown) {
            lv_obj_clear_flag(g_qrcode_countdown_label, LV_OBJ_FLAG_HIDDEN);
            rvt_qrcode_update_countdown(g_qrcode_display_request.ttl_seconds);
        } else if (g_qrcode_countdown_label != NULL) {
            lv_label_set_text(g_qrcode_countdown_label, "");
            lv_obj_add_flag(g_qrcode_countdown_label, LV_OBJ_FLAG_HIDDEN);
        }
    } else if (g_qrcode_countdown_label != NULL) {
        lv_label_set_text(g_qrcode_countdown_label, "");
        lv_obj_add_flag(g_qrcode_countdown_label, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_clear_flag(g_qrcode_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_qrcode_overlay);
    g_qrcode_visible = true;
}

/*
 * 函数描述：在 LVGL UI 线程中处理二维码 show/hide 请求、倒计时和顶层保持。
 * 入参：无。
 * 返回值：无。
 */
void rvt_qrcode_poll(void)
{
    rvt_qrcode_action_t action;
    rvt_qrcode_display_t request;
    bool pending;
    int remaining;
    uint32_t elapsed_ms;

    pthread_mutex_lock(&g_qrcode_lock);
    pending = g_qrcode_pending;
    if (pending) {
        action = g_qrcode_pending_action;
        request = g_qrcode_pending_request;
        g_qrcode_pending = false;
    }
    pthread_mutex_unlock(&g_qrcode_lock);

    if (pending) {
        if (action == RVT_QRCODE_ACTION_SHOW) {
            rvt_qrcode_show_overlay(&request);
        } else {
            rvt_qrcode_hide_overlay();
        }
    }

    if (!g_qrcode_visible) {
        return;
    }

    if (g_qrcode_display_request.ttl_seconds > 0) {
        elapsed_ms = lv_tick_elaps(g_qrcode_start_tick);
        remaining = g_qrcode_display_request.ttl_seconds -
                    (int)((elapsed_ms + 999U) / 1000U);
        if (!g_qrcode_display_request.hide_countdown) {
            rvt_qrcode_update_countdown(remaining);
        }

        if (remaining <= 0) {
            rvt_qrcode_hide_overlay();
            return;
        }
    }

    lv_obj_move_foreground(g_qrcode_overlay);
}

#else

/*
 * 函数描述：LVGL QR 未启用时的初始化空实现。
 * 入参：
 *   screen - LVGL 根屏幕对象，未使用。
 *   ui - UI 缩放和字体参数，未使用。
 * 返回值：无。
 */
void rvt_qrcode_init(lv_obj_t *screen, const rvt_qrcode_ui_metrics_t *ui)
{
    (void)screen;
    (void)ui;
}

/*
 * 函数描述：LVGL QR 未启用时的显示空实现。
 * 入参：
 *   request - 二维码请求，未使用。
 * 返回值：固定返回 -ENOSYS。
 */
int rvt_qrcode_show(const rvt_qrcode_request_t *request)
{
    (void)request;
    return -ENOSYS;
}

/*
 * 函数描述：LVGL QR 未启用时的隐藏空实现。
 * 入参：无。
 * 返回值：固定返回 -ENOSYS。
 */
int rvt_qrcode_hide(void)
{
    return -ENOSYS;
}

/*
 * 函数描述：LVGL QR 未启用时的轮询空实现。
 * 入参：无。
 * 返回值：无。
 */
void rvt_qrcode_poll(void)
{
}

#endif
