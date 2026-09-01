#include <nuttx/config.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include <errno.h>
#include <stdlib.h>
#include <pthread.h>

#include <lvgl/lvgl.h>
#include "driver_demo.h"
#include "scooterdemo_pages.h"
#include "scooterdemo_page_ai.h"
#ifdef CONFIG_RIVOTEK_QRCODE_SERVICE
#include "rvt_qrcode.h"
#endif
#if defined(CONFIG_RIVOTEK_PROVISIONING_SERVICE) && \
    defined(CONFIG_RIVOTEK_QRCODE_SERVICE)
#include "rvt_provisioning_qrcode_bridge.h"
#endif
#include "key_processor.h"
#include <nuttx/input/buttons.h>
#include <poll.h>
#include "bt_music_impl.h"
#include "theme_impl.h"
#include "time_impl.h"
#include "weather_impl.h"
#include "rivotek_platform.h"

/* Use libuv if configured */
#ifdef CONFIG_LV_USE_NUTTX_LIBUV
#include <uv.h>
#endif

#ifndef AVRCP_ATTR_PLAYING_TIME
#define AVRCP_ATTR_PLAYING_TIME 0x07
#endif

// Resolution Adaptation
#define BASE_W 800
#define BASE_H 480

static int32_t disp_w = BASE_W;
static int32_t disp_h = BASE_H;

// Scaling Macros
#define W(v) ((int32_t)((v) * disp_w / BASE_W))
#define H(v) ((int32_t)((v) * disp_h / BASE_H))
#define R(v) H(v) // Use height scale for radius/circles to ensure fit
#define F(v) ((int32_t)((v) * disp_h / BASE_H)) // Scale font size based on height

// Colors
#define COLOR_BG        lv_color_hex(0x000000)
#define COLOR_ACCENT    lv_color_hex(0xFF5722) // Orange
#define COLOR_TEXT      lv_color_hex(0xFFFFFF)
#define COLOR_GREEN     lv_color_hex(0x00FF00)
#define COLOR_GRAY      lv_color_hex(0x888888)
#define COLOR_FOCUS     lv_color_hex(0x2196F3) // Blue for focus

#define SCOOTERDEMO_HOME_BG_IMAGE_PATH "/resource/imgs/yd_bg_orange.png"
#define SCOOTERDEMO_HOME_GIF_RESOURCE_PATH "/resource/imgs/meng.gif"
#define SCOOTERDEMO_HOME_IDLE_GIF_RESOURCE_PATH "/resource/imgs/meng.gif"
#define SCOOTERDEMO_HOME_GIF_SLOT_IDLE_Y_OFFSET  (-H(10))
#define SCOOTERDEMO_HOME_GIF_SLOT_DEMO_Y_OFFSET  (-H(10))
#define SCOOTERDEMO_WEATHER_ICON_SUNNY_PATH "/resource/imgs/weather_sunny.png"
#define SCOOTERDEMO_WEATHER_ICON_RAIN_PATH "/resource/imgs/weather_rain.png"
#define SCOOTERDEMO_WEATHER_ICON_OTHER_PATH "/resource/imgs/weather_other.png"
#define SCOOTERDEMO_NAV_MAP_CHONGQING_PATH "/resource/imgs/nav_map_chongqing.png"
#define SCOOTERDEMO_NAV_MAP_HANGZHOU_PATH "/resource/imgs/nav_map_hangzhou.png"
#define SCOOTERDEMO_NAV_MAP_WUXI_PATH "/resource/imgs/nav_map_wuxi.png"
#define SCOOTERDEMO_NAV_MAP_NANJING_PATH "/resource/imgs/nav_map_nanjing.png"
#define SCOOTERDEMO_NAV_MAP_SUZHOU_PATH "/resource/imgs/nav_map_suzhou.png"
#define SCOOTERDEMO_NAV_MAP_BEIJING_PATH "/resource/imgs/nav_map_beijing.png"
#define SCOOTERDEMO_AI_NAV_STATUS_ICON_PATH "/resource/imgs/ai_nav_status.png"

#define SCOOTERDEMO_SIDEBAR_ITEM_COUNT 4
#define SCOOTERDEMO_PAGE_SWITCH_DELAY_MS 50
#define SCOOTERDEMO_HOME_IDLE_TIMEOUT_MS 10000U
#define SCOOTERDEMO_BT_UI_SYNC_INTERVAL_MS 200U
#define SCOOTERDEMO_BT_METADATA_FALLBACK_MS 2000U

// Fonts
static lv_font_t *font_16;
static lv_font_t *font_20;
static lv_font_t *font_ready_26;
static lv_font_t *font_info_20;
static lv_font_t *font_status_26;
static lv_font_t *font_theme_22;
static lv_font_t *font_weather_city_18;
static lv_font_t *font_weather_temp_32;
static lv_font_t *font_weather_text_18;
static lv_font_t *font_gear_current_56;
static lv_font_t *font_gear_side_24;
static lv_font_t *font_24;
static lv_font_t *font_unit_24;
static lv_font_t *font_28;
static lv_font_t *font_40;
static lv_font_t *font_48;
static lv_font_t *font_speed_134;

#define SIDEBAR_TOUCH_DEBOUNCE_MS 80

// Data Interface
typedef struct {
    int speed;
    float trip_km;
    float total_km;
    int battery_pct;
    int temp_c;
    int range_km;
    char gear[2]; // "P", "D", "N", "R"
} VehicleData;

static VehicleData g_vdata = {
    .speed = 0,
    .trip_km = 32.5f,
    .total_km = 389.6f,
    .battery_pct = 100,
    .temp_c = 22,
    .range_km = 465,
    .gear = "P"
};

// Global UI Objects
static lv_obj_t *screen;
static lv_obj_t *home_bg_img;
static lv_obj_t *top_bar;
static lv_obj_t *home_top_bar;
static lv_obj_t *right_sidebar;
static lv_obj_t *home_bg_base;
static lv_obj_t *home_gif_slot;
static lv_obj_t *home_gif_img;
static lv_obj_t *home_effect_label;
static lv_obj_t *time_label;
static lv_obj_t *top_temp_label;
static lv_obj_t *home_time_label;
static lv_obj_t *home_top_temp_label;
static lv_obj_t *top_mode_label;
static lv_obj_t *home_top_mode_label;
static lv_obj_t *top_ai_status_icon;
static lv_obj_t *home_top_ai_status_icon;
static lv_obj_t *left_temp_label;
static lv_obj_t *weather_desc_label;
static lv_obj_t *weather_city_label;
static lv_obj_t *weather_air_label;
static lv_obj_t *weather_icon_img;
static lv_obj_t *weather_icon_fallback_label;
static lv_obj_t *speed_bg_slot;
static lv_obj_t *speed_bg_img;
static lv_obj_t *driving_speed_bg_slot;
static lv_obj_t *driving_speed_bg_img;
static lv_obj_t *speed_label;
static lv_obj_t *speed_unit_label;
static lv_obj_t *trip_label;
static lv_obj_t *total_label;
static lv_obj_t *range_label;
static lv_obj_t *battery_bar;
static lv_obj_t *battery_pct_label;
static lv_obj_t *gear_labels[4]; // S, D, P, R
static lv_obj_t *gear_current_label;
static lv_obj_t *bottom_theme_img;
static lv_group_t *sidebar_group;
static lv_obj_t *sidebar_btns[SCOOTERDEMO_PAGE_COUNT];
static int sidebar_btn_count;
static lv_obj_t *sidebar_focus_label;
static lv_obj_t *bt_icon_label;
static lv_obj_t *bt_device_label;
static lv_obj_t *music_title_label;
static lv_obj_t *music_artist_label;
static lv_obj_t *music_album_fallback_icon;
static lv_obj_t *music_album_rotating_img;
static lv_obj_t *main_containers[SCOOTERDEMO_PAGE_COUNT];
static scooterdemo_page_manager_t g_page_manager;
static lv_timer_t *g_page_switch_timer;
static int g_pending_page_index = -1;
static bool g_music_album_rotation_active;
static bool g_home_idle_media_visible;
static uint32_t g_home_last_interaction_tick;

typedef enum {
    SCOOTERDEMO_BUTTON_HANDLER_NONE = 0,
    SCOOTERDEMO_BUTTON_HANDLER_SIDEBAR,
    SCOOTERDEMO_BUTTON_HANDLER_MUSIC,
    SCOOTERDEMO_BUTTON_HANDLER_SETTINGS,
} scooterdemo_button_handler_id_t;

static scooterdemo_button_handler_id_t g_active_button_handler = SCOOTERDEMO_BUTTON_HANDLER_NONE;

static void update_sidebar_focus_indicator(void);
static void apply_theme_selection(int theme_index, void *user_data);
static void on_theme_changed(int theme_index, void *user_data);
static void sidebar_page_button_event_cb(KEYCODE keycode, BUTTON_EVENT event, void *user_data);
static void music_page_button_event_cb(KEYCODE keycode, BUTTON_EVENT event, void *user_data);
static void settings_page_button_event_cb(KEYCODE keycode, BUTTON_EVENT event, void *user_data);
static void update_page_button_handler(int page_index);
static void ai_request_page_switch(int page_index, void *user_data);
static void sync_sidebar_focus_with_page(int page_index);
static void switch_page_now(int page_index);
static void request_page_switch(int page_index);
static void page_switch_timer_cb(lv_timer_t *timer);
static void sync_music_page_snapshot(void);
static void update_music_album_rotation(void);
static void sync_music_ui_snapshot(uint32_t tick);
static void sync_music_ui_timer_cb(lv_timer_t *timer);
static void refresh_gear_highlight(const char *gear);
static void set_label_text_if_changed(lv_obj_t *label, const char *text);
static const char *get_weather_icon_path(const char *weather_desc);
static void update_weather_icon(const char *weather_desc);
static const char *get_navigation_map_path_for_city(const char *city);
static void update_navigation_map_for_city(const char *city);

static const char *get_theme_bottom_image_path(int theme_index)
{
    static const char *theme_bottom_paths[SCOOTERDEMO_THEME_COUNT] = {
        "/resource/imgs/Group_g.png",
        "/resource/imgs/Group_b.png",
        "/resource/imgs/Group_o.png",
    };

    if (theme_index < 0 || theme_index >= SCOOTERDEMO_THEME_COUNT) {
        return theme_bottom_paths[0];
    }

    return theme_bottom_paths[theme_index];
}

static void update_theme_bottom_image(int theme_index)
{
    if (bottom_theme_img == NULL) {
        return;
    }

    lv_img_set_src(bottom_theme_img, get_theme_bottom_image_path(theme_index));
    lv_obj_center(bottom_theme_img);
}

static const char *get_theme_mode_name(int theme_index)
{
    static const char *theme_mode_names[SCOOTERDEMO_THEME_COUNT] = {
        "智绿灵润",
        "科蓝慧芯",
        "速橙幻微",
    };

    if (theme_index < 0 || theme_index >= SCOOTERDEMO_THEME_COUNT) {
        return theme_mode_names[0];
    }

    return theme_mode_names[theme_index];
}

static void update_theme_mode_labels(int theme_index)
{
    const char *mode_name = get_theme_mode_name(theme_index);

    if (top_mode_label != NULL) {
        set_label_text_if_changed(top_mode_label, mode_name);
    }

    if (home_top_mode_label != NULL) {
        set_label_text_if_changed(home_top_mode_label, mode_name);
    }
}

static void update_ai_nav_status_icons(void)
{
    bool show_icon = scooterdemo_ai_is_session_active();

    if (top_ai_status_icon != NULL) {
        if (show_icon) {
            lv_obj_clear_flag(top_ai_status_icon, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(top_ai_status_icon, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (home_top_ai_status_icon != NULL) {
        if (show_icon) {
            lv_obj_clear_flag(home_top_ai_status_icon, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(home_top_ai_status_icon, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static lv_color_t get_theme_accent_color(int theme_index)
{
    static const uint32_t theme_accent_colors[SCOOTERDEMO_THEME_COUNT] = {
        0x00C853,
        0x2196F3,
        0xFF5722,
    };

    if (theme_index < 0 || theme_index >= SCOOTERDEMO_THEME_COUNT) {
        return lv_color_hex(theme_accent_colors[0]);
    }

    return lv_color_hex(theme_accent_colors[theme_index]);
}

static void apply_theme_selection(int theme_index, void *user_data)
{
    (void)user_data;

    rivotek_theme_component_request_apply(theme_index);
}

static void on_theme_changed(int theme_index, void *user_data)
{
    (void)user_data;

    update_theme_mode_labels(theme_index);
    update_theme_bottom_image(theme_index);
    scooterdemo_ai_reset_percent_label();
    refresh_gear_highlight(g_vdata.gear);
    update_sidebar_focus_indicator();
    update_navigation_map_for_city(rivotek_weather_component_get_city_text());
    if (g_page_manager.active_page >= 0 &&
        g_page_manager.active_page < SCOOTERDEMO_PAGE_COUNT &&
        main_containers[g_page_manager.active_page] != NULL) {
        lv_obj_invalidate(main_containers[g_page_manager.active_page]);
    }
    if (screen != NULL) {
        lv_obj_invalidate(screen);
        lv_refr_now(NULL);
    }
}

static void start_album_rotation(lv_obj_t *img)
{
    lv_anim_delete(img, NULL);
    lv_img_set_angle(img, 0);
    lv_img_set_pivot(img, W(99) / 2, H(99) / 2);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, img);
    lv_anim_set_exec_cb(&anim, (lv_anim_exec_xcb_t)lv_img_set_angle);
    lv_anim_set_values(&anim, 0, 3600);
    lv_anim_set_time(&anim, 12000);
    lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&anim, lv_anim_path_linear);
    lv_anim_set_playback_time(&anim, 0);
    lv_anim_set_playback_delay(&anim, 0);
    lv_anim_set_early_apply(&anim, true);
    lv_anim_start(&anim);
}

static void stop_album_rotation(lv_obj_t *img)
{
    if (img == NULL) {
        return;
    }

    lv_anim_delete(img, NULL);
    lv_img_set_angle(img, 0);
}

static void refresh_gear_highlight(const char *gear)
{
    static const char *gears[] = {"S", "D", "P", "R"};
    lv_color_t active_color = get_theme_accent_color(rivotek_theme_component_get_active_index());
    const char *current_gear = "P";

    for (int index = 0; index < 4; index++) {
        if (gear != NULL && strcmp(gear, gears[index]) == 0) {
            current_gear = gears[index];
            break;
        }
    }

    if (gear_labels[0] != NULL) {
        set_label_text_if_changed(gear_labels[0], "S");
        lv_obj_set_style_text_color(gear_labels[0], COLOR_GRAY, 0);
        lv_obj_set_style_text_opa(gear_labels[0], LV_OPA_30, 0);
        lv_obj_set_style_text_font(gear_labels[0], font_gear_side_24, 0);
        lv_obj_align(gear_labels[0], LV_ALIGN_CENTER, W(-34), 0);
    }

    if (gear_labels[1] != NULL) {
        set_label_text_if_changed(gear_labels[1],
                                  strcmp(current_gear, "D") == 0 ? "P" : "D");
        lv_obj_set_style_text_color(gear_labels[1], COLOR_GRAY, 0);
        lv_obj_set_style_text_opa(gear_labels[1], LV_OPA_30, 0);
        lv_obj_set_style_text_font(gear_labels[1], font_gear_side_24, 0);
        lv_obj_align(gear_labels[1], LV_ALIGN_CENTER, W(46), 0);
    }

    for (int index = 2; index < 4; index++) {
        if (gear_labels[index] != NULL) {
            lv_obj_add_flag(gear_labels[index], LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (gear_current_label != NULL) {
        set_label_text_if_changed(gear_current_label, current_gear);
        lv_obj_set_style_text_color(gear_current_label, active_color, 0);
        lv_obj_set_style_text_font(gear_current_label, font_gear_current_56, 0);
    }
}

static void set_label_text_if_changed(lv_obj_t *label, const char *text)
{
    const char *current_text;

    if (label == NULL || text == NULL) {
        return;
    }

    current_text = lv_label_get_text(label);
    if (current_text != NULL && strcmp(current_text, text) == 0) {
        return;
    }

    lv_label_set_text(label, text);
}

static const char *get_navigation_map_path_for_city(const char *city)
{
    if (city != NULL) {
        if (strstr(city, "重庆") != NULL) {
            return SCOOTERDEMO_NAV_MAP_CHONGQING_PATH;
        }

        if (strstr(city, "杭州") != NULL) {
            return SCOOTERDEMO_NAV_MAP_HANGZHOU_PATH;
        }

        if (strstr(city, "无锡") != NULL) {
            return SCOOTERDEMO_NAV_MAP_WUXI_PATH;
        }

        if (strstr(city, "苏州") != NULL) {
            return SCOOTERDEMO_NAV_MAP_SUZHOU_PATH;
        }

        if (strstr(city, "北京") != NULL) {
            return SCOOTERDEMO_NAV_MAP_BEIJING_PATH;
        }

        if (strstr(city, "南京") != NULL) {
            return SCOOTERDEMO_NAV_MAP_NANJING_PATH;
        }
    }

    return SCOOTERDEMO_NAV_MAP_BEIJING_PATH;
}

static void update_navigation_map_for_city(const char *city)
{
    const char *map_path;

    map_path = get_navigation_map_path_for_city(city);
    scooterdemo_navigation_page_set_background(map_path);
    scooterdemo_ai_set_navigation_map_path(map_path);
}

static const char *get_weather_icon_path(const char *weather_desc)
{
    if (weather_desc == NULL) {
        return SCOOTERDEMO_WEATHER_ICON_OTHER_PATH;
    }

    if (strstr(weather_desc, "晴") != NULL) {
        return SCOOTERDEMO_WEATHER_ICON_SUNNY_PATH;
    }

    if (strstr(weather_desc, "雨") != NULL) {
        return SCOOTERDEMO_WEATHER_ICON_RAIN_PATH;
    }

    return SCOOTERDEMO_WEATHER_ICON_OTHER_PATH;
}

static void update_weather_icon(const char *weather_desc)
{
    const char *icon_path;

    if (weather_icon_img == NULL || weather_icon_fallback_label == NULL) {
        return;
    }

    icon_path = get_weather_icon_path(weather_desc);
    if (access(icon_path, F_OK) == 0) {
        lv_img_set_src(weather_icon_img, icon_path);
        lv_obj_clear_flag(weather_icon_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(weather_icon_fallback_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_center(weather_icon_img);
    } else {
        lv_obj_add_flag(weather_icon_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(weather_icon_fallback_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_center(weather_icon_fallback_label);
    }
}

static void set_home_center_speed_visible(bool visible)
{
    bool show_standby_speed_bg;
    bool show_driving_speed_bg;

    show_standby_speed_bg = visible &&
                           g_page_manager.active_page == SCOOTERDEMO_PAGE_HOME &&
                           !driver_demo_is_running() &&
                           !g_home_idle_media_visible;

    show_driving_speed_bg = visible &&
                           g_page_manager.active_page == SCOOTERDEMO_PAGE_HOME &&
                           driver_demo_is_running() &&
                           !g_home_idle_media_visible;

    if (speed_bg_slot != NULL) {
        if (show_standby_speed_bg) {
            lv_obj_clear_flag(speed_bg_slot, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(speed_bg_slot, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (driving_speed_bg_slot != NULL) {
        if (show_driving_speed_bg) {
            lv_obj_clear_flag(driving_speed_bg_slot, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(driving_speed_bg_slot, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (speed_label != NULL) {
        if (visible) {
            lv_obj_clear_flag(speed_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(speed_label, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (speed_unit_label != NULL) {
        if (visible) {
            lv_obj_clear_flag(speed_unit_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(speed_unit_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void hide_home_center_media(void)
{
    if (home_gif_slot != NULL) {
        lv_obj_add_flag(home_gif_slot, LV_OBJ_FLAG_HIDDEN);
    }
    if (home_gif_img != NULL) {
        lv_obj_add_flag(home_gif_img, LV_OBJ_FLAG_HIDDEN);
    }
    if (home_effect_label != NULL) {
        lv_obj_add_flag(home_effect_label, LV_OBJ_FLAG_HIDDEN);
    }
}

static void set_home_center_media_y_offset(int32_t y_offset)
{
    if (home_gif_slot == NULL) {
        return;
    }

    lv_obj_align(home_gif_slot, LV_ALIGN_CENTER, 0, y_offset);
}

static const char *resolve_home_idle_gif_path(uint32_t tick)
{
    static const char *current_path = NULL;
    static uint32_t last_switch_tick = 0;
    static bool init_done = false;
    static const char *available_candidates[10];
    static int available_count = 0;

    static const char *candidates[] = {
        SCOOTERDEMO_HOME_IDLE_GIF_RESOURCE_PATH,
        SCOOTERDEMO_HOME_GIF_RESOURCE_PATH,
        "/resource/imgs/idle_1.gif",
        "/resource/imgs/idle_2.gif",
        "/resource/imgs/idle_3.gif",
        NULL
    };

    if (!init_done) {
        for (int i = 0; candidates[i] != NULL && available_count < 10; i++) {
            if (access(candidates[i], F_OK) == 0) {
                bool exists = false;
                for (int j = 0; j < available_count; j++) {
                    if (strcmp(available_candidates[j], candidates[i]) == 0) {
                        exists = true;
                        break;
                    }
                }
                if (!exists) {
                    available_candidates[available_count++] = candidates[i];
                }
            }
        }
        init_done = true;
        // Make sure random numbers are different across boots
        srand(tick);
    }

    if (available_count == 0) {
        return NULL;
    }

    if (current_path == NULL || (tick - last_switch_tick >= 20000U)) {
        if (available_count == 1) {
            current_path = available_candidates[0];
        } else {
            int idx;
            do {
                idx = rand() % available_count;
            } while (available_candidates[idx] == current_path);
            current_path = available_candidates[idx];
        }
        last_switch_tick = tick;
    }

    return current_path;
}

static void show_home_center_media(const char *gif_path, const char *fallback_text)
{
    static const char *last_gif_path = NULL;
    bool has_gif;

    if (home_gif_slot == NULL) {
        return;
    }

    has_gif = (gif_path != NULL && access(gif_path, F_OK) == 0);
    lv_obj_clear_flag(home_gif_slot, LV_OBJ_FLAG_HIDDEN);

    if (has_gif) {
        if (home_gif_img == NULL) {
            home_gif_img = lv_gif_create(home_gif_slot);
            last_gif_path = NULL;
        }

        if (last_gif_path != gif_path) {
            lv_gif_set_src(home_gif_img, gif_path);
            last_gif_path = gif_path;
        }

        lv_obj_center(home_gif_img);
        lv_obj_clear_flag(home_gif_img, LV_OBJ_FLAG_HIDDEN);
        if (home_effect_label != NULL) {
            lv_obj_add_flag(home_effect_label, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    if (home_gif_img != NULL) {
        lv_obj_add_flag(home_gif_img, LV_OBJ_FLAG_HIDDEN);
    }

    if (home_effect_label != NULL) {
        set_label_text_if_changed(home_effect_label,
                                  fallback_text != NULL ? fallback_text : "");
        if (fallback_text != NULL && fallback_text[0] != '\0') {
            lv_obj_clear_flag(home_effect_label, LV_OBJ_FLAG_HIDDEN);
            lv_obj_center(home_effect_label);
        } else {
            lv_obj_add_flag(home_effect_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void note_user_activity(void)
{
    g_home_last_interaction_tick = lv_tick_get();

    if (g_page_manager.active_page == SCOOTERDEMO_PAGE_HOME &&
        !driver_demo_is_running()) {
        g_vdata.speed = 0;
        snprintf(g_vdata.gear, sizeof(g_vdata.gear), "%s", "P");
        g_home_idle_media_visible = false;
        set_home_center_media_y_offset(SCOOTERDEMO_HOME_GIF_SLOT_IDLE_Y_OFFSET);
        hide_home_center_media();
        set_home_center_speed_visible(true);
    }
}

static void update_home_idle_media(uint32_t tick, driver_demo_effect_t effect)
{
    bool home_page_active;
    bool should_show_idle;

    home_page_active = (g_page_manager.active_page == SCOOTERDEMO_PAGE_HOME);
    should_show_idle = home_page_active &&
                       !driver_demo_is_running() &&
                       effect == DRIVER_DEMO_EFFECT_NONE &&
                       g_home_last_interaction_tick != 0U &&
                       (tick - g_home_last_interaction_tick >= SCOOTERDEMO_HOME_IDLE_TIMEOUT_MS);

    if (should_show_idle) {
        g_home_idle_media_visible = true;
        set_home_center_media_y_offset(SCOOTERDEMO_HOME_GIF_SLOT_IDLE_Y_OFFSET);
        set_home_center_speed_visible(false);
        show_home_center_media(resolve_home_idle_gif_path(tick), "待机");
        return;
    }

    if (effect == DRIVER_DEMO_EFFECT_NONE) {
        g_home_idle_media_visible = false;
        hide_home_center_media();
    }

    set_home_center_speed_visible(true);
}

static const char *resolve_driver_demo_gif_path(driver_demo_effect_t effect)
{
    static const char *effect_gif_cache[3];
    static bool effect_gif_cached[3];
    const char * const *candidates = NULL;
    int effect_index = (int)effect;
    static const char *accel_candidates[] = {
        "/resource/imgs/jiasu.gif",
        "/resource/imgs/driver_demo_accel.gif",
        "/resource/imgs/accel.gif",
        "/resource/imgs/meng.gif",
        NULL
    };
    static const char *brake_candidates[] = {
        "/resource/imgs/jisha.gif",
        "/resource/imgs/driver_demo_brake.gif",
        "/resource/imgs/brake.gif",
        "/resource/imgs/meng.gif",
        NULL
    };

    if (effect == DRIVER_DEMO_EFFECT_NONE || effect_index < 0 || effect_index >= 3) {
        return NULL;
    }

    if (effect_gif_cached[effect_index]) {
        return effect_gif_cache[effect_index];
    }

    candidates = (effect == DRIVER_DEMO_EFFECT_ACCEL) ? accel_candidates : brake_candidates;
    for (int i = 0; candidates[i] != NULL; i++) {
        if (access(candidates[i], F_OK) == 0) {
            effect_gif_cache[effect_index] = candidates[i];
            break;
        }
    }

    effect_gif_cached[effect_index] = true;
    return effect_gif_cache[effect_index];
}

static void update_driver_demo_effect(driver_demo_effect_t effect)
{
    const char *effect_text = NULL;
    const char *gif_path;

    if (home_gif_slot == NULL) {
        return;
    }

    if (effect == DRIVER_DEMO_EFFECT_NONE) {
        hide_home_center_media();
        return;
    }

    set_home_center_media_y_offset(SCOOTERDEMO_HOME_GIF_SLOT_DEMO_Y_OFFSET);
    gif_path = resolve_driver_demo_gif_path(effect);
    effect_text = (effect == DRIVER_DEMO_EFFECT_ACCEL) ? "急加速" : "急刹车";

    show_home_center_media(gif_path, effect_text);

    if (speed_label != NULL) {
        lv_obj_move_foreground(speed_label);
    }
    if (speed_unit_label != NULL) {
        lv_obj_move_foreground(speed_unit_label);
    }
}

static void sync_music_page_snapshot(void)
{
    char music_title_text[SCOOTERDEMO_BT_MUSIC_TITLE_MAX];
    char music_artist_text[SCOOTERDEMO_BT_MUSIC_ARTIST_MAX];

    rivotek_audio_component_get_title_text(music_title_text,
                                           sizeof(music_title_text));
    rivotek_audio_component_get_artist_text(music_artist_text,
                                            sizeof(music_artist_text));

    scooterdemo_music_page_sync(music_title_text,
                                music_artist_text,
                                rivotek_audio_component_is_connected(),
                                rivotek_audio_component_is_playing(),
                                rivotek_audio_component_get_duration_ms());
}

static void sync_music_ui_snapshot(uint32_t tick)
{
    static uint32_t bt_meta_last_tick;
    char music_title_text[SCOOTERDEMO_BT_MUSIC_TITLE_MAX];
    char music_artist_text[SCOOTERDEMO_BT_MUSIC_ARTIST_MAX];

    if (rivotek_audio_component_is_connected() &&
        !rivotek_audio_component_has_metadata() &&
        tick - bt_meta_last_tick >= SCOOTERDEMO_BT_METADATA_FALLBACK_MS) {
        bt_meta_last_tick = tick;
        (void)rivotek_audio_component_request_metadata();
    }

    update_music_album_rotation();

    rivotek_audio_component_get_title_text(music_title_text,
                                           sizeof(music_title_text));
    rivotek_audio_component_get_artist_text(music_artist_text,
                                            sizeof(music_artist_text));

    if (music_title_label) {
        set_label_text_if_changed(music_title_label, music_title_text);
    }
    if (music_artist_label) {
        set_label_text_if_changed(music_artist_label, music_artist_text);
    }

    if (g_page_manager.active_page == SCOOTERDEMO_PAGE_MUSIC) {
        sync_music_page_snapshot();
    }
}

static void sync_music_ui_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    sync_music_ui_snapshot(lv_tick_get());
}

static void update_music_album_rotation(void)
{
    bool should_rotate;

    if (music_album_rotating_img == NULL) {
        return;
    }

    should_rotate = rivotek_audio_component_is_connected() &&
                    rivotek_audio_component_is_playing();
    if (should_rotate == g_music_album_rotation_active) {
        return;
    }

    if (should_rotate) {
        start_album_rotation(music_album_rotating_img);
    } else {
        stop_album_rotation(music_album_rotating_img);
    }

    g_music_album_rotation_active = should_rotate;
}

// Font Initialization
static void init_fonts(void)
{
    #ifdef LV_USE_FREETYPE
    /* Load Scaled Fonts */
    font_16 = lv_freetype_font_create("/resource/fonts/MiSans-Normal.ttf",
                                               LV_FREETYPE_FONT_RENDER_MODE_BITMAP, F(16),
                                               LV_FREETYPE_FONT_STYLE_NORMAL);
    font_20 = lv_freetype_font_create("/resource/fonts/MiSans-Normal.ttf",
                                               LV_FREETYPE_FONT_RENDER_MODE_BITMAP, F(20),
                                               LV_FREETYPE_FONT_STYLE_NORMAL);
    font_ready_26 = lv_freetype_font_create("/resource/fonts/dinpro_black.otf",
                                            LV_FREETYPE_FONT_RENDER_MODE_BITMAP, F(26),
                                            LV_FREETYPE_FONT_STYLE_NORMAL);
    font_info_20 = lv_freetype_font_create("/resource/fonts/dinpro_bold.otf",
                                           LV_FREETYPE_FONT_RENDER_MODE_BITMAP, F(20),
                                           LV_FREETYPE_FONT_STYLE_NORMAL);
    font_status_26 = lv_freetype_font_create("/resource/fonts/dinpro_bold.otf",
                                             LV_FREETYPE_FONT_RENDER_MODE_BITMAP, F(26),
                                             LV_FREETYPE_FONT_STYLE_NORMAL);
    font_theme_22 = lv_freetype_font_create("/resource/fonts/MiSans-Bold.ttf",
                                            LV_FREETYPE_FONT_RENDER_MODE_BITMAP, F(22),
                                            LV_FREETYPE_FONT_STYLE_NORMAL);
    font_weather_city_18 = lv_freetype_font_create("/resource/fonts/MiSans-Medium.ttf",
                                                   LV_FREETYPE_FONT_RENDER_MODE_BITMAP, F(18),
                                                   LV_FREETYPE_FONT_STYLE_NORMAL);
    font_weather_temp_32 = lv_freetype_font_create("/resource/fonts/dinpro.otf",
                                                   LV_FREETYPE_FONT_RENDER_MODE_BITMAP, F(32),
                                                   LV_FREETYPE_FONT_STYLE_NORMAL);
    font_weather_text_18 = lv_freetype_font_create("/resource/fonts/MiSans-Medium.ttf",
                                                   LV_FREETYPE_FONT_RENDER_MODE_BITMAP, F(18),
                                                   LV_FREETYPE_FONT_STYLE_NORMAL);
    font_gear_current_56 = lv_freetype_font_create("/resource/fonts/dinpro_black.otf",
                                                   LV_FREETYPE_FONT_RENDER_MODE_BITMAP, F(56),
                                                   LV_FREETYPE_FONT_STYLE_NORMAL);
    font_gear_side_24 = lv_freetype_font_create("/resource/fonts/dinpro_medium.otf",
                                                LV_FREETYPE_FONT_RENDER_MODE_BITMAP, F(24),
                                                LV_FREETYPE_FONT_STYLE_NORMAL);
    font_24 = lv_freetype_font_create("/resource/fonts/MiSans-Normal.ttf",
                                               LV_FREETYPE_FONT_RENDER_MODE_BITMAP, F(24),
                                               LV_FREETYPE_FONT_STYLE_NORMAL);
    font_unit_24 = lv_freetype_font_create("/resource/fonts/MiSans-Regular.ttf",
                                           LV_FREETYPE_FONT_RENDER_MODE_BITMAP, F(24),
                                           LV_FREETYPE_FONT_STYLE_NORMAL);
    font_28 = lv_freetype_font_create("/resource/fonts/MiSans-Normal.ttf",
                                               LV_FREETYPE_FONT_RENDER_MODE_BITMAP, F(28),
                                               LV_FREETYPE_FONT_STYLE_NORMAL);
    font_40 = lv_freetype_font_create("/resource/fonts/MiSans-Normal.ttf",
                                               LV_FREETYPE_FONT_RENDER_MODE_BITMAP, F(40),
                                               LV_FREETYPE_FONT_STYLE_NORMAL);
    font_48 = lv_freetype_font_create("/resource/fonts/MiSans-Normal.ttf",
                                               LV_FREETYPE_FONT_RENDER_MODE_BITMAP, F(48),
                                               LV_FREETYPE_FONT_STYLE_NORMAL);
    font_speed_134 = lv_freetype_font_create("/resource/fonts/dinpro.otf",
                                             LV_FREETYPE_FONT_RENDER_MODE_BITMAP, F(134),
                                             LV_FREETYPE_FONT_STYLE_NORMAL);
                                               
    /* Fallback Logic */
    if(font_16 == NULL) {
        font_16 = &lv_font_montserrat_16;
        printf("Failed to load font 16, fallback to default\n");
    }
    if(font_20 == NULL) {
        font_20 = &lv_font_montserrat_20;
        printf("Failed to load font 20, fallback to default\n");
    }
    if(font_ready_26 == NULL) {
        font_ready_26 = &lv_font_montserrat_28;
        printf("Failed to load ready font 26, fallback to default\n");
    }
    if(font_info_20 == NULL) {
        font_info_20 = &lv_font_montserrat_20;
        printf("Failed to load info font 20, fallback to default\n");
    }
    if(font_status_26 == NULL) {
        font_status_26 = &lv_font_montserrat_28;
        printf("Failed to load status font 26, fallback to default\n");
    }
    if(font_theme_22 == NULL) {
        font_theme_22 = &lv_font_montserrat_20;
        printf("Failed to load theme font 22, fallback to default\n");
    }
    if(font_weather_city_18 == NULL) {
        font_weather_city_18 = &lv_font_montserrat_20;
        printf("Failed to load weather city font 18, fallback to default\n");
    }
    if(font_weather_temp_32 == NULL) {
        font_weather_temp_32 = &lv_font_montserrat_40;
        printf("Failed to load weather temp font 32, fallback to default\n");
    }
    if(font_weather_text_18 == NULL) {
        font_weather_text_18 = &lv_font_montserrat_20;
        printf("Failed to load weather text font 18, fallback to default\n");
    }
    if(font_gear_current_56 == NULL) {
        font_gear_current_56 = &lv_font_montserrat_48;
        printf("Failed to load gear current font 56, fallback to default\n");
    }
    if(font_gear_side_24 == NULL) {
        font_gear_side_24 = &lv_font_montserrat_24;
        printf("Failed to load gear side font 24, fallback to default\n");
    }
    if(font_24 == NULL) {
        font_24 = &lv_font_montserrat_24;
        //printf("Failed to load font 24, fallback to default\n");
    }
    if(font_unit_24 == NULL) {
        font_unit_24 = &lv_font_montserrat_24;
        printf("Failed to load unit font 24, fallback to default\n");
    }
    if(font_28 == NULL) {
        font_28 = &lv_font_montserrat_28;
        printf("Failed to load font 28, fallback to default\n");
    }
    if(font_40 == NULL) {
        font_40 = &lv_font_montserrat_40;
        printf("Failed to load font 40, fallback to default\n");
    }
    if(font_48 == NULL) {
        font_48 = &lv_font_montserrat_48;
        printf("Failed to load font 48, fallback to default\n");
    }
    if(font_speed_134 == NULL) {
        font_speed_134 = &lv_font_montserrat_48;
        printf("Failed to load speed font 134, fallback to default\n");
    }
    #else
    /* No FreeType Support */
    extern lv_font_t lv_font_simsun_16_cjk; 
    font_16 = &lv_font_simsun_16_cjk;
    font_20 = &lv_font_montserrat_20;
    font_ready_26 = &lv_font_montserrat_28;
    font_info_20 = &lv_font_montserrat_20;
    font_status_26 = &lv_font_montserrat_28;
    font_theme_22 = &lv_font_montserrat_20;
    font_weather_city_18 = &lv_font_montserrat_20;
    font_weather_temp_32 = &lv_font_montserrat_40;
    font_weather_text_18 = &lv_font_montserrat_20;
    font_gear_current_56 = &lv_font_montserrat_48;
    font_gear_side_24 = &lv_font_montserrat_24;
    font_24 = &lv_font_montserrat_24;
    font_unit_24 = &lv_font_montserrat_24;
    font_28 = &lv_font_montserrat_28;
    font_40 = &lv_font_montserrat_40;
    font_48 = &lv_font_montserrat_48;
    font_speed_134 = &lv_font_montserrat_48;
    #endif
}

// Interface Function
VehicleData* get_vehicle_data(void) {
    return &g_vdata;
}

// Update Simulation Data
static void update_simulation(void) {
    static bool accelerating = true;
    static bool default_gear_applied = false;

    if (!default_gear_applied) {
        strcpy(g_vdata.gear, "P");
        g_vdata.speed = 0;
        default_gear_applied = true;
    }

    // Simulate Speed based on gear
    if (strcmp(g_vdata.gear, "D") == 0 || strcmp(g_vdata.gear, "S") == 0) {
        if (accelerating) {
            g_vdata.speed += (strcmp(g_vdata.gear, "S") == 0) ? 3 : 2; // S档加速更快
            if (g_vdata.speed > 120) accelerating = false;
        } else {
            g_vdata.speed -= 2;
            if (g_vdata.speed <= 0) {
                g_vdata.speed = 0;
                accelerating = true;
            }
        }
    } else if (strcmp(g_vdata.gear, "R") == 0) {
        // 倒车档速度较慢
        if (accelerating) {
            g_vdata.speed += 1;
            if (g_vdata.speed > 20) accelerating = false;
        } else {
            g_vdata.speed -= 1;
            if (g_vdata.speed <= 0) {
                g_vdata.speed = 0;
                accelerating = true;
            }
        }
    } else {
        // P 或 N 档速度快速归零
        if (g_vdata.speed > 0) {
            g_vdata.speed -= 5;
            if (g_vdata.speed < 0) g_vdata.speed = 0;
        } else {
            accelerating = true;
        }
    }

}

// Update UI from Data
static void update_ui_timer_cb(lv_timer_t *timer) {
    uint32_t tick = lv_tick_get();
    static char last_gear[sizeof(g_vdata.gear)] = "";
    static bool ai_session_active_last;
    static int last_page_index = -1;
    driver_demo_output_t driver_demo_output = {0};
    bool driver_demo_override;
    bool home_page_active;
    bool ai_session_active;
    (void)timer;

    home_page_active = (g_page_manager.active_page == SCOOTERDEMO_PAGE_HOME);

    driver_demo_override = driver_demo_update(&driver_demo_output, tick);
    if (driver_demo_override) {
        g_vdata.speed = driver_demo_output.speed;
        snprintf(g_vdata.gear, sizeof(g_vdata.gear), "%s", driver_demo_output.gear);
    } else if (home_page_active) {
        g_vdata.speed = 0;
        snprintf(g_vdata.gear, sizeof(g_vdata.gear), "%s", "P");
    } else {
        update_simulation();
    }

    VehicleData *data = get_vehicle_data();

    // Update Time
    rivotek_time_component_update(tick);
    if (time_label) {
        set_label_text_if_changed(time_label, rivotek_time_component_get_text());
    }
    if (home_time_label) {
        set_label_text_if_changed(home_time_label, rivotek_time_component_get_text());
    }

    // Update Weather
    const char *weather_city;
    const char *temp_str;
    const char *weather_desc;
    const char *air_quality;

    weather_city = rivotek_weather_component_get_city_text();
    temp_str = rivotek_weather_component_get_temp_text();
    weather_desc = rivotek_weather_component_get_desc_text();
    air_quality = rivotek_weather_component_get_air_quality_text();

    if (top_temp_label) {
        set_label_text_if_changed(top_temp_label, temp_str);
    }
    if (home_top_temp_label) {
        set_label_text_if_changed(home_top_temp_label, temp_str);
    }
    if (left_temp_label) {
        set_label_text_if_changed(left_temp_label, temp_str);
    }
    if (weather_city_label) {
        set_label_text_if_changed(weather_city_label, weather_city);
    }
    if (weather_air_label) {
        set_label_text_if_changed(weather_air_label, air_quality);
    }
    if (weather_desc_label) set_label_text_if_changed(weather_desc_label, weather_desc);
    update_weather_icon(weather_desc);
    update_navigation_map_for_city(weather_city);

    // Update Speed
    char speed_str[10];
    snprintf(speed_str, sizeof(speed_str), "%d", data->speed);
    if(speed_label) set_label_text_if_changed(speed_label, speed_str);

    // Update Gear Highlight only when value changed
    if (strcmp(last_gear, data->gear) != 0) {
        refresh_gear_highlight(data->gear);
        snprintf(last_gear, sizeof(last_gear), "%s", data->gear);
    }
    update_driver_demo_effect(driver_demo_output.effect);
    update_home_idle_media(tick, driver_demo_output.effect);

    /* Poll AI assistant mqueue for incoming commands */
    scooterdemo_ai_poll();
#ifdef CONFIG_RIVOTEK_QRCODE_SERVICE
    rvt_qrcode_poll();
#endif

    ai_session_active = scooterdemo_ai_is_session_active();
    if (ai_session_active != ai_session_active_last ||
        last_page_index != g_page_manager.active_page) {
        uint32_t double_click_mask;

        ai_session_active_last = ai_session_active;
        last_page_index = g_page_manager.active_page;
        if (ai_session_active) {
            double_click_mask = BUTTON_PROCESSOR_KEY_MASK_DOWN;
        } else if (g_page_manager.active_page == SCOOTERDEMO_PAGE_HOME) {
            double_click_mask = BUTTON_PROCESSOR_KEY_MASK_UP |
                                BUTTON_PROCESSOR_KEY_MASK_DOWN;
        } else if (g_page_manager.active_page == SCOOTERDEMO_PAGE_NAVIGATION) {
            double_click_mask = BUTTON_PROCESSOR_KEY_MASK_OK;
        } else {
            double_click_mask = BUTTON_PROCESSOR_KEY_MASK_OK;
        }
        key_processor_set_double_click_mask(double_click_mask);
    }
    update_ai_nav_status_icons();
}

// Create Top Status Bar
static lv_obj_t *create_status_bar(lv_obj_t *parent,
                                   lv_obj_t **time_value_label,
                                   lv_obj_t **mode_value_label,
                                   lv_obj_t **temp_value_label,
                                   lv_obj_t **ai_status_icon)
{
    static const char *icon_paths[] = {
        "/resource/imgs/bt.png",
        "/resource/imgs/wifi.png",
        "/resource/imgs/4g.png"
    };
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, disp_w, H(50));
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_0, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, W(10), 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(bar, LV_SCROLLBAR_MODE_OFF);

    // Left: READY
    lv_obj_t *ready_label = lv_label_create(bar);
    lv_label_set_text(ready_label, "READY");
    lv_obj_set_style_text_color(ready_label,lv_color_hex(0x006400), 0);
    lv_obj_set_style_text_font(ready_label, font_ready_26, 0);
    lv_obj_align(ready_label, LV_ALIGN_LEFT_MID, W(20), 0);

    // Center Container
    lv_obj_t *center_info = lv_obj_create(bar);
    lv_obj_set_size(center_info, W(400), H(40));
    lv_obj_align(center_info, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(center_info, LV_OPA_0, 0);
    lv_obj_set_style_border_width(center_info, 0, 0);
    // 全局隐藏容器的滚动条
    lv_obj_set_scrollbar_mode(center_info, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(center_info, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(center_info, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(center_info, W(28), 0);

    // Time
    *time_value_label = lv_label_create(center_info);
    lv_label_set_text(*time_value_label, "00:00");
    lv_obj_set_style_text_color(*time_value_label, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(*time_value_label, font_status_26, 0);
    lv_obj_set_style_bg_opa(*time_value_label, LV_OPA_0, 0);

    // City Mode
    *mode_value_label = lv_label_create(center_info);
    lv_label_set_text(*mode_value_label,
                      get_theme_mode_name(rivotek_theme_component_get_active_index()));
    lv_obj_set_style_text_color(*mode_value_label, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(*mode_value_label, font_theme_22, 0);
    lv_obj_set_style_bg_opa(*mode_value_label, LV_OPA_0, 0);

    // Temp
    *temp_value_label = lv_label_create(center_info);
    lv_label_set_text(*temp_value_label, "19°C");
    lv_obj_set_style_text_color(*temp_value_label, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(*temp_value_label, font_status_26, 0);
    lv_obj_set_style_bg_opa(*temp_value_label, LV_OPA_0, 0);

    // Right: Icons
    lv_obj_t *right_icons = lv_obj_create(bar);
    lv_obj_set_size(right_icons, W(197), H(40));
    lv_obj_align(right_icons, LV_ALIGN_RIGHT_MID, W(0), 0);
    lv_obj_set_style_bg_opa(right_icons, LV_OPA_0, 0);
    lv_obj_set_style_border_width(right_icons, 0, 0);
    lv_obj_set_style_pad_all(right_icons, 0, 0);
    lv_obj_set_style_pad_gap(right_icons, W(10), 0);
    lv_obj_set_scrollbar_mode(right_icons, LV_SCROLLBAR_MODE_OFF);

    lv_obj_set_flex_flow(right_icons, LV_FLEX_FLOW_ROW);
    // 使用 LV_FLEX_ALIGN_END 让图标向右对齐
    lv_obj_set_flex_align(right_icons,
                          LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    if (ai_status_icon != NULL) {
        *ai_status_icon = lv_img_create(right_icons);
        lv_img_set_src(*ai_status_icon, SCOOTERDEMO_AI_NAV_STATUS_ICON_PATH);
        lv_obj_set_size(*ai_status_icon, W(32), H(32));
        lv_obj_set_style_bg_opa(*ai_status_icon, LV_OPA_0, 0);
        lv_obj_add_flag(*ai_status_icon, LV_OBJ_FLAG_HIDDEN);
    }

    for (int i = 0; i < 3; i++) {
        lv_obj_t *img = lv_img_create(right_icons);

        lv_img_set_src(img, icon_paths[i]);
        lv_obj_set_size(img, W(35), H(35));
        lv_obj_set_style_bg_opa(img, LV_OPA_0, 0);
    }

    return bar;
}

static void create_top_bar(lv_obj_t *parent)
{
    top_bar = create_status_bar(parent,
                                &time_label,
                                &top_mode_label,
                                &top_temp_label,
                                &top_ai_status_icon);
}

static void create_home_top_bar(lv_obj_t *parent)
{
    home_top_bar = create_status_bar(parent,
                                     &home_time_label,
                                     &home_top_mode_label,
                                     &home_top_temp_label,
                                     &home_top_ai_status_icon);
}

// Create Left Panel (Weather & Location)
static void create_left_panel(lv_obj_t *parent)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_size(panel, W(230), H(280));
    lv_obj_align(panel, LV_ALIGN_RIGHT_MID, W(-42), H(8));
    lv_obj_set_style_bg_opa(panel, LV_OPA_0, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_OFF);

    // Location and air quality
    lv_obj_t *loc_cont = lv_obj_create(panel);
    lv_obj_remove_style_all(loc_cont);
    lv_obj_set_size(loc_cont, W(220), H(40));
    lv_obj_set_style_bg_opa(loc_cont, LV_OPA_0, 0);
    lv_obj_set_style_border_width(loc_cont, 0, 0);
    lv_obj_set_scrollbar_mode(loc_cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(loc_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(loc_cont,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(loc_cont, W(7), 0);
    lv_obj_align(loc_cont, LV_ALIGN_TOP_MID, 0, H(6));

    lv_obj_t *city_cont = lv_obj_create(loc_cont);
    lv_obj_remove_style_all(city_cont);
    lv_obj_set_size(city_cont, LV_SIZE_CONTENT, H(30));
    lv_obj_set_style_bg_opa(city_cont, LV_OPA_0, 0);
    lv_obj_set_style_border_width(city_cont, 0, 0);
    lv_obj_set_scrollbar_mode(city_cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(city_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(city_cont,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(city_cont, W(3), 0);

    lv_obj_t *pin_icon = lv_label_create(city_cont);
    lv_label_set_text(pin_icon, LV_SYMBOL_GPS);
    lv_obj_set_style_text_color(pin_icon, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(pin_icon, &lv_font_montserrat_16, 0);

    weather_city_label = lv_label_create(city_cont);
    lv_label_set_text(weather_city_label, "定位中");
    lv_obj_set_style_text_color(weather_city_label, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(weather_city_label, font_weather_city_18, 0);

    weather_air_label = lv_label_create(loc_cont);
    lv_label_set_text(weather_air_label, "空气--");
    lv_obj_set_style_text_color(weather_air_label, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(weather_air_label, font_weather_text_18, 0);

    // Top image slot above the temperature area
    lv_obj_t *temp_top_img_slot = lv_obj_create(panel);
    lv_obj_set_size(temp_top_img_slot, W(120), H(96));
    lv_obj_align(temp_top_img_slot, LV_ALIGN_CENTER, W(10), H(-45));
    lv_obj_set_style_bg_opa(temp_top_img_slot, LV_OPA_0, 0);
    lv_obj_set_style_border_width(temp_top_img_slot, 0, 0);
    lv_obj_set_style_pad_all(temp_top_img_slot, 0, 0);
    lv_obj_set_scrollbar_mode(temp_top_img_slot, LV_SCROLLBAR_MODE_OFF);

    weather_icon_img = lv_img_create(temp_top_img_slot);
    lv_obj_center(weather_icon_img);

    weather_icon_fallback_label = lv_label_create(temp_top_img_slot);
    lv_label_set_text(weather_icon_fallback_label, LV_SYMBOL_WARNING);
    lv_obj_set_style_text_color(weather_icon_fallback_label, lv_color_hex(0xFFA500), 0);
    lv_obj_set_style_text_font(weather_icon_fallback_label, font_48, 0);
    lv_obj_add_flag(weather_icon_fallback_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_center(weather_icon_fallback_label);

    // Temperature
    left_temp_label = lv_label_create(panel);
    lv_label_set_text(left_temp_label, "19°C");
    lv_obj_set_style_text_color(left_temp_label, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(left_temp_label, font_weather_temp_32, 0);
    lv_obj_align(left_temp_label, LV_ALIGN_BOTTOM_MID, W(-28), H(-80));

    // Description
    weather_desc_label = lv_label_create(panel);
    lv_label_set_text(weather_desc_label, "多云");
    lv_obj_set_style_text_color(weather_desc_label, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(weather_desc_label, font_weather_text_18, 0);
    lv_obj_align_to(weather_desc_label, left_temp_label, LV_ALIGN_OUT_RIGHT_MID, W(35), 0);

    update_weather_icon(rivotek_weather_component_get_desc_text());
}

// Create Center Speedometer
static void create_center_meter(lv_obj_t *parent)
{
    lv_obj_t *cont = lv_obj_create(parent);

    lv_obj_remove_style_all(cont);
    lv_obj_set_size(cont, W(320), H(320));
    lv_obj_align(cont, LV_ALIGN_CENTER, 0, H(-8));
    lv_obj_set_style_bg_opa(cont, LV_OPA_0, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    speed_bg_slot = lv_obj_create(cont);
    lv_obj_remove_style_all(speed_bg_slot);
    lv_obj_set_size(speed_bg_slot, W(300), H(300));
    lv_obj_align(speed_bg_slot, LV_ALIGN_CENTER, 0, H(-10));
    lv_obj_set_style_bg_opa(speed_bg_slot, LV_OPA_0, 0);
    lv_obj_set_style_border_width(speed_bg_slot, 0, 0);
    lv_obj_clear_flag(speed_bg_slot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(speed_bg_slot, LV_OBJ_FLAG_HIDDEN);

    speed_bg_img = lv_img_create(speed_bg_slot);
    lv_obj_center(speed_bg_img);

    driving_speed_bg_slot = lv_obj_create(cont);
    lv_obj_remove_style_all(driving_speed_bg_slot);
    lv_obj_set_size(driving_speed_bg_slot, W(300), H(300));
    lv_obj_align(driving_speed_bg_slot, LV_ALIGN_CENTER, 0, H(-10));
    lv_obj_set_style_bg_opa(driving_speed_bg_slot, LV_OPA_0, 0);
    lv_obj_set_style_border_width(driving_speed_bg_slot, 0, 0);
    lv_obj_clear_flag(driving_speed_bg_slot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(driving_speed_bg_slot, LV_OBJ_FLAG_HIDDEN);

    driving_speed_bg_img = lv_img_create(driving_speed_bg_slot);
    lv_obj_center(driving_speed_bg_img);

    rivotek_theme_component_bind_speed_background(speed_bg_img, driving_speed_bg_img);

    speed_label = lv_label_create(cont);
    lv_label_set_text(speed_label, "0");
    lv_obj_set_style_text_color(speed_label, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(speed_label, font_speed_134, 0);
    lv_obj_set_style_text_align(speed_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_opa(speed_label, LV_OPA_0, 0);
    lv_obj_align(speed_label, LV_ALIGN_CENTER, 0, H(-16));

    speed_unit_label = lv_label_create(cont);
    lv_label_set_text(speed_unit_label, "Km/h");
    lv_obj_set_style_text_color(speed_unit_label, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(speed_unit_label, font_unit_24, 0);
    lv_obj_set_style_text_align(speed_unit_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_opa(speed_unit_label, LV_OPA_0, 0);
    lv_obj_align_to(speed_unit_label, speed_label, LV_ALIGN_OUT_BOTTOM_MID, 0, H(8));

    lv_obj_move_foreground(cont);
}

// Create Right Panel (Bluetooth Music)
static void create_right_panel(lv_obj_t *parent)
{
    bool has_top_layer;
    bool has_bottom_layer;
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, W(230), H(300));
    lv_obj_align(panel, LV_ALIGN_LEFT_MID, W(20), H(22));
    lv_obj_set_style_bg_opa(panel, LV_OPA_0, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_OFF);

    has_top_layer = (access("/resource/imgs/top_layer.png", F_OK) == 0);
    has_bottom_layer = false;

    // Album Art Placeholder (Circle / Image)
    lv_obj_t *album_art = lv_obj_create(panel);
    lv_obj_remove_style_all(album_art);
    // Accommodate rotating bounding box (sqrt(2) * 99 ~ 140)
    lv_obj_set_size(album_art, W(140), H(140));
    // Adjust position to center both layers properly
    lv_obj_align(album_art, LV_ALIGN_TOP_MID, 0, H(30));
    lv_obj_set_style_bg_opa(album_art, LV_OPA_0, 0);
    lv_obj_set_style_border_width(album_art, 0, 0);
    lv_obj_clear_flag(album_art, LV_OBJ_FLAG_SCROLLABLE);

    // Replace the old bottom layer image with a new 48x60 container,
    // aligned to the top-right position of the original 108x108 layer.
    lv_obj_t *bottom_layer_cont = lv_obj_create(album_art);
    lv_obj_remove_style_all(bottom_layer_cont);
    lv_obj_set_size(bottom_layer_cont, W(48), H(60));
    lv_obj_align(bottom_layer_cont, LV_ALIGN_TOP_MID, W(30), H(16));
    lv_obj_set_style_bg_opa(bottom_layer_cont, LV_OPA_0, 0);
    lv_obj_set_style_border_width(bottom_layer_cont, 0, 0);
    lv_obj_set_style_pad_all(bottom_layer_cont, 0, 0);
    lv_obj_clear_flag(bottom_layer_cont, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *bottom_layer_img = lv_img_create(bottom_layer_cont);
    lv_obj_center(bottom_layer_img);
    rivotek_theme_component_bind_bottom_layer(bottom_layer_img);
    has_bottom_layer = (lv_img_get_src(bottom_layer_img) != NULL);
    
    // Create top layer image (smaller: 99x99)
    if (has_top_layer) {
        lv_obj_t *top_layer_img = lv_img_create(album_art);
        lv_img_set_src(top_layer_img, "/resource/imgs/top_layer.png");
        lv_obj_set_size(top_layer_img, W(99), H(99));
        lv_obj_center(top_layer_img);
        lv_obj_set_style_transform_pivot_x(top_layer_img, W(99) / 2, 0);
        lv_obj_set_style_transform_pivot_y(top_layer_img, H(99) / 2, 0);

        music_album_rotating_img = top_layer_img;
        g_music_album_rotation_active = false;
    }
    
    // Optional: Fallback music icon if images don't load
    music_album_fallback_icon = lv_label_create(album_art);
    lv_label_set_text(music_album_fallback_icon, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_color(music_album_fallback_icon, lv_color_hex(0xF0F0F0), 0);
    lv_obj_set_style_text_font(music_album_fallback_icon, font_28, 0);
    lv_obj_center(music_album_fallback_icon);

    if (has_bottom_layer && has_top_layer) {
        lv_obj_add_flag(music_album_fallback_icon, LV_OBJ_FLAG_HIDDEN);
    }

    // Song Info
    music_title_label = lv_label_create(panel);
    lv_label_set_text(music_title_label, "等待蓝牙音频");
    lv_obj_set_style_text_color(music_title_label, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(music_title_label, font_20, 0);
    lv_obj_set_style_bg_opa(music_title_label, LV_OPA_0, 0);
    lv_label_set_long_mode(music_title_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(music_title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(music_title_label, W(130));
    // 调整歌名位置以适应变小的专辑图
    lv_obj_align(music_title_label, LV_ALIGN_TOP_MID, 0, H(150));

    music_artist_label = lv_label_create(panel);
    lv_label_set_text(music_artist_label, "未连接设备");
    lv_obj_set_style_text_color(music_artist_label, COLOR_GRAY, 0);
    lv_obj_set_style_text_font(music_artist_label, font_16, 0);
    lv_obj_set_style_bg_opa(music_artist_label, LV_OPA_0, 0);
    lv_obj_set_style_text_align(music_artist_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(music_artist_label, W(150));
    lv_obj_align(music_artist_label, LV_ALIGN_TOP_MID, 0, H(185));


}
static scooterdemo_ui_metrics_t get_ui_metrics(void)
{
    scooterdemo_ui_metrics_t ui = {
        .disp_w = disp_w,
        .disp_h = disp_h,
        .font_16 = font_16,
        .font_20 = font_20,
        .font_24 = font_24,
        .font_unit_24 = font_unit_24,
        .font_28 = font_28,
        .font_40 = font_40,
        .font_48 = font_48,
        .font_speed_134 = font_speed_134,
    };

    return ui;
}

static void create_home_background_layers(lv_obj_t *parent)
{
    home_bg_base = lv_obj_create(parent);
    lv_obj_remove_style_all(home_bg_base);
    lv_obj_set_size(home_bg_base, lv_pct(100), lv_pct(100));
    lv_obj_align(home_bg_base, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(home_bg_base, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(home_bg_base, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(home_bg_base, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(home_bg_base, 0, 0);
    lv_obj_set_style_radius(home_bg_base, 0, 0);

    home_gif_slot = lv_obj_create(parent);
    lv_obj_remove_style_all(home_gif_slot);
    lv_obj_set_size(home_gif_slot, W(320), H(320));
    lv_obj_align(home_gif_slot, LV_ALIGN_CENTER, 0, SCOOTERDEMO_HOME_GIF_SLOT_IDLE_Y_OFFSET);
    lv_obj_clear_flag(home_gif_slot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(home_gif_slot, LV_OPA_0, 0);
    lv_obj_set_style_border_width(home_gif_slot, 0, 0);
    lv_obj_set_style_radius(home_gif_slot, 0, 0);
    lv_obj_set_style_clip_corner(home_gif_slot, false, 0);
    lv_obj_center(home_gif_slot);

    home_gif_img = NULL;
    home_effect_label = lv_label_create(home_gif_slot);
    lv_label_set_text(home_effect_label, "");
    lv_obj_set_style_text_color(home_effect_label, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(home_effect_label, font_40, 0);
    lv_obj_set_style_text_align(home_effect_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(home_effect_label);
    lv_obj_add_flag(home_effect_label, LV_OBJ_FLAG_HIDDEN);

    home_bg_img = lv_img_create(parent);
    lv_obj_add_flag(home_bg_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_img_recolor_opa(home_bg_img, LV_OPA_30, 0);
    lv_obj_set_style_img_recolor(home_bg_img, lv_color_hex(0x000000), 0);
    lv_obj_align(home_bg_img, LV_ALIGN_CENTER, 0, 0);
    rivotek_theme_component_bind_home_background(home_bg_img);

    lv_obj_move_background(home_bg_base);
    lv_obj_add_flag(home_gif_slot, LV_OBJ_FLAG_HIDDEN);
}

static void switch_page_now(int page_index)
{
    bool is_home_page;
    bool is_navigation_page;
    bool is_settings_page;
    bool is_music_page;
    bool hide_sidebar;

    if (page_index < 0 || page_index >= SCOOTERDEMO_PAGE_COUNT) {
        return;
    }

    is_home_page = (page_index == SCOOTERDEMO_PAGE_HOME);
    is_navigation_page = (page_index == SCOOTERDEMO_PAGE_NAVIGATION);
    is_settings_page = (page_index == SCOOTERDEMO_PAGE_SETTINGS);
    is_music_page = (page_index == SCOOTERDEMO_PAGE_MUSIC);
    hide_sidebar = is_settings_page || is_music_page;

    scooterdemo_page_manager_show(&g_page_manager,
                                  (scooterdemo_page_id_t)page_index);

    if (right_sidebar != NULL) {
        if (hide_sidebar) {
            lv_obj_add_flag(right_sidebar, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(right_sidebar, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(right_sidebar);
        }
    }

    if (top_bar != NULL) {
        if (is_home_page) {
            lv_obj_add_flag(top_bar, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(top_bar, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(top_bar);
        }
    }

    if (home_top_bar != NULL) {
        if (is_home_page) {
            lv_obj_clear_flag(home_top_bar, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(home_top_bar);
        } else {
            lv_obj_add_flag(home_top_bar, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (is_settings_page) {
        scooterdemo_settings_page_on_show();
        if (main_containers[SCOOTERDEMO_PAGE_SETTINGS] != NULL) {
            lv_obj_invalidate(main_containers[SCOOTERDEMO_PAGE_SETTINGS]);
        }
    }

    if (is_music_page) {
        scooterdemo_music_page_on_show();
        sync_music_page_snapshot();
    }

    if (is_navigation_page) {
        update_navigation_map_for_city(rivotek_weather_component_get_city_text());
    }

    if (!is_settings_page) {
        sync_sidebar_focus_with_page(page_index);
    }

    if (page_index >= 0 && page_index < SCOOTERDEMO_PAGE_COUNT &&
        main_containers[page_index] != NULL) {
        lv_obj_invalidate(main_containers[page_index]);
    }
    scooterdemo_ai_notify_navigation_page(is_navigation_page);
    lv_obj_invalidate(screen);
    lv_refr_now(NULL);

    if (is_home_page) {
        note_user_activity();
    }

    update_page_button_handler(page_index);
}

static void page_switch_timer_cb(lv_timer_t *timer)
{
    int page_index = g_pending_page_index;

    g_pending_page_index = -1;
    if (g_page_switch_timer == timer) {
        g_page_switch_timer = NULL;
    }
    lv_timer_del(timer);

    if (page_index >= 0) {
        switch_page_now(page_index);
    }
}

static void request_page_switch(int page_index)
{
    if (page_index < 0 || page_index >= SCOOTERDEMO_PAGE_COUNT) {
        return;
    }

    g_pending_page_index = page_index;

    if (g_page_switch_timer != NULL) {
        lv_timer_del(g_page_switch_timer);
        g_page_switch_timer = NULL;
    }

    g_page_switch_timer = lv_timer_create(page_switch_timer_cb,
                                          SCOOTERDEMO_PAGE_SWITCH_DELAY_MS,
                                          NULL);
    if (g_page_switch_timer == NULL) {
        switch_page_now(g_pending_page_index);
        g_pending_page_index = -1;
    }
}

static void ai_request_page_switch(int page_index, void *user_data)
{
    (void)user_data;
    request_page_switch(page_index);
}

static void sync_sidebar_focus_with_page(int page_index)
{
    if (page_index < 0 || page_index >= sidebar_btn_count || sidebar_group == NULL) {
        return;
    }

    lv_group_focus_obj(sidebar_btns[page_index]);
    update_sidebar_focus_indicator();
}

static void go_back_one_level(void)
{
    if (g_page_manager.active_page == SCOOTERDEMO_PAGE_SETTINGS &&
        scooterdemo_settings_page_handle_back()) {
        return;
    }

    if (g_page_manager.active_page == SCOOTERDEMO_PAGE_HOME) {
        return;
    }

    request_page_switch(g_page_manager.previous_page);
}

static void update_sidebar_focus_indicator(void)
{
    lv_obj_t *focused = lv_group_get_focused(sidebar_group);

    if (sidebar_group == NULL) {
        return;
    }

    for (int i = 0; i < sidebar_btn_count; i++) {
        lv_obj_t *img = sidebar_btns[i];
        bool is_selected = (focused != NULL) ? (img == focused)
                                            : (i == g_page_manager.active_page);
        
        if (is_selected) {
            lv_obj_add_state(img, LV_STATE_CHECKED);
            (void)rivotek_theme_component_apply_sidebar_item(img, i, true);
        } else {
            lv_obj_clear_state(img, LV_STATE_CHECKED);
            (void)rivotek_theme_component_apply_sidebar_item(img, i, false);
        }
    }
}

static void sidebar_btn_click_cb(lv_event_t *e)
{
    static uint32_t last_click_tick;
    lv_obj_t *target = lv_event_get_current_target(e); 
    uint32_t now = lv_tick_get();

    if (now - last_click_tick < SIDEBAR_TOUCH_DEBOUNCE_MS) {
        return;
    }

    last_click_tick = now;
    note_user_activity();
    
    // 触摸点击时，首先让焦点移过去（这会更新切图）
    if (sidebar_group) {
        lv_group_focus_obj(target);
        update_sidebar_focus_indicator();
    }
    
    // 点击或按下Enter键时，进行界面的实际切换
    for (int i = 0; i < sidebar_btn_count; i++) {
        if (sidebar_btns[i] == target) {
            request_page_switch(i);
            break;
        }
    }
}

static void handle_sidebar_confirm(void)
{
    lv_obj_t *focused;

    focused = lv_group_get_focused(sidebar_group);
    if (focused != NULL) {
        lv_obj_send_event(focused, LV_EVENT_CLICKED, NULL);
    }
}

static void sidebar_page_button_event_cb(KEYCODE keycode, BUTTON_EVENT event, void *user_data)
{
    bool home_page_active;
    bool navigation_page_active;
    bool ai_session_active;

    (void)user_data;
    if (event == BUTTON_EVENT_SHORT_PRESS || event == BUTTON_EVENT_LONG_PRESS ||
        event == BUTTON_EVENT_DOUBLE_CLICK) {
        note_user_activity();
    }

    home_page_active = (g_page_manager.active_page == SCOOTERDEMO_PAGE_HOME);
    navigation_page_active = (g_page_manager.active_page == SCOOTERDEMO_PAGE_NAVIGATION);
    ai_session_active = scooterdemo_ai_is_session_active();

    /* Long-press OK → hand over to AI module.
     * AI module will self-heal stale active flags and ignore only truly active sessions. */
    if (keycode == KEYCODE_OK && event == BUTTON_EVENT_LONG_PRESS) {
        scooterdemo_ai_trigger_greeting();
        return;
    }

    if (ai_session_active && keycode == KEYCODE_DOWN &&
        event == BUTTON_EVENT_DOUBLE_CLICK) {
        scooterdemo_ai_force_exit();
        return;
    }

    if (navigation_page_active && keycode == KEYCODE_OK &&
        event == BUTTON_EVENT_DOUBLE_CLICK) {
        request_page_switch(SCOOTERDEMO_PAGE_HOME);
        return;
    }

    if (driver_demo_handle_button(keycode, event, home_page_active, lv_tick_get())) {
        return;
    }

    if (sidebar_group == NULL || event != BUTTON_EVENT_SHORT_PRESS) {
        return;
    }

    switch (keycode) {
    case KEYCODE_DOWN:
        lv_group_focus_next(sidebar_group);
        update_sidebar_focus_indicator();
        break;

    case KEYCODE_UP:
        lv_group_focus_prev(sidebar_group);
        update_sidebar_focus_indicator();
        break;

    case KEYCODE_OK:
        handle_sidebar_confirm();
        break;

    default:
        break;
    }
}

static void settings_page_button_event_cb(KEYCODE keycode, BUTTON_EVENT event, void *user_data)
{
    (void)user_data;

    if (event == BUTTON_EVENT_SHORT_PRESS || event == BUTTON_EVENT_LONG_PRESS ||
        event == BUTTON_EVENT_DOUBLE_CLICK) {
        note_user_activity();
    }

    switch (keycode) {
    case KEYCODE_DOWN:
        if (event == BUTTON_EVENT_SHORT_PRESS) {
            scooterdemo_settings_page_handle_nav_down();
        }
        break;

    case KEYCODE_UP:
        if (event == BUTTON_EVENT_SHORT_PRESS) {
            scooterdemo_settings_page_handle_nav_up();
        }
        break;

    case KEYCODE_OK:
        if (event == BUTTON_EVENT_SHORT_PRESS) {
            scooterdemo_settings_page_handle_enter();
        } else if (event == BUTTON_EVENT_DOUBLE_CLICK) {
            if (!scooterdemo_settings_page_handle_double_click()) {
                go_back_one_level();
            }
        }
        break;

    default:
        break;
    }
}

static void music_page_button_event_cb(KEYCODE keycode, BUTTON_EVENT event, void *user_data)
{
    (void)user_data;

    if (event == BUTTON_EVENT_SHORT_PRESS || event == BUTTON_EVENT_LONG_PRESS ||
        event == BUTTON_EVENT_DOUBLE_CLICK) {
        note_user_activity();
    }

    switch (keycode) {
    case KEYCODE_DOWN:
        if (event == BUTTON_EVENT_SHORT_PRESS) {
            scooterdemo_music_page_handle_nav_down();
        }
        break;

    case KEYCODE_UP:
        if (event == BUTTON_EVENT_SHORT_PRESS) {
            scooterdemo_music_page_handle_nav_up();
        }
        break;

    case KEYCODE_OK:
        if (event == BUTTON_EVENT_SHORT_PRESS) {
            scooterdemo_music_page_handle_enter();
        } else if (event == BUTTON_EVENT_DOUBLE_CLICK) {
            go_back_one_level();
        }
        break;

    default:
        break;
    }
}

static void update_page_button_handler(int page_index)
{
    scooterdemo_button_handler_id_t next_handler;
    uint32_t double_click_mask = 0U;

    if (g_active_button_handler == SCOOTERDEMO_BUTTON_HANDLER_SIDEBAR) {
        key_processor_unregister_event_handler(sidebar_page_button_event_cb, NULL);
    } else if (g_active_button_handler == SCOOTERDEMO_BUTTON_HANDLER_MUSIC) {
        key_processor_unregister_event_handler(music_page_button_event_cb, NULL);
    } else if (g_active_button_handler == SCOOTERDEMO_BUTTON_HANDLER_SETTINGS) {
        key_processor_unregister_event_handler(settings_page_button_event_cb, NULL);
    }

    if (page_index == SCOOTERDEMO_PAGE_SETTINGS) {
        next_handler = SCOOTERDEMO_BUTTON_HANDLER_SETTINGS;
        double_click_mask = BUTTON_PROCESSOR_KEY_MASK_OK;
        key_processor_register_event_handler(settings_page_button_event_cb, NULL);
    } else if (page_index == SCOOTERDEMO_PAGE_MUSIC) {
        next_handler = SCOOTERDEMO_BUTTON_HANDLER_MUSIC;
        double_click_mask = BUTTON_PROCESSOR_KEY_MASK_OK;
        key_processor_register_event_handler(music_page_button_event_cb, NULL);
    } else {
        next_handler = SCOOTERDEMO_BUTTON_HANDLER_SIDEBAR;
        if (page_index == SCOOTERDEMO_PAGE_HOME) {
            double_click_mask = BUTTON_PROCESSOR_KEY_MASK_UP |
                                BUTTON_PROCESSOR_KEY_MASK_DOWN;
        } else if (page_index == SCOOTERDEMO_PAGE_NAVIGATION) {
            double_click_mask = BUTTON_PROCESSOR_KEY_MASK_OK;
        }
        key_processor_register_event_handler(sidebar_page_button_event_cb, NULL);
    }

    key_processor_set_double_click_mask(double_click_mask);
    g_active_button_handler = next_handler;
}

// Create Right Sidebar Menu with Navigation
static void create_right_sidebar(lv_obj_t *parent)
{
    lv_obj_t *sidebar = lv_obj_create(parent);
    right_sidebar = sidebar;
    lv_obj_set_size(sidebar, W(60), H(220));
    lv_obj_align(sidebar, LV_ALIGN_LEFT_MID, W(12), H(2));
    lv_obj_set_style_bg_opa(sidebar, LV_OPA_0, 0);
    lv_obj_set_style_border_width(sidebar, 0, 0);
    lv_obj_set_style_pad_all(sidebar, 0, 0);
    lv_obj_set_scrollbar_mode(sidebar, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(sidebar, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(sidebar, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(sidebar, H(15), 0);

    sidebar_group = lv_group_create();
    lv_group_set_wrap(sidebar_group, true);
    sidebar_btn_count = 0;

    const char *icons[] = {LV_SYMBOL_DRIVE, LV_SYMBOL_GPS, LV_SYMBOL_AUDIO, LV_SYMBOL_SETTINGS};
    
    for(int i=0; i<4; i++) {
        lv_obj_t *img = lv_img_create(sidebar);
        lv_obj_set_size(img, W(40), H(40));

        lv_obj_add_flag(img, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(img, sidebar_btn_click_cb, LV_EVENT_CLICKED, NULL);

        if (i == 0) {
            lv_obj_add_state(img, LV_STATE_CHECKED);
        }

        if (!rivotek_theme_component_apply_sidebar_item(img, i, false)) {
            lv_obj_t *lbl = lv_label_create(img);
            lv_label_set_text(lbl, icons[i]);
            lv_obj_center(lbl);
        }
        
        lv_group_add_obj(sidebar_group, img);
        sidebar_btns[sidebar_btn_count++] = img;
    }

    update_sidebar_focus_indicator();
}

// Create Bottom Panel (Re-adding Gear Indicator SDPR)
static void create_bottom_panel(lv_obj_t *parent)
{
    // Create a container for gear indicator (SDPR)
    lv_obj_t *gear_cont = lv_obj_create(parent);
    
    // Set size to fit the four labels
    lv_obj_set_size(gear_cont, W(200), H(50));

    lv_obj_align(gear_cont, LV_ALIGN_CENTER, -6, H(155));
    
    lv_obj_set_style_bg_opa(gear_cont, LV_OPA_0, 0);
    lv_obj_set_style_border_width(gear_cont, 0, 0);
    lv_obj_set_scrollbar_mode(gear_cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(gear_cont, LV_OBJ_FLAG_SCROLLABLE);

    for(int i = 0; i < 2; i++) {
        gear_labels[i] = lv_label_create(gear_cont);
        lv_label_set_text(gear_labels[i], i == 0 ? "S" : "D");
        lv_obj_set_style_text_color(gear_labels[i], COLOR_GRAY, 0);
        lv_obj_set_style_text_opa(gear_labels[i], LV_OPA_30, 0);
        lv_obj_set_style_text_font(gear_labels[i], font_gear_side_24, 0);
    }

    lv_obj_align(gear_labels[0], LV_ALIGN_CENTER, W(-34), 0);
    lv_obj_align(gear_labels[1], LV_ALIGN_CENTER, W(46), 0);

    gear_labels[2] = NULL;
    gear_labels[3] = NULL;

    gear_current_label = lv_label_create(gear_cont);
    lv_label_set_text(gear_current_label, g_vdata.gear);
    lv_obj_set_style_text_font(gear_current_label, font_gear_current_56, 0);
    lv_obj_set_style_text_align(gear_current_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(gear_current_label, LV_ALIGN_CENTER, W(6), 0);

    refresh_gear_highlight(g_vdata.gear);

    // Add text to the left of the gear indicator
    lv_obj_t *gear_text = lv_obj_create(parent);
    lv_obj_remove_style_all(gear_text);
    lv_obj_set_size(gear_text, W(150), H(36));
    lv_obj_set_flex_flow(gear_text, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(gear_text,
                          LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_gap(gear_text, W(8), 0);
    lv_obj_clear_flag(gear_text, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *power_title_label = lv_label_create(gear_text);
    lv_label_set_text(power_title_label, "POWER");
    lv_obj_set_style_text_color(power_title_label, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(power_title_label, font_info_20, 0);

    lv_obj_t *power_value_label = lv_label_create(gear_text);
    lv_label_set_text(power_value_label, "100%");
    lv_obj_set_style_text_color(power_value_label, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(power_value_label, font_status_26, 0);

    lv_obj_align_to(gear_text, gear_cont, LV_ALIGN_OUT_LEFT_MID, -W(30), H(8));
    
    // Add text to the right of the gear indicator
    lv_obj_t *gear_text_right = lv_obj_create(parent);
    lv_obj_remove_style_all(gear_text_right);
    lv_obj_set_size(gear_text_right, W(170), H(36));
    lv_obj_set_flex_flow(gear_text_right, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(gear_text_right,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_gap(gear_text_right, W(8), 0);
    lv_obj_clear_flag(gear_text_right, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *odo_title_label = lv_label_create(gear_text_right);
    lv_label_set_text(odo_title_label, "ODO");
    lv_obj_set_style_text_color(odo_title_label, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(odo_title_label, font_info_20, 0);

    lv_obj_t *odo_value_label = lv_label_create(gear_text_right);
    lv_label_set_text(odo_value_label, "999.9km");
    lv_obj_set_style_text_color(odo_value_label, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(odo_value_label, font_status_26, 0);

    lv_obj_align_to(gear_text_right, gear_cont, LV_ALIGN_OUT_RIGHT_MID, W(30), H(8));

    // Create a container for the bottom image (610*30 pixels scaled)
    lv_obj_t *bottom_img_cont = lv_obj_create(parent);
    lv_obj_set_size(bottom_img_cont, W(358), H(14));
    lv_obj_align(bottom_img_cont, LV_ALIGN_BOTTOM_MID, 0, H(-15));

    lv_obj_set_style_bg_opa(bottom_img_cont, LV_OPA_0, 0);
    lv_obj_set_style_border_width(bottom_img_cont, 0, 0);
    lv_obj_set_scrollbar_mode(bottom_img_cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(bottom_img_cont, LV_OBJ_FLAG_SCROLLABLE);

    // Create image object for the 610*30 slice
    bottom_theme_img = lv_img_create(bottom_img_cont);
    update_theme_bottom_image(rivotek_theme_component_get_active_index());

    /* Let AI module swap this image for battery-warn scenario */
    scooterdemo_ai_set_bottom_img(bottom_theme_img);

    // Add left range label next to the bottom themed image
    lv_obj_t *km_label = lv_label_create(parent);
    lv_label_set_text(km_label, "121km");
    lv_obj_set_style_text_color(km_label, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(km_label, font_status_26, 0);
    lv_obj_align_to(km_label, bottom_img_cont, LV_ALIGN_OUT_LEFT_MID, -W(15), 0);
 
    // Add right percentage label next to the bottom themed image
    lv_obj_t *percent_label = lv_label_create(parent);
    lv_label_set_text(percent_label, "100%");
    lv_obj_set_style_text_color(percent_label, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(percent_label, font_status_26, 0);
    lv_obj_align_to(percent_label, bottom_img_cont, LV_ALIGN_OUT_RIGHT_MID, W(10), 0);

    /* Let AI module update this label for battery-warn scenario */
    scooterdemo_ai_set_percent_label(percent_label);
}


int main(int argc, char *argv[])
{
    lv_nuttx_dsc_t info;
    lv_nuttx_result_t result;
    scooterdemo_ui_metrics_t ui_metrics;
    const rivotek_platform_config_t *platform_config = rivotek_platform_config_get();

    if (lv_is_initialized()) {
        printf("LVGL already initialized\n");
        return -1;
    }

    (void)rivotek_platform_board_init();

    /* LVGL Init */
    lv_init();
    lv_nuttx_dsc_init(&info);

    #ifdef CONFIG_LV_USE_NUTTX_LCD
        info.fb_path = platform_config->fb_path;
        // 增加等待机制，避免因驱动加载较慢导致概率性黑屏
        while (access(info.fb_path, F_OK) < 0) {
            printf("Wait for %s...\n", info.fb_path);
            usleep(100000); // 延时 100ms
        }
    #endif 
    #ifdef CONFIG_LV_USE_NUTTX_TOUCHSCREEN
        info.input_path = NULL; // 禁用触摸屏
    #endif
    #ifdef CONFIG_LV_USE_NUTTX_LIBUV
        uv_loop_t ui_loop;
        lv_memzero(&ui_loop, sizeof(ui_loop));
    #endif

    /* NuttX Backend Init */
    lv_nuttx_init(&info, &result);
    rivotek_audio_component_init();
    rivotek_time_component_init();
    
    // Create Main Screen
    screen = lv_scr_act();
    // 强制设置主屏幕的背景透明度为0（完全透明），避免局部重绘时出现默认的黑色/白色背景填充
    lv_obj_set_style_bg_opa(screen, LV_OPA_0, 0);
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);   
    rivotek_weather_component_init();
    // Update display dimensions for scaling
    disp_w = lv_obj_get_width(screen);
    disp_h = lv_obj_get_height(screen);
    printf("UI Demo Initialized: %dx%d (Base: %dx%d)\n", disp_w, disp_h, BASE_W, BASE_H);
    scooterdemo_boot_demo_run(screen);
    // Init Fonts (after resolution is known)
    init_fonts();
    ui_metrics = get_ui_metrics();
    scooterdemo_page_manager_init(&g_page_manager);
    rivotek_theme_component_init(scooterdemo_settings_page_get_active_theme());
    rivotek_theme_component_set_changed_cb(on_theme_changed, NULL);
    scooterdemo_settings_page_set_theme_apply_cb(apply_theme_selection, NULL);
    srand((unsigned int)time(NULL));
    driver_demo_init();
    // 如果您使用的是文件系统加载PNG/JPG，请确保LVGL已启用对应的解码器和文件系统，并使用正确的路径（如带有盘符）：
    // Create UI Components
    create_right_sidebar(screen); // Create sidebar first to init group
    create_top_bar(screen);
    for (int i = 0; i < SCOOTERDEMO_PAGE_COUNT; i++) {
        main_containers[i] = scooterdemo_page_manager_create_container(screen,
                                                                       &ui_metrics);
        scooterdemo_page_manager_register(&g_page_manager,
                                          (scooterdemo_page_id_t)i,
                                          main_containers[i]);
    }
    create_home_background_layers(main_containers[SCOOTERDEMO_PAGE_HOME]);
    create_right_panel(main_containers[0]);
    create_center_meter(main_containers[0]);
    create_left_panel(main_containers[0]);
    create_bottom_panel(main_containers[0]);
    create_home_top_bar(main_containers[SCOOTERDEMO_PAGE_HOME]);
    scooterdemo_navigation_page_build(main_containers[SCOOTERDEMO_PAGE_NAVIGATION],
                                      &ui_metrics);
    update_navigation_map_for_city(rivotek_weather_component_get_city_text());
    scooterdemo_music_page_build(main_containers[SCOOTERDEMO_PAGE_MUSIC],
                                 &ui_metrics);
    scooterdemo_settings_page_build(main_containers[SCOOTERDEMO_PAGE_SETTINGS],
                                    &ui_metrics);
    switch_page_now(SCOOTERDEMO_PAGE_HOME);
    g_home_last_interaction_tick = lv_tick_get();
    /* Initialise AI assistant overlay (mqueue + avatar + nav map) */
    scooterdemo_ai_init(screen,
                        main_containers[SCOOTERDEMO_PAGE_HOME],
                        main_containers[SCOOTERDEMO_PAGE_NAVIGATION],
                        disp_w, disp_h,
                        ai_request_page_switch,
                        NULL);
#ifdef CONFIG_RIVOTEK_QRCODE_SERVICE
    rvt_qrcode_ui_metrics_t qr_metrics = {
        .disp_w = ui_metrics.disp_w,
        .disp_h = ui_metrics.disp_h,
        .font_16 = ui_metrics.font_16,
        .font_20 = ui_metrics.font_20,
        .font_28 = ui_metrics.font_28,
        .font_40 = ui_metrics.font_40,
    };

    rvt_qrcode_init(screen, &qr_metrics);
#ifdef CONFIG_RIVOTEK_PROVISIONING_SERVICE
    if (rvt_provisioning_qrcode_bridge_init() < 0) {
        printf("Failed to init QR provisioning bridge\n");
    }
#endif
#endif
    if (sidebar_btn_count > 0) {
        lv_group_focus_obj(sidebar_btns[0]);
        lv_obj_add_state(sidebar_btns[0], LV_STATE_CHECKED);
        update_sidebar_focus_indicator();
    }
    key_processor_set_thresholds(1000, 400);
    key_processor_init();
    if (rivotek_weather_component_start() < 0) {
        printf("Failed to start weather thread\n");
    }
    // Start Data Timer
    lv_timer_create(update_ui_timer_cb, 1000, NULL);
    lv_timer_create(sync_music_ui_timer_cb, SCOOTERDEMO_BT_UI_SYNC_INTERVAL_MS, NULL);
    // Main Loop
    while (1) {
        uint32_t sleep_ms = lv_timer_handler();
        if (sleep_ms < 1) {
            sleep_ms = 1;
        } else if (sleep_ms > 20) {
            sleep_ms = 20;
        }
        usleep(sleep_ms * 1000);
    }
    return 0;
}
