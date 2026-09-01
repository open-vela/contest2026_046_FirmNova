#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include <lvgl/lvgl.h>

#include "scooterdemo_settings_subpages.h"

#define COLOR_TEXT lv_color_hex(0xFFFFFF)
#define COLOR_TEXT_DARK lv_color_hex(0x111111)
#define COLOR_SUBTEXT lv_color_hex(0xDCE8F5)
#define COLOR_SEG_BG lv_color_hex(0x5A5A5A)
#define COLOR_SEG_OFF lv_color_hex(0x5A5A5A)
#define COLOR_SEG_ON lv_color_hex(0x4BB2FF)

#define SCOOTER_MODEL_PATH "/resource/imgs/scooter_model.png"
#define TIRE_HEADER_X 18
#define TIRE_HEADER_Y 38
#define TIRE_PANEL_X 18
#define TIRE_PANEL_Y 93
#define TIRE_PANEL_W 546
#define TIRE_PANEL_H 266
#define TIRE_READING_Y 142
#define TIRE_READING_LEFT_X 86
#define TIRE_READING_RIGHT_X 399

static scooterdemo_tire_info_state_t g_tire_state = {
    .use_bar_unit = true,
    .left_pressure = 2.7,
    .right_pressure = 2.75,
    .left_temp_c = 26,
    .right_temp_c = 26,
};

static lv_obj_t *g_btn_bar;
static lv_obj_t *g_btn_kpa;
static lv_obj_t *g_left_pressure_label;
static lv_obj_t *g_right_pressure_label;
static lv_obj_t *g_left_temp_label;
static lv_obj_t *g_right_temp_label;

/* 设置单位按钮样式。 */
static void style_unit_button(lv_obj_t *btn, bool selected)
{
    lv_obj_set_style_bg_color(btn, selected ? lv_color_hex(0xFFFFFF) : COLOR_SEG_OFF, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_100, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_set_style_outline_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);

    lv_obj_t *label = lv_obj_get_child(btn, 0);
    if (label != NULL) {
        lv_obj_set_style_text_color(label, selected ? COLOR_TEXT_DARK : COLOR_TEXT, 0);
    }
}

/* 根据当前状态刷新胎压与温度显示。 */
static void update_tire_info_labels(void)
{
    char buf[32];

    if (g_left_pressure_label != NULL) {
        if (g_tire_state.use_bar_unit) {
            snprintf(buf, sizeof(buf), "%.2fbar", g_tire_state.left_pressure);
        } else {
            snprintf(buf, sizeof(buf), "%.0fkPa", g_tire_state.left_pressure * 100);
        }
        lv_label_set_text(g_left_pressure_label, buf);
    }

    if (g_right_pressure_label != NULL) {
        if (g_tire_state.use_bar_unit) {
            snprintf(buf, sizeof(buf), "%.2fbar", g_tire_state.right_pressure);
        } else {
            snprintf(buf, sizeof(buf), "%.0fkPa", g_tire_state.right_pressure * 100);
        }
        lv_label_set_text(g_right_pressure_label, buf);
    }

    if (g_left_temp_label != NULL) {
        snprintf(buf, sizeof(buf), "%d°C", g_tire_state.left_temp_c);
        lv_label_set_text(g_left_temp_label, buf);
    }

    if (g_right_temp_label != NULL) {
        snprintf(buf, sizeof(buf), "%d°C", g_tire_state.right_temp_c);
        lv_label_set_text(g_right_temp_label, buf);
    }

    if (g_btn_bar != NULL) {
        style_unit_button(g_btn_bar, g_tire_state.use_bar_unit);
    }

    if (g_btn_kpa != NULL) {
        style_unit_button(g_btn_kpa, !g_tire_state.use_bar_unit);
    }
}

/*
 * 处理单位按钮点击事件。
 * 参数 e: LVGL 事件对象，user_data 表示目标单位。
 */
static void unit_button_event_cb(lv_event_t *e)
{
    bool use_bar_unit;

    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    use_bar_unit = (bool)(intptr_t)lv_event_get_user_data(e);
    if (g_tire_state.use_bar_unit == use_bar_unit) {
        return;
    }

    g_tire_state.use_bar_unit = use_bar_unit;
    update_tire_info_labels();
}

/*
 * 创建左右轮胎读数区块。
 * 参数 parent: 区块父容器。
 * 参数 ui: UI 缩放与字体参数。
 * 参数 is_left: true 为左侧，false 为右侧。
 * 参数 pressure_label: 输出胎压标签指针。
 * 参数 temp_label: 输出温度标签指针。
 * 返回值: 创建的区块对象。
 */
static lv_obj_t *create_reading_block(lv_obj_t *parent,
                                      const scooterdemo_ui_metrics_t *ui,
                                      bool is_left,
                                      lv_obj_t **pressure_label,
                                      lv_obj_t **temp_label)
{
    lv_obj_t *block = lv_obj_create(parent);

    lv_obj_remove_style_all(block);
    lv_obj_set_size(block, SCOOTERDEMO_W(ui, 120), SCOOTERDEMO_H(ui, 90));
    lv_obj_align(block, LV_ALIGN_TOP_LEFT,
                 is_left ? SCOOTERDEMO_W(ui, TIRE_READING_LEFT_X)
                         : SCOOTERDEMO_W(ui, TIRE_READING_RIGHT_X),
                 SCOOTERDEMO_H(ui, TIRE_READING_Y));

    *pressure_label = lv_label_create(block);
    lv_obj_set_style_text_font(*pressure_label, ui->font_20, 0);
    lv_obj_set_style_text_color(*pressure_label, COLOR_TEXT, 0);
    lv_obj_align(*pressure_label, LV_ALIGN_TOP_LEFT, 0, 0);

    *temp_label = lv_label_create(block);
    lv_obj_set_style_text_font(*temp_label, ui->font_20, 0);
    lv_obj_set_style_text_color(*temp_label, COLOR_SUBTEXT, 0);
    lv_obj_align(*temp_label, LV_ALIGN_TOP_LEFT, 0, SCOOTERDEMO_H(ui, 42));

    return block;
}

/*
 * 构建胎压监测子页。
 * 参数 parent: 子页父容器。
 * 参数 ui: UI 缩放与字体参数。
 * 返回值: 子页根对象。
 */
lv_obj_t *scooterdemo_settings_subpage_tire_build(lv_obj_t *parent,
                                                  const scooterdemo_ui_metrics_t *ui)
{
    lv_obj_t *container;
    lv_obj_t *header_row;
    lv_obj_t *unit_seg;
    lv_obj_t *panel;
    lv_obj_t *model_slot;

    container = lv_obj_create(parent);
    lv_obj_set_size(container, SCOOTERDEMO_W(ui, 585), SCOOTERDEMO_H(ui, 360));
    lv_obj_align(container, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(container, LV_OPA_0, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_outline_width(container, 0, 0);
    lv_obj_set_style_shadow_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_set_scrollbar_mode(container, LV_SCROLLBAR_MODE_OFF);

    header_row = lv_obj_create(container);
    lv_obj_set_size(header_row, SCOOTERDEMO_W(ui, TIRE_PANEL_W), SCOOTERDEMO_H(ui, 44));
    lv_obj_align(header_row, LV_ALIGN_TOP_LEFT,
                 SCOOTERDEMO_W(ui, TIRE_HEADER_X),
                 SCOOTERDEMO_H(ui, TIRE_HEADER_Y));
    lv_obj_set_style_bg_opa(header_row, LV_OPA_0, 0);
    lv_obj_set_style_border_width(header_row, 0, 0);
    lv_obj_set_style_outline_width(header_row, 0, 0);
    lv_obj_set_style_shadow_width(header_row, 0, 0);
    lv_obj_set_style_pad_all(header_row, 0, 0);
    lv_obj_set_scrollbar_mode(header_row, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *unit_title = lv_label_create(header_row);
    lv_label_set_text(unit_title, "单位设置");
    lv_obj_set_style_text_font(unit_title, ui->font_20, 0);
    lv_obj_set_style_text_color(unit_title, COLOR_TEXT, 0);
    lv_obj_align(unit_title, LV_ALIGN_LEFT_MID, 0, 0);

    unit_seg = lv_obj_create(header_row);
    lv_obj_set_size(unit_seg, SCOOTERDEMO_W(ui, 92), SCOOTERDEMO_H(ui, 30));
    lv_obj_align(unit_seg, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(unit_seg, COLOR_SEG_BG, 0);
    lv_obj_set_style_bg_opa(unit_seg, LV_OPA_100, 0);
    lv_obj_set_style_border_width(unit_seg, 0, 0);
    lv_obj_set_style_radius(unit_seg, 14, 0);
    lv_obj_set_style_pad_all(unit_seg, 2, 0);
    lv_obj_set_scrollbar_mode(unit_seg, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(unit_seg, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(unit_seg, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    g_btn_bar = lv_btn_create(unit_seg);
    lv_obj_set_size(g_btn_bar, SCOOTERDEMO_W(ui, 42), SCOOTERDEMO_H(ui, 26));
    lv_obj_add_event_cb(g_btn_bar, unit_button_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)true);
    lv_obj_t *bar_label = lv_label_create(g_btn_bar);
    lv_label_set_text(bar_label, "bar");
    lv_obj_set_style_text_font(bar_label, ui->font_16, 0);
    lv_obj_set_style_text_color(bar_label, COLOR_TEXT, 0);
    lv_obj_center(bar_label);

    g_btn_kpa = lv_btn_create(unit_seg);
    lv_obj_set_size(g_btn_kpa, SCOOTERDEMO_W(ui, 42), SCOOTERDEMO_H(ui, 26));
    lv_obj_add_event_cb(g_btn_kpa, unit_button_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)false);
    lv_obj_t *kpa_label = lv_label_create(g_btn_kpa);
    lv_label_set_text(kpa_label, "kPa");
    lv_obj_set_style_text_font(kpa_label, ui->font_16, 0);
    lv_obj_set_style_text_color(kpa_label, COLOR_TEXT, 0);
    lv_obj_center(kpa_label);

    panel = lv_obj_create(container);
    lv_obj_set_size(panel, SCOOTERDEMO_W(ui, TIRE_PANEL_W), SCOOTERDEMO_H(ui, TIRE_PANEL_H));
    lv_obj_align(panel, LV_ALIGN_TOP_LEFT,
                 SCOOTERDEMO_W(ui, TIRE_PANEL_X),
                 SCOOTERDEMO_H(ui, TIRE_PANEL_Y));
    lv_obj_set_style_bg_opa(panel, LV_OPA_0, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_outline_width(panel, 0, 0);
    lv_obj_set_style_shadow_width(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_OFF);

    model_slot = lv_obj_create(panel);
    lv_obj_remove_style_all(model_slot);
    lv_obj_set_size(model_slot, SCOOTERDEMO_W(ui, TIRE_PANEL_W), SCOOTERDEMO_H(ui, TIRE_PANEL_H));
    lv_obj_align(model_slot, LV_ALIGN_CENTER, 0, 0);

    if (access(SCOOTER_MODEL_PATH, F_OK) == 0) {
        lv_obj_t *model_img = lv_img_create(model_slot);
        lv_img_set_src(model_img, SCOOTER_MODEL_PATH);
        lv_obj_center(model_img);
    } else {
        lv_obj_t *placeholder = lv_label_create(model_slot);
        lv_label_set_text(placeholder, "车模图片缺失");
        lv_obj_set_style_text_color(placeholder, COLOR_SUBTEXT, 0);
        lv_obj_set_style_text_font(placeholder, ui->font_20, 0);
        lv_obj_center(placeholder);
    }

    create_reading_block(panel, ui, true, &g_left_pressure_label, &g_left_temp_label);
    create_reading_block(panel, ui, false, &g_right_pressure_label, &g_right_temp_label);

    update_tire_info_labels();
    return container;
}

/*
 * 设置胎压子页状态并刷新显示。
 * 参数 state: 新状态，传 NULL 则忽略。
 */
void scooterdemo_settings_subpage_tire_set_state(
    const scooterdemo_tire_info_state_t *state)
{
    if (state == NULL) {
        return;
    }

    g_tire_state = *state;
    update_tire_info_labels();
}

/*
 * 获取胎压子页当前状态。
 * 参数 state: 输出状态指针，传 NULL 则忽略。
 */
void scooterdemo_settings_subpage_tire_get_state(
    scooterdemo_tire_info_state_t *state)
{
    if (state == NULL) {
        return;
    }

    *state = g_tire_state;
}

/* 释放胎压子页静态对象引用。 */
void scooterdemo_settings_subpage_tire_release(void)
{
    g_btn_bar = NULL;
    g_btn_kpa = NULL;
    g_left_pressure_label = NULL;
    g_right_pressure_label = NULL;
    g_left_temp_label = NULL;
    g_right_temp_label = NULL;
}

/* 处理胎压子页向上导航。 */
bool scooterdemo_settings_subpage_tire_handle_nav_up(void)
{
    g_tire_state.use_bar_unit = !g_tire_state.use_bar_unit;
    update_tire_info_labels();
    return true;
}

/* 处理胎压子页向下导航。 */
bool scooterdemo_settings_subpage_tire_handle_nav_down(void)
{
    g_tire_state.use_bar_unit = !g_tire_state.use_bar_unit;
    update_tire_info_labels();
    return true;
}

/* 处理胎压子页确认事件。 */
bool scooterdemo_settings_subpage_tire_handle_enter(void)
{
    g_tire_state.use_bar_unit = !g_tire_state.use_bar_unit;
    update_tire_info_labels();
    return true;
}

/* 处理胎压子页双击事件。 */
bool scooterdemo_settings_subpage_tire_handle_double_click(void)
{
    g_tire_state.use_bar_unit = !g_tire_state.use_bar_unit;
    update_tire_info_labels();
    return true;
}
