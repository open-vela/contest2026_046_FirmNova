#include <unistd.h>
#include <stdint.h>
#include <lvgl/lvgl.h>
#include "scooterdemo_settings_subpages.h"

#define COLOR_TEXT lv_color_hex(0xFFFFFF)
#define COLOR_TEXT_DARK lv_color_hex(0x111111)
#define COLOR_HIGHLIGHT lv_color_hex(0x4BB2FF)
#define COLOR_ROW_BG lv_color_hex(0x505050)
#define COLOR_SEG_BG lv_color_hex(0x5A5A5A)
#define COLOR_SEG_OFF lv_color_hex(0x5A5A5A)
#define COLOR_SWITCH_TRACK lv_color_hex(0x585858)
#define COLOR_SWITCH_THUMB lv_color_hex(0xFFFFFF)

typedef struct {
    lv_obj_t *root;
    lv_obj_t *thumb;
    lv_obj_t *symbol;
} custom_switch_t;

static scooterdemo_system_settings_state_t g_system_state = {
    .bt_enabled = true,
    .use_24h_clock = false,
    .audio_route = SCOOTERDEMO_SYSTEM_AUDIO_ROUTE_CLUSTER,
};
static scooterdemo_system_settings_callbacks_t g_system_callbacks;
static void *g_system_callbacks_user_data;

static lv_obj_t *g_system_rows[3];
static lv_obj_t *g_system_row_labels[3];
static custom_switch_t g_sw_bt;
static custom_switch_t g_sw_time;
static lv_obj_t *g_btn_cluster;
static lv_obj_t *g_btn_phone;
static lv_obj_t *g_lbl_cluster;
static lv_obj_t *g_lbl_phone;
static int g_system_focus_index;

/* 设置系统设置行样式。 */
static void style_row(lv_obj_t *row)
{
    lv_obj_set_style_radius(row, 10, 0);
    lv_obj_set_style_bg_color(row, COLOR_ROW_BG, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_70, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_outline_width(row, 0, 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_pad_left(row, 14, 0);
    lv_obj_set_style_pad_right(row, 12, 0);
    lv_obj_set_style_pad_top(row, 0, 0);
    lv_obj_set_style_pad_bottom(row, 0, 0);
    lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
}

/* 设置系统子页开关样式。 */
static void style_switch(custom_switch_t *sw)
{
    lv_obj_set_size(sw->root, 60, 28);
    lv_obj_set_style_bg_color(sw->root, COLOR_SWITCH_TRACK, 0);
    lv_obj_set_style_bg_opa(sw->root, LV_OPA_100, 0);
    lv_obj_set_style_border_width(sw->root, 0, 0);
    lv_obj_set_style_outline_width(sw->root, 0, 0);
    lv_obj_set_style_shadow_width(sw->root, 0, 0);
    lv_obj_set_style_radius(sw->root, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(sw->root, 2, 0);
    lv_obj_clear_flag(sw->root, LV_OBJ_FLAG_SCROLLABLE);

    sw->thumb = lv_obj_create(sw->root);
    lv_obj_set_size(sw->thumb, 28, 24);
    lv_obj_set_style_bg_color(sw->thumb, COLOR_SWITCH_THUMB, 0);
    lv_obj_set_style_bg_opa(sw->thumb, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sw->thumb, 0, 0);
    lv_obj_set_style_outline_width(sw->thumb, 0, 0);
    lv_obj_set_style_shadow_width(sw->thumb, 0, 0);
    lv_obj_set_style_radius(sw->thumb, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(sw->thumb, LV_OBJ_FLAG_SCROLLABLE);

    sw->symbol = lv_label_create(sw->thumb);
    lv_label_set_text(sw->symbol, "-");
    lv_obj_set_style_text_color(sw->symbol, COLOR_TEXT_DARK, 0);
    lv_obj_center(sw->symbol);
}

static void set_switch_state(custom_switch_t *sw, bool enabled)
{
    if (sw->root == NULL || sw->thumb == NULL) {
        return;
    }

    lv_obj_align(sw->thumb, enabled ? LV_ALIGN_RIGHT_MID : LV_ALIGN_LEFT_MID, 0, 0);
}

/* 设置音频通道路由按钮样式。 */
static void style_route_button(lv_obj_t *btn, bool selected)
{
    lv_obj_set_style_bg_color(btn, selected ? COLOR_SWITCH_THUMB : COLOR_SEG_OFF, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_100, 0);
    lv_obj_set_style_radius(btn, 14, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_outline_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);

    lv_obj_t *label = lv_obj_get_child(btn, 0);
    if (label != NULL) {
        lv_obj_set_style_text_color(label, selected ? COLOR_TEXT_DARK : COLOR_TEXT, 0);
    }
}

/* 根据焦点索引刷新系统设置行高亮。 */
static void update_system_focus_highlight(void)
{
    for (int i = 0; i < 3; i++) {
        if (g_system_rows[i] == NULL) {
            continue;
        }

        if (g_system_focus_index >= 0 && i == g_system_focus_index) {
            lv_obj_set_style_bg_color(g_system_rows[i], COLOR_SWITCH_THUMB, 0);
            lv_obj_set_style_bg_opa(g_system_rows[i], LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(g_system_rows[i], 0, 0);
            lv_obj_set_style_radius(g_system_rows[i], 20, 0);
            if (g_system_row_labels[i] != NULL) {
                lv_obj_set_style_text_color(g_system_row_labels[i], COLOR_TEXT_DARK, 0);
            }
        } else {
            lv_obj_set_style_bg_color(g_system_rows[i], COLOR_ROW_BG, 0);
            lv_obj_set_style_bg_opa(g_system_rows[i], LV_OPA_70, 0);
            lv_obj_set_style_border_width(g_system_rows[i], 0, 0);
            if (g_system_row_labels[i] != NULL) {
                lv_obj_set_style_text_color(g_system_row_labels[i], COLOR_TEXT, 0);
            }
        }
    }
}


/* 根据当前系统状态刷新所有控件。 */
static void apply_system_state_to_ui(void)
{
    set_switch_state(&g_sw_bt, g_system_state.bt_enabled);
    set_switch_state(&g_sw_time, g_system_state.use_24h_clock);

    if (g_btn_cluster != NULL) {
        style_route_button(g_btn_cluster,
                           g_system_state.audio_route == SCOOTERDEMO_SYSTEM_AUDIO_ROUTE_CLUSTER);
    }

    if (g_btn_phone != NULL) {
        style_route_button(g_btn_phone,
                           g_system_state.audio_route == SCOOTERDEMO_SYSTEM_AUDIO_ROUTE_PHONE);
    }
}


/* 切换当前焦点对应的系统设置项。 */
static void toggle_system_focused_control(void)
{
    if (g_system_focus_index == 0) {
        g_system_state.bt_enabled = !g_system_state.bt_enabled;
        apply_system_state_to_ui();
        if (g_system_callbacks.on_bt_switch_changed != NULL) {
            g_system_callbacks.on_bt_switch_changed(g_system_state.bt_enabled,
                                                    g_system_callbacks_user_data);
        }
    } else if (g_system_focus_index == 1) {
        g_system_state.audio_route =
            (g_system_state.audio_route == SCOOTERDEMO_SYSTEM_AUDIO_ROUTE_CLUSTER)
                ? SCOOTERDEMO_SYSTEM_AUDIO_ROUTE_PHONE
                : SCOOTERDEMO_SYSTEM_AUDIO_ROUTE_CLUSTER;
        apply_system_state_to_ui();
        if (g_system_callbacks.on_audio_route_changed != NULL) {
            g_system_callbacks.on_audio_route_changed(g_system_state.audio_route,
                                                      g_system_callbacks_user_data);
        }
    } else {
        g_system_state.use_24h_clock = !g_system_state.use_24h_clock;
        apply_system_state_to_ui();
        if (g_system_callbacks.on_clock_mode_changed != NULL) {
            g_system_callbacks.on_clock_mode_changed(g_system_state.use_24h_clock,
                                                     g_system_callbacks_user_data);
        }
    }
}

/*
 * 处理蓝牙开关事件。
 * 参数 e: LVGL 事件对象。
 */
static void bt_switch_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    g_system_state.bt_enabled = !g_system_state.bt_enabled;
    apply_system_state_to_ui();
    if (g_system_callbacks.on_bt_switch_changed != NULL) {
        g_system_callbacks.on_bt_switch_changed(g_system_state.bt_enabled,
                                                g_system_callbacks_user_data);
    }
}

/*
 * 处理时制开关事件。
 * 参数 e: LVGL 事件对象。
 */
static void time_switch_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    g_system_state.use_24h_clock = !g_system_state.use_24h_clock;
    apply_system_state_to_ui();
    if (g_system_callbacks.on_clock_mode_changed != NULL) {
        g_system_callbacks.on_clock_mode_changed(g_system_state.use_24h_clock,
                                                 g_system_callbacks_user_data);
    }
}

/*
 * 处理音频通道路由按钮事件。
 * 参数 e: LVGL 事件对象，user_data 为路由枚举。
 */
static void route_button_event_cb(lv_event_t *e)
{
    scooterdemo_system_audio_route_t route;

    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    route = (scooterdemo_system_audio_route_t)(intptr_t)lv_event_get_user_data(e);
    if (g_system_state.audio_route == route) {
        return;
    }

    g_system_state.audio_route = route;
    apply_system_state_to_ui();

    if (g_system_callbacks.on_audio_route_changed != NULL) {
        g_system_callbacks.on_audio_route_changed(g_system_state.audio_route,
                                                  g_system_callbacks_user_data);
    }
}

/*
 * 创建系统设置行。
 * 参数 parent: 行父容器。
 * 参数 ui: UI 缩放与字体参数。
 * 参数 title: 行标题。
 * 参数 y: 纵向偏移。
 * 返回值: 创建的行对象。
 */
static lv_obj_t *create_setting_row(lv_obj_t *parent,
                                    const scooterdemo_ui_metrics_t *ui,
                                    const char *title,
                                    int y,
                                    int row_index)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, SCOOTERDEMO_W(ui, 560), SCOOTERDEMO_H(ui, 52));
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, SCOOTERDEMO_H(ui, y));
    style_row(row);

    lv_obj_t *label = lv_label_create(row);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_font(label, ui->font_20, 0);
    lv_obj_set_style_text_color(label, COLOR_TEXT, 0);
    if (row_index >= 0 && row_index < 3) {
        g_system_row_labels[row_index] = label;
    }

    return row;
}

/*
 * 构建系统设置子页。
 * 参数 parent: 子页父容器。
 * 参数 ui: UI 缩放与字体参数。
 * 返回值: 子页根对象。
 */
lv_obj_t *scooterdemo_settings_subpage_system_build(lv_obj_t *parent,
                                                    const scooterdemo_ui_metrics_t *ui)
{
    lv_obj_t *container = lv_obj_create(parent);
    lv_obj_set_size(container, SCOOTERDEMO_W(ui, 585), SCOOTERDEMO_H(ui, 360));
    lv_obj_align(container, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(container, LV_OPA_0, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_outline_width(container, 0, 0);
    lv_obj_set_style_shadow_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_set_scrollbar_mode(container, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *row_bt_switch = create_setting_row(container, ui, "蓝牙开关", 42, 0);
    g_system_rows[0] = row_bt_switch;
    g_sw_bt.root = lv_btn_create(row_bt_switch);
    style_switch(&g_sw_bt);
    lv_obj_add_event_cb(g_sw_bt.root, bt_switch_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *row_bt_route = create_setting_row(container, ui, "蓝牙音频通道", 108, 1);
    g_system_rows[1] = row_bt_route;
    lv_obj_t *seg = lv_obj_create(row_bt_route);
    lv_obj_set_size(seg, SCOOTERDEMO_W(ui, 122), SCOOTERDEMO_H(ui, 34));
    lv_obj_set_style_bg_color(seg, COLOR_SEG_BG, 0);
    lv_obj_set_style_bg_opa(seg, LV_OPA_100, 0);
    lv_obj_set_style_radius(seg, 16, 0);
    lv_obj_set_style_border_width(seg, 0, 0);
    lv_obj_set_style_pad_all(seg, 2, 0);
    lv_obj_set_scrollbar_mode(seg, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(seg, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(seg, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    g_btn_cluster = lv_btn_create(seg);
    lv_obj_set_size(g_btn_cluster, SCOOTERDEMO_W(ui, 56), SCOOTERDEMO_H(ui, 30));
    lv_obj_add_event_cb(g_btn_cluster,
                        route_button_event_cb,
                        LV_EVENT_CLICKED,
                        (void *)(intptr_t)SCOOTERDEMO_SYSTEM_AUDIO_ROUTE_CLUSTER);
    g_lbl_cluster = lv_label_create(g_btn_cluster);
    lv_label_set_text(g_lbl_cluster, "仪表");
    lv_obj_set_style_text_font(g_lbl_cluster, ui->font_16, 0);
    lv_obj_set_style_text_color(g_lbl_cluster, COLOR_TEXT, 0);
    lv_obj_center(g_lbl_cluster);

    g_btn_phone = lv_btn_create(seg);
    lv_obj_set_size(g_btn_phone, SCOOTERDEMO_W(ui, 56), SCOOTERDEMO_H(ui, 30));
    lv_obj_add_event_cb(g_btn_phone,
                        route_button_event_cb,
                        LV_EVENT_CLICKED,
                        (void *)(intptr_t)SCOOTERDEMO_SYSTEM_AUDIO_ROUTE_PHONE);
    g_lbl_phone = lv_label_create(g_btn_phone);
    lv_label_set_text(g_lbl_phone, "手机");
    lv_obj_set_style_text_font(g_lbl_phone, ui->font_16, 0);
    lv_obj_set_style_text_color(g_lbl_phone, COLOR_TEXT, 0);
    lv_obj_center(g_lbl_phone);

    lv_obj_t *row_time = create_setting_row(container, ui, "时制切换", 174, 2);
    g_system_rows[2] = row_time;
    g_sw_time.root = lv_btn_create(row_time);
    style_switch(&g_sw_time);
    lv_obj_add_event_cb(g_sw_time.root, time_switch_event_cb, LV_EVENT_CLICKED, NULL);

    g_system_focus_index = -1;
    update_system_focus_highlight();
    apply_system_state_to_ui();

    return container;
}

/*
 * 设置系统子页回调。
 * 参数 callbacks: 回调函数集合，传 NULL 表示清空回调。
 * 参数 user_data: 回调透传上下文。
 */
void scooterdemo_settings_subpage_system_set_callbacks(
    const scooterdemo_system_settings_callbacks_t *callbacks,
    void *user_data)
{
    if (callbacks != NULL) {
        g_system_callbacks = *callbacks;
    } else {
        g_system_callbacks.on_bt_switch_changed = NULL;
        g_system_callbacks.on_audio_route_changed = NULL;
        g_system_callbacks.on_clock_mode_changed = NULL;
    }

    g_system_callbacks_user_data = user_data;
}

/*
 * 设置系统子页状态并刷新显示。
 * 参数 state: 新状态，传 NULL 则忽略。
 */
void scooterdemo_settings_subpage_system_set_state(
    const scooterdemo_system_settings_state_t *state)
{
    if (state == NULL) {
        return;
    }

    g_system_state = *state;
    if (g_system_state.audio_route != SCOOTERDEMO_SYSTEM_AUDIO_ROUTE_PHONE) {
        g_system_state.audio_route = SCOOTERDEMO_SYSTEM_AUDIO_ROUTE_CLUSTER;
    }

    apply_system_state_to_ui();
}

/*
 * 获取系统子页当前状态。
 * 参数 state: 输出状态指针，传 NULL 则忽略。
 */
void scooterdemo_settings_subpage_system_get_state(
    scooterdemo_system_settings_state_t *state)
{
    if (state == NULL) {
        return;
    }

    *state = g_system_state;
}

/* 释放系统子页静态对象引用。 */
void scooterdemo_settings_subpage_system_release(void)
{
    for (int i = 0; i < 3; i++) {
        g_system_rows[i] = NULL;
        g_system_row_labels[i] = NULL;
    }

    g_sw_bt.root = NULL;
    g_sw_bt.thumb = NULL;
    g_sw_bt.symbol = NULL;
    g_sw_time.root = NULL;
    g_sw_time.thumb = NULL;
    g_sw_time.symbol = NULL;
    g_btn_cluster = NULL;
    g_btn_phone = NULL;
    g_lbl_cluster = NULL;
    g_lbl_phone = NULL;
    g_system_focus_index = -1;
}

/* 处理系统子页向上导航。 */
bool scooterdemo_settings_subpage_system_handle_nav_up(void)
{
    if (g_system_focus_index < 0) {
        g_system_focus_index = 2;
        update_system_focus_highlight();
        return true;
    }

    g_system_focus_index--;
    if (g_system_focus_index < 0) {
        g_system_focus_index = 2;
    }

    update_system_focus_highlight();
    return true;
}

/* 处理系统子页向下导航。 */
bool scooterdemo_settings_subpage_system_handle_nav_down(void)
{
    if (g_system_focus_index < 0) {
        g_system_focus_index = 0;
        update_system_focus_highlight();
        return true;
    }

    g_system_focus_index++;
    if (g_system_focus_index > 2) {
        g_system_focus_index = 0;
    }

    update_system_focus_highlight();
    return true;
}

/*
 * 设置系统子页焦点状态。
 * 参数 focused: true 表示进入子页焦点，false 表示清除子页焦点。
 */
void scooterdemo_settings_subpage_system_set_focus(bool focused)
{
    if (focused) {
        if (g_system_focus_index < 0) {
            g_system_focus_index = 0;
        }
    } else {
        g_system_focus_index = -1;
    }

    update_system_focus_highlight();
}

/* 处理系统子页确认事件。 */
bool scooterdemo_settings_subpage_system_handle_enter(void)
{
    if (g_system_focus_index < 0) {
        return false;
    }

    toggle_system_focused_control();
    return true;
}

/* 处理系统子页双击事件。 */
bool scooterdemo_settings_subpage_system_handle_double_click(void)
{
    toggle_system_focused_control();
    return true;
}
