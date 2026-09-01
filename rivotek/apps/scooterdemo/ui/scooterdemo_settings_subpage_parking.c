#include <stdint.h>

#include <lvgl/lvgl.h>

#include "scooterdemo_settings_subpages.h"

#define COLOR_TEXT lv_color_hex(0xFFFFFF)
#define COLOR_TEXT_DARK lv_color_hex(0x111111)
#define COLOR_HIGHLIGHT lv_color_hex(0x4BB2FF)
#define COLOR_ROW_BG lv_color_hex(0x505050)
#define COLOR_SWITCH_TRACK lv_color_hex(0x585858)
#define COLOR_SWITCH_THUMB lv_color_hex(0xFFFFFF)

typedef struct {
    lv_obj_t *root;
    lv_obj_t *thumb;
    lv_obj_t *symbol;
} custom_switch_t;

static const char *g_parking_switch_labels[SCOOTERDEMO_PARKING_SWITCH_COUNT] = {
    "边撑感应开关",
    "自动大灯开关",
    "TCS开关",
    "陡坡缓降开关",
    "坡道驻停开关",
};

static scooterdemo_parking_settings_state_t g_parking_state = {
    .side_stand_enabled = false,
    .auto_headlight_enabled = false,
    .tcs_enabled = false,
    .hdc_enabled = true,
    .hill_hold_enabled = false,
};

static scooterdemo_parking_settings_callbacks_t g_parking_callbacks;
static void *g_parking_callbacks_user_data;
static lv_obj_t *g_parking_rows[SCOOTERDEMO_PARKING_SWITCH_COUNT];
static lv_obj_t *g_parking_row_labels[SCOOTERDEMO_PARKING_SWITCH_COUNT];
static custom_switch_t g_parking_switches[SCOOTERDEMO_PARKING_SWITCH_COUNT];
static int g_parking_focus_index;

/* 设置驻车行容器样式。 */
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

/* 设置驻车开关样式。 */
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

/*
 * 读取指定驻车开关状态。
 * 参数 id: 开关枚举。
 * 返回值: 开关是否开启。
 */
static bool parking_state_get_by_id(scooterdemo_parking_switch_id_t id)
{
    switch (id) {
    case SCOOTERDEMO_PARKING_SWITCH_SIDE_STAND:
        return g_parking_state.side_stand_enabled;
    case SCOOTERDEMO_PARKING_SWITCH_AUTO_HEADLIGHT:
        return g_parking_state.auto_headlight_enabled;
    case SCOOTERDEMO_PARKING_SWITCH_TCS:
        return g_parking_state.tcs_enabled;
    case SCOOTERDEMO_PARKING_SWITCH_HDC:
        return g_parking_state.hdc_enabled;
    case SCOOTERDEMO_PARKING_SWITCH_HILL_HOLD:
        return g_parking_state.hill_hold_enabled;
    default:
        return false;
    }
}

/*
 * 设置指定驻车开关状态。
 * 参数 id: 开关枚举。
 * 参数 enabled: 目标开关状态。
 */
static void parking_state_set_by_id(scooterdemo_parking_switch_id_t id, bool enabled)
{
    switch (id) {
    case SCOOTERDEMO_PARKING_SWITCH_SIDE_STAND:
        g_parking_state.side_stand_enabled = enabled;
        break;
    case SCOOTERDEMO_PARKING_SWITCH_AUTO_HEADLIGHT:
        g_parking_state.auto_headlight_enabled = enabled;
        break;
    case SCOOTERDEMO_PARKING_SWITCH_TCS:
        g_parking_state.tcs_enabled = enabled;
        break;
    case SCOOTERDEMO_PARKING_SWITCH_HDC:
        g_parking_state.hdc_enabled = enabled;
        break;
    case SCOOTERDEMO_PARKING_SWITCH_HILL_HOLD:
        g_parking_state.hill_hold_enabled = enabled;
        break;
    default:
        break;
    }
}

/* 根据当前驻车状态刷新界面开关。 */
static void apply_parking_state_to_ui(void)
{
    for (int i = 0; i < SCOOTERDEMO_PARKING_SWITCH_COUNT; i++) {
        custom_switch_t *sw = &g_parking_switches[i];

        if (sw->root == NULL) {
            continue;
        }

        set_switch_state(sw, parking_state_get_by_id((scooterdemo_parking_switch_id_t)i));
    }
}

/* 根据焦点索引刷新驻车行高亮。 */
static void update_parking_focus_highlight(void)
{
    for (int i = 0; i < SCOOTERDEMO_PARKING_SWITCH_COUNT; i++) {
        if (g_parking_rows[i] == NULL) {
            continue;
        }

        if (g_parking_focus_index >= 0 && i == g_parking_focus_index) {
            lv_obj_set_style_bg_color(g_parking_rows[i], COLOR_SWITCH_THUMB, 0);
            lv_obj_set_style_bg_opa(g_parking_rows[i], LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(g_parking_rows[i], 0, 0);
            lv_obj_set_style_radius(g_parking_rows[i], 20, 0);
            if (g_parking_row_labels[i] != NULL) {
                lv_obj_set_style_text_color(g_parking_row_labels[i], COLOR_TEXT_DARK, 0);
            }
        } else {
            lv_obj_set_style_bg_color(g_parking_rows[i], COLOR_ROW_BG, 0);
            lv_obj_set_style_bg_opa(g_parking_rows[i], LV_OPA_70, 0);
            lv_obj_set_style_border_width(g_parking_rows[i], 0, 0);
            if (g_parking_row_labels[i] != NULL) {
                lv_obj_set_style_text_color(g_parking_row_labels[i], COLOR_TEXT, 0);
            }
        }
    }
}

/*
 * 触发指定驻车开关回调。
 * 参数 id: 开关枚举。
 * 参数 enabled: 当前开关状态。
 */
static void emit_parking_switch_callback(scooterdemo_parking_switch_id_t id, bool enabled)
{
    switch (id) {
    case SCOOTERDEMO_PARKING_SWITCH_SIDE_STAND:
        if (g_parking_callbacks.on_side_stand_changed != NULL) {
            g_parking_callbacks.on_side_stand_changed(enabled, g_parking_callbacks_user_data);
        }
        break;

    case SCOOTERDEMO_PARKING_SWITCH_AUTO_HEADLIGHT:
        if (g_parking_callbacks.on_auto_headlight_changed != NULL) {
            g_parking_callbacks.on_auto_headlight_changed(enabled, g_parking_callbacks_user_data);
        }
        break;

    case SCOOTERDEMO_PARKING_SWITCH_TCS:
        if (g_parking_callbacks.on_tcs_changed != NULL) {
            g_parking_callbacks.on_tcs_changed(enabled, g_parking_callbacks_user_data);
        }
        break;

    case SCOOTERDEMO_PARKING_SWITCH_HDC:
        if (g_parking_callbacks.on_hdc_changed != NULL) {
            g_parking_callbacks.on_hdc_changed(enabled, g_parking_callbacks_user_data);
        }
        break;

    case SCOOTERDEMO_PARKING_SWITCH_HILL_HOLD:
        if (g_parking_callbacks.on_hill_hold_changed != NULL) {
            g_parking_callbacks.on_hill_hold_changed(enabled, g_parking_callbacks_user_data);
        }
        break;

    default:
        break;
    }

    if (g_parking_callbacks.on_any_switch_changed != NULL) {
        g_parking_callbacks.on_any_switch_changed(id, enabled, g_parking_callbacks_user_data);
    }
}

/*
 * 处理驻车开关值变化事件。
 * 参数 e: LVGL 事件对象，user_data 为开关枚举。
 */
static void parking_switch_event_cb(lv_event_t *e)
{
    scooterdemo_parking_switch_id_t id;
    bool enabled;

    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    id = (scooterdemo_parking_switch_id_t)(intptr_t)lv_event_get_user_data(e);
    enabled = !parking_state_get_by_id(id);

    parking_state_set_by_id(id, enabled);
    apply_parking_state_to_ui();
    emit_parking_switch_callback(id, enabled);
}

/*
 * 创建驻车开关行。
 * 参数 parent: 行父容器。
 * 参数 ui: UI 缩放与字体参数。
 * 参数 title: 行标题。
 * 参数 y: 纵向偏移。
 * 参数 id: 开关枚举。
 * 返回值: 创建的行对象。
 */
static lv_obj_t *create_parking_switch_row(lv_obj_t *parent,
                                           const scooterdemo_ui_metrics_t *ui,
                                           const char *title,
                                           int y,
                                           scooterdemo_parking_switch_id_t id)
{
    lv_obj_t *row = lv_obj_create(parent);
    g_parking_rows[id] = row;
    lv_obj_set_size(row, SCOOTERDEMO_W(ui, 560), SCOOTERDEMO_H(ui, 48));
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, SCOOTERDEMO_H(ui, y));
    style_row(row);

    lv_obj_t *label = lv_label_create(row);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_font(label, ui->font_20, 0);
    lv_obj_set_style_text_color(label, COLOR_TEXT, 0);
    g_parking_row_labels[id] = label;

    g_parking_switches[id].root = lv_btn_create(row);
    style_switch(&g_parking_switches[id]);
    lv_obj_add_event_cb(g_parking_switches[id].root,
                        parking_switch_event_cb,
                        LV_EVENT_CLICKED,
                        (void *)(intptr_t)id);

    return row;
}

/*
 * 构建驻车设置子页。
 * 参数 parent: 子页父容器。
 * 参数 ui: UI 缩放与字体参数。
 * 返回值: 子页根对象。
 */
lv_obj_t *scooterdemo_settings_subpage_parking_build(lv_obj_t *parent,
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

    for (int i = 0; i < SCOOTERDEMO_PARKING_SWITCH_COUNT; i++) {
        create_parking_switch_row(container,
                                  ui,
                                  g_parking_switch_labels[i],
                                  24 + i * 62,
                                  (scooterdemo_parking_switch_id_t)i);
    }

    g_parking_focus_index = -1;
    update_parking_focus_highlight();
    apply_parking_state_to_ui();
    return container;
}

/*
 * 设置驻车子页回调。
 * 参数 callbacks: 回调函数集合，传 NULL 表示清空回调。
 * 参数 user_data: 回调透传上下文。
 */
void scooterdemo_settings_subpage_parking_set_callbacks(
    const scooterdemo_parking_settings_callbacks_t *callbacks,
    void *user_data)
{
    if (callbacks != NULL) {
        g_parking_callbacks = *callbacks;
    } else {
        g_parking_callbacks.on_side_stand_changed = NULL;
        g_parking_callbacks.on_auto_headlight_changed = NULL;
        g_parking_callbacks.on_tcs_changed = NULL;
        g_parking_callbacks.on_hdc_changed = NULL;
        g_parking_callbacks.on_hill_hold_changed = NULL;
        g_parking_callbacks.on_any_switch_changed = NULL;
    }

    g_parking_callbacks_user_data = user_data;
}

/*
 * 设置驻车子页状态并刷新显示。
 * 参数 state: 新状态，传 NULL 则忽略。
 */
void scooterdemo_settings_subpage_parking_set_state(
    const scooterdemo_parking_settings_state_t *state)
{
    if (state == NULL) {
        return;
    }

    g_parking_state = *state;
    apply_parking_state_to_ui();
}

/*
 * 获取驻车子页当前状态。
 * 参数 state: 输出状态指针，传 NULL 则忽略。
 */
void scooterdemo_settings_subpage_parking_get_state(
    scooterdemo_parking_settings_state_t *state)
{
    if (state == NULL) {
        return;
    }

    *state = g_parking_state;
}

/* 释放驻车子页静态对象引用。 */
void scooterdemo_settings_subpage_parking_release(void)
{
    for (int i = 0; i < SCOOTERDEMO_PARKING_SWITCH_COUNT; i++) {
        g_parking_rows[i] = NULL;
        g_parking_row_labels[i] = NULL;
        g_parking_switches[i].root = NULL;
        g_parking_switches[i].thumb = NULL;
        g_parking_switches[i].symbol = NULL;
    }

    g_parking_focus_index = -1;
}

/* 处理驻车子页向上导航。 */
bool scooterdemo_settings_subpage_parking_handle_nav_up(void)
{
    if (g_parking_focus_index < 0) {
        g_parking_focus_index = SCOOTERDEMO_PARKING_SWITCH_COUNT - 1;
        update_parking_focus_highlight();
        return true;
    }

    g_parking_focus_index--;
    if (g_parking_focus_index < 0) {
        g_parking_focus_index = SCOOTERDEMO_PARKING_SWITCH_COUNT - 1;
    }

    update_parking_focus_highlight();
    return true;
}

/* 处理驻车子页向下导航。 */
bool scooterdemo_settings_subpage_parking_handle_nav_down(void)
{
    if (g_parking_focus_index < 0) {
        g_parking_focus_index = 0;
        update_parking_focus_highlight();
        return true;
    }

    g_parking_focus_index++;
    if (g_parking_focus_index >= SCOOTERDEMO_PARKING_SWITCH_COUNT) {
        g_parking_focus_index = 0;
    }

    update_parking_focus_highlight();
    return true;
}

/*
 * 设置驻车子页焦点状态。
 * 参数 focused: true 表示进入子页焦点，false 表示清除子页焦点。
 */
void scooterdemo_settings_subpage_parking_set_focus(bool focused)
{
    if (focused) {
        if (g_parking_focus_index < 0) {
            g_parking_focus_index = 0;
        }
    } else {
        g_parking_focus_index = -1;
    }

    update_parking_focus_highlight();
}

/* 处理驻车子页确认事件。 */
bool scooterdemo_settings_subpage_parking_handle_enter(void)
{
    bool enabled;
    scooterdemo_parking_switch_id_t id = (scooterdemo_parking_switch_id_t)g_parking_focus_index;

    if (g_parking_focus_index < 0) {
        return false;
    }

    enabled = !parking_state_get_by_id(id);
    parking_state_set_by_id(id, enabled);
    apply_parking_state_to_ui();
    emit_parking_switch_callback(id, enabled);
    return true;
}

/* 处理驻车子页双击事件。 */
bool scooterdemo_settings_subpage_parking_handle_double_click(void)
{
    bool enabled;
    scooterdemo_parking_switch_id_t id = (scooterdemo_parking_switch_id_t)g_parking_focus_index;

    if (g_parking_focus_index < 0) {
        return false;
    }

    enabled = !parking_state_get_by_id(id);
    parking_state_set_by_id(id, enabled);
    apply_parking_state_to_ui();
    emit_parking_switch_callback(id, enabled);
    return true;
}
