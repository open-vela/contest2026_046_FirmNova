#include <stdio.h>
#include <unistd.h>

#include <lvgl/lvgl.h>

#include "scooterdemo_settings_subpages.h"

#define COLOR_TEXT lv_color_hex(0xFFFFFF)
#define COLOR_SUBTEXT lv_color_hex(0xE6EEF5)

#define VEHICLE_INFO_BG_PATH "/resource/imgs/settings_vehicle_info_bg.png"
#define VEHICLE_INFO_PANEL_W 546
#define VEHICLE_INFO_PANEL_H 320
#define VEHICLE_INFO_BG_Y_OFFSET 15
#define INFO_BLOCK_X 354
#define INFO_BLOCK_WIDTH 170

static int g_short_trip_km = 12;
static uint32_t g_short_trip_minutes = 72;
static int g_total_trip_km = 122;
static uint32_t g_total_trip_minutes = 912;

/*
 * 将分钟格式化为 h/min 字符串。
 * 参数 minutes: 总分钟数。
 * 参数 buf: 输出缓冲区。
 * 参数 buf_size: 缓冲区大小。
 */
static void format_duration(uint32_t minutes, char *buf, size_t buf_size)
{
    uint32_t hours;
    uint32_t remain_minutes;

    if (buf == NULL || buf_size == 0) {
        return;
    }

    hours = minutes / 60;
    remain_minutes = minutes % 60;
    snprintf(buf, buf_size, "%uh%umin", (unsigned int)hours, (unsigned int)remain_minutes);
}

/*
 * 创建里程信息块。
 * 参数 parent: 信息块父容器。
 * 参数 ui: UI 缩放与字体参数。
 * 参数 title: 区块标题。
 * 参数 mileage_km: 里程值（km）。
 * 参数 duration_minutes: 时长（分钟）。
 * 参数 y_offset: 纵向偏移。
 */
static void create_info_block(lv_obj_t *parent,
                              const scooterdemo_ui_metrics_t *ui,
                              const char *title,
                              int mileage_km,
                              uint32_t duration_minutes,
                              int y_offset)
{
    char line_mileage[32];
    char line_duration[32];
    char duration_buf[24];

    lv_obj_t *block = lv_obj_create(parent);
    lv_obj_remove_style_all(block);
    lv_obj_set_size(block, SCOOTERDEMO_W(ui, INFO_BLOCK_WIDTH), SCOOTERDEMO_H(ui, 105));
    lv_obj_align(block, LV_ALIGN_TOP_LEFT,
                 SCOOTERDEMO_W(ui, INFO_BLOCK_X),
                 SCOOTERDEMO_H(ui, y_offset));

    lv_obj_t *title_label = lv_label_create(block);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_font(title_label, ui->font_28, 0);
    lv_obj_set_style_text_color(title_label, COLOR_TEXT, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 0, 0);

    snprintf(line_mileage, sizeof(line_mileage), "行驶: %dkm", mileage_km);
    lv_obj_t *mileage_label = lv_label_create(block);
    lv_label_set_text(mileage_label, line_mileage);
    lv_obj_set_style_text_font(mileage_label, ui->font_20, 0);
    lv_obj_set_style_text_color(mileage_label, COLOR_SUBTEXT, 0);
    lv_obj_align(mileage_label, LV_ALIGN_TOP_LEFT, 0, SCOOTERDEMO_H(ui, 42));

    format_duration(duration_minutes, duration_buf, sizeof(duration_buf));
    snprintf(line_duration, sizeof(line_duration), "用时: %s", duration_buf);
    lv_obj_t *duration_label = lv_label_create(block);
    lv_label_set_text(duration_label, line_duration);
    lv_obj_set_style_text_font(duration_label, ui->font_20, 0);
    lv_obj_set_style_text_color(duration_label, COLOR_SUBTEXT, 0);
    lv_obj_align(duration_label, LV_ALIGN_TOP_LEFT, 0, SCOOTERDEMO_H(ui, 72));
}

/*
 * 构建车辆信息子页。
 * 参数 parent: 子页父容器。
 * 参数 ui: UI 缩放与字体参数。
 * 返回值: 子页根对象。
 */
lv_obj_t *scooterdemo_settings_subpage_vehicle_build(lv_obj_t *parent,
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

    lv_obj_t *panel = lv_obj_create(container);
    lv_obj_set_size(panel,
                    SCOOTERDEMO_W(ui, VEHICLE_INFO_PANEL_W),
                    SCOOTERDEMO_H(ui, VEHICLE_INFO_PANEL_H));
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_0, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_outline_width(panel, 0, 0);
    lv_obj_set_style_shadow_width(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_OFF);

    if (access(VEHICLE_INFO_BG_PATH, F_OK) == 0) {
        lv_obj_t *panel_bg = lv_img_create(panel);

        lv_img_set_src(panel_bg, VEHICLE_INFO_BG_PATH);
        lv_obj_align(panel_bg, LV_ALIGN_CENTER, 0, SCOOTERDEMO_H(ui, VEHICLE_INFO_BG_Y_OFFSET));
        lv_obj_clear_flag(panel_bg, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_move_background(panel_bg);
    }

    create_info_block(panel,
                      ui,
                      "小计里程",
                      g_short_trip_km,
                      g_short_trip_minutes,
                      54);
    create_info_block(panel,
                      ui,
                      "总计里程",
                      g_total_trip_km,
                      g_total_trip_minutes,
                      184);

    return container;
}

/* 释放车辆信息子页静态对象引用。 */
void scooterdemo_settings_subpage_vehicle_release(void)
{
}

/* 处理车辆信息子页向上导航。 */
bool scooterdemo_settings_subpage_vehicle_handle_nav_up(void)
{
    return true;
}

/* 处理车辆信息子页向下导航。 */
bool scooterdemo_settings_subpage_vehicle_handle_nav_down(void)
{
    return true;
}

/* 处理车辆信息子页确认事件。 */
bool scooterdemo_settings_subpage_vehicle_handle_enter(void)
{
    return true;
}

/* 处理车辆信息子页双击事件。 */
bool scooterdemo_settings_subpage_vehicle_handle_double_click(void)
{
    return false;
}
