#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "scooterdemo_pages.h"
#include "theme_impl.h"

#define COLOR_TEXT lv_color_hex(0xFFFFFF)
#define COLOR_SUBTEXT lv_color_hex(0x6B8796)
#define COLOR_HINT lv_color_hex(0x9EAAB4)
#define COLOR_PAGE_BG lv_color_hex(0x071521)
#define COLOR_PAGE_BG_ALT lv_color_hex(0x020A12)
#define COLOR_DIVIDER lv_color_hex(0x5BA2C7)
#define COLOR_GLOW_MAIN lv_color_hex(0x0BB8FF)
#define COLOR_GLOW_SUB lv_color_hex(0x0F7FB0)
#define COLOR_PLAY_BUTTON lv_color_hex(0xF6A300)
#define COLOR_PLAY_BUTTON_ALT lv_color_hex(0xD58A00)

#define MUSIC_DEMO_TITLE "这次不会再打扰"
#define MUSIC_DEMO_ARTIST "洛天依"
#define MUSIC_TOTAL_SECONDS 257

#define MUSIC_ASSET_PAGE_BG "/resource/imgs/settings_bg2.png"
#define MUSIC_ASSET_ALBUM_COVER "/resource/imgs/music_bg.png"
#define MUSIC_ASSET_BTN_PLAY "/resource/imgs/music_btn_play.png"
#define MUSIC_ASSET_BTN_PREV "/resource/imgs/music_btn_prev.png"
#define MUSIC_ASSET_BTN_PAUSE "/resource/imgs/music_btn_pause.png"
#define MUSIC_ASSET_BTN_NEXT "/resource/imgs/music_btn_next.png"
#define MUSIC_CONTROL_COUNT 3
#define MUSIC_CONTROL_ZOOM_NORMAL 256
#define MUSIC_CONTROL_ZOOM_FOCUSED 300
#define MUSIC_CONTROL_ZOOM_PRESSED 360
#define MUSIC_PAGE_SHIFT_X 0
#define MUSIC_PAGE_SHIFT_Y 0
#define MUSIC_RIGHT_IMAGE_W 463
#define MUSIC_RIGHT_IMAGE_H 439
#define MUSIC_RIGHT_IMAGE_MARGIN_RIGHT 24
#define MUSIC_RIGHT_IMAGE_MARGIN_BOTTOM 20
#define MUSIC_PREV_BUTTON_X 116
#define MUSIC_PREV_BUTTON_Y 278
#define MUSIC_PLAY_BUTTON_X 180
#define MUSIC_PLAY_BUTTON_Y 272
#define MUSIC_NEXT_BUTTON_X 248
#define MUSIC_NEXT_BUTTON_Y 278

#define MUSIC_PLACEHOLDER_TITLE "等待蓝牙音频"
#define MUSIC_PLACEHOLDER_ARTIST "未连接设备"

LV_FONT_DECLARE(lv_font_montserrat_20);
LV_FONT_DECLARE(lv_font_montserrat_28);

typedef enum {
    MUSIC_CONTROL_PREV = 0,
    MUSIC_CONTROL_PLAY_PAUSE,
    MUSIC_CONTROL_NEXT,
} music_control_id_t;

static lv_obj_t *g_music_title_label;
static lv_obj_t *g_music_artist_label;
static lv_obj_t *g_music_play_button;
static lv_obj_t *g_music_play_img;
static lv_obj_t *g_music_play_pause_img;
static lv_obj_t *g_music_play_symbol_label;
static lv_obj_t *g_music_control_buttons[MUSIC_CONTROL_COUNT];
static lv_obj_t *g_music_album_rotating_img;
static music_control_id_t g_music_selected_control = MUSIC_CONTROL_PLAY_PAUSE;
static bool g_music_connected;
static bool g_music_playing;
static bool g_music_album_rotation_active;
static uint32_t g_music_play_long_press_tick;

static void music_set_control_zoom(void *obj, int32_t value);
static bool music_trigger_control(music_control_id_t control_id);

static const char *music_control_name(music_control_id_t control_id)
{
    switch (control_id) {
    case MUSIC_CONTROL_PREV:
        return "prev";
    case MUSIC_CONTROL_PLAY_PAUSE:
        return g_music_playing ? "pause" : "play";
    case MUSIC_CONTROL_NEXT:
        return "next";
    default:
        return "unknown";
    }
}

static void music_reset_runtime_state(void)
{
    g_music_title_label = NULL;
    g_music_artist_label = NULL;
    g_music_play_button = NULL;
    g_music_play_img = NULL;
    g_music_play_pause_img = NULL;
    g_music_play_symbol_label = NULL;
    g_music_album_rotating_img = NULL;
    g_music_selected_control = MUSIC_CONTROL_PLAY_PAUSE;
    g_music_connected = false;
    g_music_playing = false;
    g_music_album_rotation_active = false;
    g_music_play_long_press_tick = 0;

    for (uint8_t index = 0; index < MUSIC_CONTROL_COUNT; index++) {
        g_music_control_buttons[index] = NULL;
    }
}

static bool music_asset_exists(const char *path)
{
    return access(path, F_OK) == 0;
}

static const char *music_pick_asset_path(const char *primary, const char *fallback)
{
    if (primary != NULL && music_asset_exists(primary)) {
        return primary;
    }

    if (fallback != NULL && music_asset_exists(fallback)) {
        return fallback;
    }

    return NULL;
}

static void music_reset_obj(lv_obj_t *obj)
{
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static void music_set_label_style(lv_obj_t *label, const lv_font_t *font, lv_color_t color)
{
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_0, 0);
}

static void music_format_time(char *buffer, size_t buffer_size, uint32_t total_seconds)
{
    uint32_t minutes = total_seconds / 60;
    uint32_t seconds = total_seconds % 60;

    lv_snprintf(buffer, buffer_size, "%" LV_PRIu32 ":%02" LV_PRIu32, minutes, seconds);
}

static void music_set_label_text_if_changed(lv_obj_t *label, const char *text)
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



static void music_start_album_rotation(lv_obj_t *img)
{
    lv_anim_t anim;

    if (img == NULL) {
        return;
    }

    lv_anim_delete(img, NULL);
    lv_img_set_angle(img, 0);
    lv_img_set_pivot(img, lv_obj_get_width(img) / 2, lv_obj_get_height(img) / 2);

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

static void music_stop_album_rotation(lv_obj_t *img)
{
    if (img == NULL) {
        return;
    }

    lv_anim_delete(img, NULL);
    lv_img_set_angle(img, 0);
}

static void music_update_album_rotation(void)
{
    // 不进行任何旋转操作，因为我们使用的是静态速度背景图片
    // 而不是动态旋转的专辑封面
    if (g_music_album_rotating_img == NULL) {
        return;
    }

    // 清除可能存在的任何动画
    lv_anim_delete(g_music_album_rotating_img, NULL);
    // 确保图片角度为0度
    lv_img_set_angle(g_music_album_rotating_img, 0);
}

static void music_update_control_focus(void)
{
    for (uint8_t index = 0; index < MUSIC_CONTROL_COUNT; index++) {
        lv_obj_t *button = g_music_control_buttons[index];
        int32_t target_zoom;

        if (button == NULL) {
            continue;
        }

        target_zoom = (index == g_music_selected_control) ?
                      MUSIC_CONTROL_ZOOM_FOCUSED : MUSIC_CONTROL_ZOOM_NORMAL;
        lv_anim_delete(button, music_set_control_zoom);
        lv_obj_set_style_transform_zoom(button, target_zoom, 0);
    }
}

static void music_set_selected_control(music_control_id_t control_id)
{
    if (control_id >= MUSIC_CONTROL_COUNT) {
        return;
    }

    g_music_selected_control = control_id;
    music_update_control_focus();
}

static void music_set_control_zoom(void *obj, int32_t value)
{
    lv_obj_set_style_transform_zoom((lv_obj_t *)obj, value, 0);
}

static void music_play_control_press_feedback(lv_obj_t *button, music_control_id_t control_id)
{
    lv_anim_t anim;
    int32_t base_zoom;

    if (button == NULL) {
        return;
    }

    base_zoom = (control_id == g_music_selected_control) ?
                MUSIC_CONTROL_ZOOM_FOCUSED : MUSIC_CONTROL_ZOOM_NORMAL;
    lv_anim_delete(button, music_set_control_zoom);
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, button);
    lv_anim_set_exec_cb(&anim, music_set_control_zoom);
    lv_anim_set_values(&anim, base_zoom, MUSIC_CONTROL_ZOOM_PRESSED);
    lv_anim_set_duration(&anim, 90);
    lv_anim_set_playback_duration(&anim, 120);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_start(&anim);
}

static void music_update_play_button_visual(void)
{
    bool show_pause_asset;
    bool show_play_asset;

    if (g_music_play_button == NULL || g_music_play_symbol_label == NULL) {
        return;
    }

    show_pause_asset = g_music_playing && (g_music_play_pause_img != NULL);
    show_play_asset = !g_music_playing && (g_music_play_img != NULL);

    if (g_music_play_pause_img != NULL) {
        if (show_pause_asset) {
            lv_obj_clear_flag(g_music_play_pause_img, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(g_music_play_pause_img);
        } else {
            lv_obj_add_flag(g_music_play_pause_img, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (g_music_play_img != NULL) {
        if (show_play_asset) {
            lv_obj_clear_flag(g_music_play_img, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(g_music_play_img);
        } else {
            lv_obj_add_flag(g_music_play_img, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (show_pause_asset || show_play_asset) {
        lv_obj_add_flag(g_music_play_symbol_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(g_music_play_symbol_label, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(g_music_play_symbol_label, g_music_playing ? "||" : ">");
    }
}

static void music_set_connected(bool connected)
{
    g_music_connected = connected;
    music_update_album_rotation();
}

static void music_set_playing(bool playing)
{
    g_music_playing = playing;
    music_update_play_button_visual();
    music_update_album_rotation();
}

static void music_control_event_cb(lv_event_t *e)
{
    music_control_id_t control_id;
    const char *control_name;
    int ret = -1;
    lv_obj_t *target;

    target = lv_event_get_current_target(e);
    control_id = (music_control_id_t)(uintptr_t)lv_event_get_user_data(e);
    control_name = music_control_name(control_id);

    if (control_id == MUSIC_CONTROL_PLAY_PAUSE &&
        lv_tick_elaps(g_music_play_long_press_tick) < 250U) {
        printf("music_page: ignore %s click due to recent long press\n", control_name);
        return;
    }

    printf("music_page: control=%s clicked\n", control_name);
    music_set_selected_control(control_id);
    music_play_control_press_feedback(target, control_id);

    switch (control_id) {
    case MUSIC_CONTROL_PREV:
        ret = rivotek_audio_component_play_prev();
        if (ret == 0) {
            (void)rivotek_audio_component_request_metadata();
        }
        break;
    case MUSIC_CONTROL_PLAY_PAUSE:
        ret = rivotek_audio_component_toggle_play();
        if (ret == 0) {
            (void)rivotek_audio_component_request_metadata();
        }
        break;
    case MUSIC_CONTROL_NEXT:
        ret = rivotek_audio_component_play_next();
        if (ret == 0) {
            (void)rivotek_audio_component_request_metadata();
        }
        break;
    default:
        break;
    }

    printf("music_page: control=%s ret=%d\n", control_name, ret);
}

static void music_play_button_long_press_event_cb(lv_event_t *e)
{
    lv_obj_t *target;
    int ret;

    target = lv_event_get_current_target(e);
    printf("music_page: control=stop long_press\n");
    music_set_selected_control(MUSIC_CONTROL_PLAY_PAUSE);
    music_play_control_press_feedback(target, MUSIC_CONTROL_PLAY_PAUSE);

    ret = rivotek_audio_component_stop();
    if (ret == 0) {
        music_set_playing(false);
        (void)rivotek_audio_component_request_metadata();
    }

    printf("music_page: control=stop ret=%d\n", ret);

    g_music_play_long_press_tick = lv_tick_get();
}

static lv_obj_t *music_create_icon_button(lv_obj_t *parent,
                                          const scooterdemo_ui_metrics_t *ui,
                                          const char *asset_path,
                                          int32_t width,
                                          int32_t height,
                                          music_control_id_t control_id)
{
    lv_obj_t *button = lv_obj_create(parent);

    music_reset_obj(button);
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_transform_pivot_x(button, width / 2, 0);
    lv_obj_set_style_transform_pivot_y(button, height / 2, 0);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(button, music_control_event_cb, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)control_id);
    lv_obj_set_ext_click_area(button, SCOOTERDEMO_W(ui, 12));
    g_music_control_buttons[control_id] = button;

    if (music_asset_exists(asset_path)) {
        lv_obj_t *img = lv_img_create(button);
        lv_img_set_src(img, asset_path);
        lv_obj_center(img);
    } else {
        lv_obj_t *fallback = lv_label_create(button);

        music_set_label_style(fallback, &lv_font_montserrat_28, COLOR_TEXT);
        lv_label_set_text(fallback,
                          control_id == MUSIC_CONTROL_PREV ? LV_SYMBOL_PREV : LV_SYMBOL_NEXT);
        lv_obj_center(fallback);
    }

    return button;
}

static void music_create_backdrop(lv_obj_t *parent, const scooterdemo_ui_metrics_t *ui)
{
    const char *bg_path = music_pick_asset_path(MUSIC_ASSET_PAGE_BG, NULL);
    lv_obj_t *backdrop;

    if (bg_path != NULL) {
        backdrop = lv_img_create(parent);

        lv_img_set_src(backdrop, bg_path);
        lv_obj_align(backdrop, LV_ALIGN_CENTER, 0, 0);
        lv_obj_clear_flag(backdrop, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_move_background(backdrop);
        return;
    }

    backdrop = lv_obj_create(parent);
    lv_obj_t *divider;
    lv_obj_t *glow_main;
    lv_obj_t *glow_sub;

    music_reset_obj(backdrop);
    lv_obj_set_size(backdrop, ui->disp_w, ui->disp_h);
    lv_obj_align(backdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(backdrop, COLOR_PAGE_BG, 0);
    lv_obj_set_style_bg_grad_color(backdrop, COLOR_PAGE_BG_ALT, 0);
    lv_obj_set_style_bg_grad_dir(backdrop, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_bg_opa(backdrop, LV_OPA_COVER, 0);

    glow_main = lv_obj_create(backdrop);
    music_reset_obj(glow_main);
    lv_obj_set_size(glow_main, SCOOTERDEMO_W(ui, 320), SCOOTERDEMO_H(ui, 320));
    lv_obj_align(glow_main, LV_ALIGN_LEFT_MID, SCOOTERDEMO_W(ui, 24), SCOOTERDEMO_H(ui, -6));
    lv_obj_set_style_radius(glow_main, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(glow_main, COLOR_GLOW_MAIN, 0);
    lv_obj_set_style_bg_opa(glow_main, LV_OPA_20, 0);

    glow_sub = lv_obj_create(backdrop);
    music_reset_obj(glow_sub);
    lv_obj_set_size(glow_sub, SCOOTERDEMO_W(ui, 180), SCOOTERDEMO_H(ui, 180));
    lv_obj_align(glow_sub, LV_ALIGN_LEFT_MID, SCOOTERDEMO_W(ui, 166), SCOOTERDEMO_H(ui, 18));
    lv_obj_set_style_radius(glow_sub, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(glow_sub, COLOR_GLOW_SUB, 0);
    lv_obj_set_style_bg_opa(glow_sub, LV_OPA_20, 0);

    divider = lv_obj_create(backdrop);
    music_reset_obj(divider);
    lv_obj_set_size(divider, ui->disp_w, SCOOTERDEMO_H(ui, 2));
    lv_obj_align(divider, LV_ALIGN_TOP_MID, 0, SCOOTERDEMO_H(ui, 49));
    lv_obj_set_style_bg_color(divider, COLOR_DIVIDER, 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_50, 0);
}

static void music_create_song_info(lv_obj_t *parent, const scooterdemo_ui_metrics_t *ui)
{
    g_music_title_label = lv_label_create(parent);
    lv_obj_set_width(g_music_title_label, SCOOTERDEMO_W(ui, 320));
    lv_label_set_long_mode(g_music_title_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(g_music_title_label, MUSIC_PLACEHOLDER_TITLE);
    music_set_label_style(g_music_title_label, ui->font_40, COLOR_TEXT);
    lv_obj_set_style_text_align(g_music_title_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(g_music_title_label, LV_ALIGN_TOP_LEFT,
                 SCOOTERDEMO_W(ui, 92), SCOOTERDEMO_H(ui, 118));

    g_music_artist_label = lv_label_create(parent);
    lv_obj_set_width(g_music_artist_label, SCOOTERDEMO_W(ui, 320));
    lv_label_set_text(g_music_artist_label, MUSIC_PLACEHOLDER_ARTIST);
    music_set_label_style(g_music_artist_label, ui->font_28, COLOR_SUBTEXT);
    lv_obj_set_style_text_align(g_music_artist_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(g_music_artist_label, LV_ALIGN_TOP_LEFT,
                 SCOOTERDEMO_W(ui, 55), SCOOTERDEMO_H(ui, 192));
}

static void music_create_controls(lv_obj_t *parent, const scooterdemo_ui_metrics_t *ui)
{
    bool has_play_asset;
    bool has_pause_asset;
    bool has_full_button_assets;
    lv_obj_t *prev_button;
    lv_obj_t *next_button;

    has_play_asset = music_asset_exists(MUSIC_ASSET_BTN_PLAY);
    has_pause_asset = music_asset_exists(MUSIC_ASSET_BTN_PAUSE);
    has_full_button_assets = has_play_asset && has_pause_asset;

    prev_button = music_create_icon_button(parent, ui, MUSIC_ASSET_BTN_PREV,
                                           SCOOTERDEMO_W(ui, 56), SCOOTERDEMO_H(ui, 56),
                                           MUSIC_CONTROL_PREV);
    lv_obj_align(prev_button, LV_ALIGN_TOP_LEFT,
                 SCOOTERDEMO_W(ui, MUSIC_PREV_BUTTON_X),
                 SCOOTERDEMO_H(ui, MUSIC_PREV_BUTTON_Y));

    g_music_play_button = lv_obj_create(parent);
    music_reset_obj(g_music_play_button);
    lv_obj_set_size(g_music_play_button, SCOOTERDEMO_W(ui, 74), SCOOTERDEMO_H(ui, 74));
    lv_obj_align(g_music_play_button, LV_ALIGN_TOP_LEFT,
                 SCOOTERDEMO_W(ui, MUSIC_PLAY_BUTTON_X),
                 SCOOTERDEMO_H(ui, MUSIC_PLAY_BUTTON_Y));
    lv_obj_set_style_transform_pivot_x(g_music_play_button, SCOOTERDEMO_W(ui, 37), 0);
    lv_obj_set_style_transform_pivot_y(g_music_play_button, SCOOTERDEMO_H(ui, 37), 0);
    if (!has_full_button_assets) {
        lv_obj_set_style_radius(g_music_play_button, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(g_music_play_button, COLOR_PLAY_BUTTON, 0);
        lv_obj_set_style_bg_grad_color(g_music_play_button, COLOR_PLAY_BUTTON_ALT, 0);
        lv_obj_set_style_bg_grad_dir(g_music_play_button, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_bg_opa(g_music_play_button, LV_OPA_COVER, 0);
        lv_obj_set_style_shadow_color(g_music_play_button, COLOR_PLAY_BUTTON, 0);
        lv_obj_set_style_shadow_width(g_music_play_button, SCOOTERDEMO_H(ui, 18), 0);
        lv_obj_set_style_shadow_opa(g_music_play_button, LV_OPA_30, 0);
    }
    lv_obj_add_flag(g_music_play_button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_music_play_button, music_control_event_cb, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)MUSIC_CONTROL_PLAY_PAUSE);
    lv_obj_add_event_cb(g_music_play_button, music_play_button_long_press_event_cb,
                        LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_set_ext_click_area(g_music_play_button, SCOOTERDEMO_W(ui, 12));
    g_music_control_buttons[MUSIC_CONTROL_PLAY_PAUSE] = g_music_play_button;

    if (has_pause_asset) {
        g_music_play_pause_img = lv_img_create(g_music_play_button);
        lv_img_set_src(g_music_play_pause_img, MUSIC_ASSET_BTN_PAUSE);
        lv_obj_center(g_music_play_pause_img);
        lv_obj_add_flag(g_music_play_pause_img, LV_OBJ_FLAG_HIDDEN);
    } else {
        g_music_play_pause_img = NULL;
    }

    if (has_play_asset) {
        g_music_play_img = lv_img_create(g_music_play_button);
        lv_img_set_src(g_music_play_img, MUSIC_ASSET_BTN_PLAY);
        lv_obj_center(g_music_play_img);
        lv_obj_add_flag(g_music_play_img, LV_OBJ_FLAG_HIDDEN);
    } else {
        g_music_play_img = NULL;
    }

    g_music_play_symbol_label = lv_label_create(g_music_play_button);
    music_set_label_style(g_music_play_symbol_label, ui->font_40, COLOR_TEXT);
    lv_obj_center(g_music_play_symbol_label);

    next_button = music_create_icon_button(parent, ui, MUSIC_ASSET_BTN_NEXT,
                                           SCOOTERDEMO_W(ui, 56), SCOOTERDEMO_H(ui, 56),
                                           MUSIC_CONTROL_NEXT);
    lv_obj_align(next_button, LV_ALIGN_TOP_LEFT,
                 SCOOTERDEMO_W(ui, MUSIC_NEXT_BUTTON_X),
                 SCOOTERDEMO_H(ui, MUSIC_NEXT_BUTTON_Y));

    music_set_selected_control(MUSIC_CONTROL_PLAY_PAUSE);
    music_update_play_button_visual();
}

static void music_create_album_cover(lv_obj_t *parent, const scooterdemo_ui_metrics_t *ui)
{
    lv_obj_t *slot = lv_obj_create(parent);
    lv_obj_t *speed_bg_img;
    lv_obj_t *speed_label;
    lv_obj_t *speed_unit_label;

    music_reset_obj(slot);
    lv_obj_set_size(slot,
                    SCOOTERDEMO_W(ui, MUSIC_RIGHT_IMAGE_W),
                    SCOOTERDEMO_H(ui, MUSIC_RIGHT_IMAGE_H));
    lv_obj_align(slot, LV_ALIGN_BOTTOM_RIGHT,
                 -SCOOTERDEMO_W(ui, MUSIC_RIGHT_IMAGE_MARGIN_RIGHT),
                 -SCOOTERDEMO_H(ui, MUSIC_RIGHT_IMAGE_MARGIN_BOTTOM));

    // 创建速度背景图片对象
    speed_bg_img = lv_img_create(slot);
    g_music_album_rotating_img = speed_bg_img;
    
    // 绑定主题相关的音乐页右侧图片
    rivotek_theme_component_bind_music_right_image(speed_bg_img);
    lv_obj_center(speed_bg_img);

    speed_label = lv_label_create(slot);
    lv_label_set_text(speed_label, "0");
    music_set_label_style(speed_label, ui->font_speed_134, COLOR_TEXT);
    lv_obj_set_style_text_align(speed_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(speed_label, LV_ALIGN_CENTER, 10, 3);

    speed_unit_label = lv_label_create(slot);
    lv_label_set_text(speed_unit_label, "Km/h");
    music_set_label_style(speed_unit_label, ui->font_unit_24, COLOR_TEXT);
    lv_obj_set_style_text_align(speed_unit_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align_to(speed_unit_label, speed_label, LV_ALIGN_OUT_BOTTOM_MID,
                    0, SCOOTERDEMO_H(ui, 8));

    lv_obj_move_foreground(slot);
}

void scooterdemo_music_page_build(lv_obj_t *parent,
                                  const scooterdemo_ui_metrics_t *ui)
{
    lv_obj_t *content;

    if (parent == NULL || ui == NULL) {
        return;
    }

    lv_obj_clean(parent);
    music_reset_runtime_state();
    lv_obj_set_style_bg_color(parent, COLOR_PAGE_BG, 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    content = lv_obj_create(parent);
    music_reset_obj(content);
    lv_obj_set_size(content,
                    ui->disp_w + SCOOTERDEMO_W(ui, MUSIC_PAGE_SHIFT_X),
                    ui->disp_h + SCOOTERDEMO_H(ui, MUSIC_PAGE_SHIFT_Y));
    lv_obj_align(content, LV_ALIGN_CENTER,
                 -SCOOTERDEMO_W(ui, MUSIC_PAGE_SHIFT_X),
                 -SCOOTERDEMO_H(ui, MUSIC_PAGE_SHIFT_Y));

    music_create_backdrop(content, ui);
    music_create_song_info(content, ui);
    music_create_controls(content, ui);
    music_create_album_cover(content, ui);
}

void scooterdemo_music_page_on_show(void)
{
    music_set_selected_control(MUSIC_CONTROL_PLAY_PAUSE);
}

bool scooterdemo_music_page_handle_nav_up(void)
{
    return music_trigger_control(MUSIC_CONTROL_PREV);
}

bool scooterdemo_music_page_handle_nav_down(void)
{
    return music_trigger_control(MUSIC_CONTROL_NEXT);
}

bool scooterdemo_music_page_handle_enter(void)
{
    return music_trigger_control(MUSIC_CONTROL_PLAY_PAUSE);
}

static bool music_trigger_control(music_control_id_t control_id)
{
    lv_obj_t *button;

    if (control_id >= MUSIC_CONTROL_COUNT) {
        return false;
    }

    music_set_selected_control(control_id);
    button = g_music_control_buttons[control_id];

    if (button == NULL) {
        return false;
    }

    lv_obj_send_event(button, LV_EVENT_CLICKED, NULL);
    return true;
}

void scooterdemo_music_page_release(lv_obj_t *parent)
{
    for (uint8_t index = 0; index < MUSIC_CONTROL_COUNT; index++) {
        if (g_music_control_buttons[index] != NULL) {
            lv_anim_delete(g_music_control_buttons[index], music_set_control_zoom);
        }
    }

    // 停止速度背景图片的任何动画
    if (g_music_album_rotating_img != NULL) {
        lv_anim_delete(g_music_album_rotating_img, NULL);
    }

    if (parent != NULL) {
        lv_obj_clean(parent);
    }

    music_reset_runtime_state();
}

void scooterdemo_music_page_sync(const char *title,
                                 const char *artist,
                                 bool connected,
                                 bool playing,
                                 uint32_t total_duration_ms)
{
    const char *display_title;
    const char *display_artist;
    bool track_changed = false;
    const char *current_title;
    const char *current_artist;

    display_title = (title != NULL && title[0] != '\0') ? title : MUSIC_PLACEHOLDER_TITLE;
    display_artist = (artist != NULL && artist[0] != '\0') ? artist :
                     (connected ? "蓝牙已连接" : MUSIC_PLACEHOLDER_ARTIST);

    if (g_music_title_label != NULL) {
        current_title = lv_label_get_text(g_music_title_label);
        if (current_title != NULL && strcmp(current_title, display_title) != 0) {
            track_changed = true;
        }
    }

    if (g_music_artist_label != NULL) {
        current_artist = lv_label_get_text(g_music_artist_label);
        if (current_artist != NULL && strcmp(current_artist, display_artist) != 0) {
            track_changed = true;
        }
    }

    if (track_changed || !connected) {
    }

    music_set_label_text_if_changed(g_music_title_label, display_title);
    music_set_label_text_if_changed(g_music_artist_label, display_artist);

    music_set_connected(connected);
    music_set_playing(playing);
}
