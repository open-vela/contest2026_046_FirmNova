#ifndef __VENDOR_ALLWINNERTECH_RIVOTEK_SCOOTERDEMO_SCOOTERDEMO_PAGES_H
#define __VENDOR_ALLWINNERTECH_RIVOTEK_SCOOTERDEMO_SCOOTERDEMO_PAGES_H

#include <stdbool.h>
#include <stdint.h>

#include <lvgl/lvgl.h>

#define SCOOTERDEMO_BASE_W 800
#define SCOOTERDEMO_BASE_H 480
#define SCOOTERDEMO_PAGE_COUNT 4
#define SCOOTERDEMO_THEME_COUNT 3

#define SCOOTERDEMO_W(ui, value) ((int32_t)((value) * (ui)->disp_w / SCOOTERDEMO_BASE_W))
#define SCOOTERDEMO_H(ui, value) ((int32_t)((value) * (ui)->disp_h / SCOOTERDEMO_BASE_H))

typedef enum {
    /* 首页。 */
    SCOOTERDEMO_PAGE_HOME = 0,
    /* 导航页。 */
    SCOOTERDEMO_PAGE_NAVIGATION,
    /* 音乐页。 */
    SCOOTERDEMO_PAGE_MUSIC,
    /* 设置页。 */
    SCOOTERDEMO_PAGE_SETTINGS,
} scooterdemo_page_id_t;

typedef struct {
    int32_t disp_w;
    int32_t disp_h;
    lv_font_t *font_16;
    lv_font_t *font_20;
    lv_font_t *font_24;
    lv_font_t *font_unit_24;
    lv_font_t *font_28;
    lv_font_t *font_40;
    lv_font_t *font_48;
    lv_font_t *font_speed_134;
} scooterdemo_ui_metrics_t;

typedef struct {
    lv_obj_t *containers[SCOOTERDEMO_PAGE_COUNT];
    uint8_t active_page;
    uint8_t previous_page;
} scooterdemo_page_manager_t;

/* 主题切换回调，theme_index 为目标主题索引。 */
typedef void (*scooterdemo_theme_apply_cb_t)(int theme_index, void *user_data);

void scooterdemo_page_manager_init(scooterdemo_page_manager_t *manager);
lv_obj_t *scooterdemo_page_manager_create_container(lv_obj_t *parent,
                                                    const scooterdemo_ui_metrics_t *ui);
void scooterdemo_page_manager_register(scooterdemo_page_manager_t *manager,
                                       scooterdemo_page_id_t page_id,
                                       lv_obj_t *container);
void scooterdemo_page_manager_show(scooterdemo_page_manager_t *manager,
                                   scooterdemo_page_id_t page_id);

void scooterdemo_navigation_page_build(lv_obj_t *parent,
                                       const scooterdemo_ui_metrics_t *ui);
void scooterdemo_navigation_page_set_background(const char *bg_path);
void scooterdemo_boot_demo_run(lv_obj_t *screen);
void scooterdemo_music_page_build(lv_obj_t *parent,
                                  const scooterdemo_ui_metrics_t *ui);
void scooterdemo_music_page_on_show(void);
bool scooterdemo_music_page_handle_nav_up(void);
bool scooterdemo_music_page_handle_nav_down(void);
bool scooterdemo_music_page_handle_enter(void);
void scooterdemo_music_page_release(lv_obj_t *parent);
void scooterdemo_music_page_sync(const char *title,
                                 const char *artist,
                                 bool connected,
                                 bool playing,
                                 uint32_t total_duration_ms);
/*
 * 构建设置页。
 * 参数 parent: 页面父容器。
 * 参数 ui: UI 缩放与字体参数。
 */
void scooterdemo_settings_page_build(lv_obj_t *parent,
                                     const scooterdemo_ui_metrics_t *ui);
/*
 * 设置主题应用回调。
 * 参数 cb: 主题应用回调函数。
 * 参数 user_data: 回调透传上下文。
 */
void scooterdemo_settings_page_set_theme_apply_cb(scooterdemo_theme_apply_cb_t cb,
                                                  void *user_data);
/* 获取当前生效主题索引。 */
int scooterdemo_settings_page_get_active_theme(void);
/* 设置页显示时的初始化入口。 */
void scooterdemo_settings_page_on_show(void);
/* 处理设置页向上导航事件。 */
bool scooterdemo_settings_page_handle_nav_up(void);
/* 处理设置页向下导航事件。 */
bool scooterdemo_settings_page_handle_nav_down(void);
/* 处理设置页确认事件。 */
bool scooterdemo_settings_page_handle_enter(void);
/* 处理设置页双击事件。 */
bool scooterdemo_settings_page_handle_double_click(void);
/* 处理设置页返回事件。 */
bool scooterdemo_settings_page_handle_back(void);

#endif
