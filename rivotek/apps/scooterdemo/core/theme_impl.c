#include <unistd.h>
#include <stdlib.h>

#include <lvgl/lvgl.h>
#include <lvgl/src/misc/lv_async.h>
#include <lvgl/src/misc/cache/instance/lv_image_cache.h>

#include "theme_impl.h"

#define SCOOTERDEMO_THEME_SWITCH_DELAY_MS 50
#define SCOOTERDEMO_SIDEBAR_ITEM_COUNT 4
#define SCOOTERDEMO_SPEED_BG_BINDING_COUNT 4
#define RIVOTEK_THEME_COMPONENT_COUNT 3
#define SCOOTERDEMO_BG_RETRY_INTERVAL_MS 500U
#define SCOOTERDEMO_BG_RETRY_ATTEMPTS 3U
#define SCOOTERDEMO_HOME_BG_IMAGE_PATH "/resource/imgs/yd_bg_orange.png"
#define SCOOTERDEMO_NAV_FIXED_IMAGE_PATH "/resource/imgs/nav_map_beijing.png"

typedef struct {
    const char *home_bg_path;
    const char *nav_bg_path;
    const char *sidebar_selected_paths[SCOOTERDEMO_SIDEBAR_ITEM_COUNT];
    const char *sidebar_unselected_paths[SCOOTERDEMO_SIDEBAR_ITEM_COUNT];
    const char *speed_bg_path;
    const char *music_right_img_path;
    const char *bottom_layer_path;
    uint32_t sidebar_selected_recolor_hex;
    lv_opa_t sidebar_selected_recolor_opa;
    uint32_t sidebar_unselected_recolor_hex;
    lv_opa_t sidebar_unselected_recolor_opa;
} rivotek_theme_profile_t;

typedef struct {
    int theme_index;
} rivotek_theme_release_request_t;

typedef struct {
    lv_obj_t *img;
    lv_obj_t *driving_img;
} rivotek_speed_bg_binding_t;

static const rivotek_theme_profile_t g_theme_profiles[RIVOTEK_THEME_COMPONENT_COUNT] = {
    {
        .home_bg_path = "/resource/imgs/yd_bg_green.png",
        .nav_bg_path = "/resource/imgs/yd_bg_green.png",
        .sidebar_selected_paths = {
            "/resource/imgs/home_g.png",
            "/resource/imgs/map_g.png",
            "/resource/imgs/music_g.png",
            "/resource/imgs/setting_g.png"
        },
        .sidebar_unselected_paths = {
            "/resource/imgs/home1.png",
            "/resource/imgs/map1.png",
            "/resource/imgs/music1.png",
            "/resource/imgs/setting1.png"
        },
        .speed_bg_path = "/resource/imgs/speed_bg_green.png",
        .music_right_img_path = "/resource/imgs/music_speed_bg_green.png",
        .bottom_layer_path = "/resource/imgs/bottom_layer_g.png",
        .sidebar_selected_recolor_hex = 0x000000,
        .sidebar_selected_recolor_opa = LV_OPA_0,
        .sidebar_unselected_recolor_hex = 0x000000,
        .sidebar_unselected_recolor_opa = LV_OPA_0,
    },
    {
        .home_bg_path = "/resource/imgs/yd_bg_blue.png",
        .nav_bg_path = "/resource/imgs/yd_bg_blue.png",
        .sidebar_selected_paths = {
            "/resource/imgs/home_b.png",
            "/resource/imgs/map_b.png",
            "/resource/imgs/music_b.png",
            "/resource/imgs/setting_b.png"
        },
        .sidebar_unselected_paths = {
            "/resource/imgs/home1.png",
            "/resource/imgs/map1.png",
            "/resource/imgs/music1.png",
            "/resource/imgs/setting1.png"
        },
        .speed_bg_path = "/resource/imgs/speed_bg_blue.png",
        .music_right_img_path = "/resource/imgs/music_speed_bg_blue.png",
        .bottom_layer_path = "/resource/imgs/bottom_layer_b.png",
        .sidebar_selected_recolor_hex = 0x000000,
        .sidebar_selected_recolor_opa = LV_OPA_0,
        .sidebar_unselected_recolor_hex = 0x000000,
        .sidebar_unselected_recolor_opa = LV_OPA_0,
    },
    {
        .home_bg_path = "/resource/imgs/yd_bg_orange.png",
        .nav_bg_path = "/resource/imgs/yd_bg_orange.png",
        .sidebar_selected_paths = {
            "/resource/imgs/home_o.png",
            "/resource/imgs/map_o.png",
            "/resource/imgs/music_o.png",
            "/resource/imgs/setting_o.png"
        },
        .sidebar_unselected_paths = {
            "/resource/imgs/home1.png",
            "/resource/imgs/map1.png",
            "/resource/imgs/music1.png",
            "/resource/imgs/setting1.png"
        },
        .speed_bg_path = "/resource/imgs/speed_bg_orange.png",
        .music_right_img_path = "/resource/imgs/music_speed_bg_orange.png",
        .bottom_layer_path = "/resource/imgs/bottom_layer_o.png",
        .sidebar_selected_recolor_hex = 0x000000,
        .sidebar_selected_recolor_opa = LV_OPA_0,
        .sidebar_unselected_recolor_hex = 0x000000,
        .sidebar_unselected_recolor_opa = LV_OPA_0,
    }
};

static int g_active_theme_index;
static int g_pending_theme_index = -1;
static lv_timer_t *g_theme_switch_timer;
static lv_obj_t *g_home_bg_img;
static lv_timer_t *g_home_bg_retry_timer;
static uint32_t g_home_bg_retry_attempts_left;
static lv_obj_t *g_navigation_bg_img;
static lv_obj_t *g_navigation_bg_slot;
static lv_timer_t *g_navigation_bg_retry_timer;
static uint32_t g_navigation_bg_retry_attempts_left;
static rivotek_speed_bg_binding_t g_speed_bg_bindings[SCOOTERDEMO_SPEED_BG_BINDING_COUNT];
static lv_obj_t *g_music_right_img;
static lv_obj_t *g_bottom_layer_img;
static rivotek_theme_changed_cb_t g_theme_changed_cb;
static void *g_theme_changed_cb_user_data;

static void theme_switch_timer_cb(lv_timer_t *timer);
static void home_background_retry_timer_cb(lv_timer_t *timer);
static void navigation_background_retry_timer_cb(lv_timer_t *timer);
static void refresh_home_background(void);
static void refresh_navigation_background(void);
static void refresh_speed_background(void);
static void refresh_music_right_image(void);
static void refresh_bottom_layer(void);
static void register_speed_background_binding(lv_obj_t *img, lv_obj_t *driving_img);

static int normalize_theme_index(int theme_index)
{
    if (theme_index < 0 || theme_index >= RIVOTEK_THEME_COMPONENT_COUNT) {
        return 0;
    }

    return theme_index;
}

static const rivotek_theme_profile_t *get_theme_profile(int theme_index)
{
    return &g_theme_profiles[normalize_theme_index(theme_index)];
}

static void drop_image_cache_path(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return;
    }

    lv_image_cache_drop(path);
}

static const char *resolve_home_background_path(void)
{
    const rivotek_theme_profile_t *theme = get_theme_profile(g_active_theme_index);

    if (access(theme->home_bg_path, F_OK) == 0) {
        return theme->home_bg_path;
    }

    if (access(SCOOTERDEMO_HOME_BG_IMAGE_PATH, F_OK) == 0) {
        return SCOOTERDEMO_HOME_BG_IMAGE_PATH;
    }

    return NULL;
}

static const char *resolve_navigation_background_path(void)
{
    const rivotek_theme_profile_t *theme = get_theme_profile(g_active_theme_index);

    if (access(SCOOTERDEMO_NAV_FIXED_IMAGE_PATH, F_OK) == 0) {
        return SCOOTERDEMO_NAV_FIXED_IMAGE_PATH;
    }

    if (access(theme->nav_bg_path, F_OK) == 0) {
        return theme->nav_bg_path;
    }

    return NULL;
}

static const char *resolve_speed_background_path(void)
{
    const rivotek_theme_profile_t *theme = get_theme_profile(g_active_theme_index);

    if (theme->speed_bg_path != NULL && access(theme->speed_bg_path, F_OK) == 0) {
        return theme->speed_bg_path;
    }

    if (access("/resource/imgs/speed_bg_orange.png", F_OK) == 0) {
        return "/resource/imgs/speed_bg_orange.png";
    }

    return NULL;
}

static const char *resolve_music_right_image_path(void)
{
    const rivotek_theme_profile_t *theme = get_theme_profile(g_active_theme_index);

    if (theme->music_right_img_path != NULL &&
        access(theme->music_right_img_path, F_OK) == 0) {
        return theme->music_right_img_path;
    }

    return resolve_speed_background_path();
}

static const char *resolve_bottom_layer_path(void)
{
    const rivotek_theme_profile_t *theme = get_theme_profile(g_active_theme_index);

    if (theme->bottom_layer_path != NULL && access(theme->bottom_layer_path, F_OK) == 0) {
        return theme->bottom_layer_path;
    }

    if (access("/resource/imgs/bottom_layer.png", F_OK) == 0) {
        return "/resource/imgs/bottom_layer.png";
    }

    return NULL;
}

static bool try_apply_home_background(bool drop_cache)
{
    const char *path;

    if (g_home_bg_img == NULL) {
        return false;
    }

    path = resolve_home_background_path();
    if (path == NULL) {
        lv_obj_add_flag(g_home_bg_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_invalidate(g_home_bg_img);
        return false;
    }

    if (drop_cache) {
        drop_image_cache_path(path);
    }

    lv_img_set_src(g_home_bg_img, path);
    lv_obj_clear_flag(g_home_bg_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(g_home_bg_img);
    return true;
}

static void stop_home_background_retry(void)
{
    if (g_home_bg_retry_timer != NULL) {
        lv_timer_del(g_home_bg_retry_timer);
        g_home_bg_retry_timer = NULL;
    }

    g_home_bg_retry_attempts_left = 0;
}

static void schedule_home_background_retry(uint32_t attempts)
{
    stop_home_background_retry();

    if (attempts == 0U) {
        return;
    }

    g_home_bg_retry_attempts_left = attempts;
    g_home_bg_retry_timer = lv_timer_create(home_background_retry_timer_cb,
                                            SCOOTERDEMO_BG_RETRY_INTERVAL_MS,
                                            NULL);
}

static bool try_apply_navigation_background(bool drop_cache)
{
    const char *path;

    if (g_navigation_bg_img == NULL || g_navigation_bg_slot == NULL) {
        return false;
    }

    path = resolve_navigation_background_path();
    if (path == NULL) {
        lv_obj_add_flag(g_navigation_bg_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_invalidate(g_navigation_bg_img);
        return false;
    }

    if (drop_cache) {
        drop_image_cache_path(path);
    }

    lv_img_set_src(g_navigation_bg_img, path);
    lv_obj_set_size(g_navigation_bg_img,
                    lv_obj_get_width(g_navigation_bg_slot),
                    lv_obj_get_height(g_navigation_bg_slot));
    lv_obj_clear_flag(g_navigation_bg_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_navigation_bg_img);
    lv_obj_invalidate(g_navigation_bg_img);
    return true;
}

static bool try_apply_speed_background(bool drop_cache)
{
    bool has_binding = false;
    const char *path;
    int index;

    path = resolve_speed_background_path();
    if (path == NULL) {
        for (index = 0; index < SCOOTERDEMO_SPEED_BG_BINDING_COUNT; index++) {
            if (g_speed_bg_bindings[index].img != NULL) {
                lv_obj_add_flag(g_speed_bg_bindings[index].img, LV_OBJ_FLAG_HIDDEN);
                lv_obj_invalidate(g_speed_bg_bindings[index].img);
            }
            if (g_speed_bg_bindings[index].driving_img != NULL) {
                lv_obj_add_flag(g_speed_bg_bindings[index].driving_img, LV_OBJ_FLAG_HIDDEN);
                lv_obj_invalidate(g_speed_bg_bindings[index].driving_img);
            }
        }
        return false;
    }

    if (drop_cache) {
        drop_image_cache_path(path);
    }

    for (index = 0; index < SCOOTERDEMO_SPEED_BG_BINDING_COUNT; index++) {
        if (g_speed_bg_bindings[index].img != NULL) {
            has_binding = true;
            lv_img_set_src(g_speed_bg_bindings[index].img, path);
            lv_obj_clear_flag(g_speed_bg_bindings[index].img, LV_OBJ_FLAG_HIDDEN);
            lv_obj_invalidate(g_speed_bg_bindings[index].img);
        }

        if (g_speed_bg_bindings[index].driving_img != NULL) {
            has_binding = true;
            lv_img_set_src(g_speed_bg_bindings[index].driving_img, path);
            lv_obj_clear_flag(g_speed_bg_bindings[index].driving_img, LV_OBJ_FLAG_HIDDEN);
            lv_obj_invalidate(g_speed_bg_bindings[index].driving_img);
        }
    }

    return has_binding;
}

static bool try_apply_music_right_image(bool drop_cache)
{
    const char *path;

    if (g_music_right_img == NULL) {
        return false;
    }

    path = resolve_music_right_image_path();
    if (path == NULL) {
        lv_obj_add_flag(g_music_right_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_invalidate(g_music_right_img);
        return false;
    }

    if (drop_cache) {
        drop_image_cache_path(path);
    }

    lv_img_set_src(g_music_right_img, path);
    lv_obj_clear_flag(g_music_right_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(g_music_right_img);
    return true;
}

static bool try_apply_bottom_layer(bool drop_cache)
{
    const char *path;

    if (g_bottom_layer_img == NULL) {
        return false;
    }

    path = resolve_bottom_layer_path();
    if (path == NULL) {
        lv_obj_add_flag(g_bottom_layer_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_invalidate(g_bottom_layer_img);
        return false;
    }

    if (drop_cache) {
        drop_image_cache_path(path);
    }

    lv_img_set_src(g_bottom_layer_img, path);
    lv_obj_clear_flag(g_bottom_layer_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(g_bottom_layer_img);
    return true;
}

static void stop_navigation_background_retry(void)
{
    if (g_navigation_bg_retry_timer != NULL) {
        lv_timer_del(g_navigation_bg_retry_timer);
        g_navigation_bg_retry_timer = NULL;
    }

    g_navigation_bg_retry_attempts_left = 0;
}

static void schedule_navigation_background_retry(uint32_t attempts)
{
    stop_navigation_background_retry();

    if (attempts == 0U) {
        return;
    }

    g_navigation_bg_retry_attempts_left = attempts;
    g_navigation_bg_retry_timer = lv_timer_create(navigation_background_retry_timer_cb,
                                                  SCOOTERDEMO_BG_RETRY_INTERVAL_MS,
                                                  NULL);
}

static void release_theme_image_resources(int theme_index)
{
    const rivotek_theme_profile_t *theme = get_theme_profile(theme_index);

    drop_image_cache_path(theme->home_bg_path);
    drop_image_cache_path(theme->nav_bg_path);
    drop_image_cache_path(theme->speed_bg_path);
    drop_image_cache_path(theme->music_right_img_path);
    drop_image_cache_path(theme->bottom_layer_path);

    for (int i = 0; i < SCOOTERDEMO_SIDEBAR_ITEM_COUNT; i++) {
        drop_image_cache_path(theme->sidebar_selected_paths[i]);
        drop_image_cache_path(theme->sidebar_unselected_paths[i]);
    }
}

static void release_theme_image_resources_async(void *user_data)
{
    rivotek_theme_release_request_t *request;

    request = (rivotek_theme_release_request_t *)user_data;
    if (request == NULL) {
        return;
    }

    if (normalize_theme_index(request->theme_index) != g_active_theme_index) {
        release_theme_image_resources(request->theme_index);
    }

    free(request);
}

static void schedule_theme_resource_release(int theme_index)
{
    rivotek_theme_release_request_t *request;

    request = malloc(sizeof(*request));
    if (request == NULL) {
        return;
    }

    request->theme_index = normalize_theme_index(theme_index);
    if (lv_async_call(release_theme_image_resources_async, request) != LV_RESULT_OK) {
        free(request);
    }
}

static void refresh_home_background(void)
{
    bool has_path = try_apply_home_background(true);

    if (has_path) {
        schedule_home_background_retry(1U);
    } else {
        schedule_home_background_retry(SCOOTERDEMO_BG_RETRY_ATTEMPTS);
    }
}

static void refresh_navigation_background(void)
{
    bool has_path = try_apply_navigation_background(true);

    if (has_path) {
        schedule_navigation_background_retry(1U);
    } else {
        schedule_navigation_background_retry(SCOOTERDEMO_BG_RETRY_ATTEMPTS);
    }
}

static void notify_theme_changed(void)
{
    if (g_theme_changed_cb != NULL) {
        g_theme_changed_cb(g_active_theme_index, g_theme_changed_cb_user_data);
    }
}

static void apply_theme_selection_now(int theme_index)
{
    int previous_theme_index;

    previous_theme_index = g_active_theme_index;
    g_active_theme_index = normalize_theme_index(theme_index);
    refresh_home_background();
    refresh_navigation_background();
    refresh_speed_background();
    refresh_music_right_image();
    refresh_bottom_layer();
    notify_theme_changed();

    if (previous_theme_index != g_active_theme_index) {
        schedule_theme_resource_release(previous_theme_index);
    }
}

static void theme_switch_timer_cb(lv_timer_t *timer)
{
    int theme_index = g_pending_theme_index;

    g_pending_theme_index = -1;
    if (g_theme_switch_timer == timer) {
        g_theme_switch_timer = NULL;
    }
    lv_timer_del(timer);

    if (theme_index >= 0) {
        apply_theme_selection_now(theme_index);
    }
}

static void home_background_retry_timer_cb(lv_timer_t *timer)
{
    bool ok;

    if (g_home_bg_retry_timer == timer) {
        g_home_bg_retry_timer = NULL;
    }
    lv_timer_del(timer);

    if (g_home_bg_retry_attempts_left == 0U) {
        return;
    }

    g_home_bg_retry_attempts_left--;
    ok = try_apply_home_background(true);
    if (!ok && g_home_bg_retry_attempts_left > 0U) {
        g_home_bg_retry_timer = lv_timer_create(home_background_retry_timer_cb,
                                                SCOOTERDEMO_BG_RETRY_INTERVAL_MS,
                                                NULL);
    } else {
        g_home_bg_retry_attempts_left = 0U;
    }
}

static void navigation_background_retry_timer_cb(lv_timer_t *timer)
{
    bool ok;

    if (g_navigation_bg_retry_timer == timer) {
        g_navigation_bg_retry_timer = NULL;
    }
    lv_timer_del(timer);

    if (g_navigation_bg_retry_attempts_left == 0U) {
        return;
    }

    g_navigation_bg_retry_attempts_left--;
    ok = try_apply_navigation_background(true);
    if (!ok && g_navigation_bg_retry_attempts_left > 0U) {
        g_navigation_bg_retry_timer = lv_timer_create(navigation_background_retry_timer_cb,
                                                      SCOOTERDEMO_BG_RETRY_INTERVAL_MS,
                                                      NULL);
    } else {
        g_navigation_bg_retry_attempts_left = 0U;
    }
}

void rivotek_theme_component_init(int initial_theme_index)
{
    g_active_theme_index = normalize_theme_index(initial_theme_index);
    g_pending_theme_index = -1;
}

void rivotek_theme_component_set_changed_cb(rivotek_theme_changed_cb_t cb,
                                            void *user_data)
{
    g_theme_changed_cb = cb;
    g_theme_changed_cb_user_data = user_data;
}

void rivotek_theme_component_request_apply(int theme_index)
{
    g_pending_theme_index = normalize_theme_index(theme_index);

    if (g_theme_switch_timer != NULL) {
        lv_timer_del(g_theme_switch_timer);
        g_theme_switch_timer = NULL;
    }

    g_theme_switch_timer = lv_timer_create(theme_switch_timer_cb,
                                           SCOOTERDEMO_THEME_SWITCH_DELAY_MS,
                                           NULL);
    if (g_theme_switch_timer == NULL) {
        apply_theme_selection_now(g_pending_theme_index);
        g_pending_theme_index = -1;
    }
}

static void refresh_speed_background(void)
{
    try_apply_speed_background(true);
}

static void refresh_music_right_image(void)
{
    try_apply_music_right_image(true);
}

static void refresh_bottom_layer(void)
{
    try_apply_bottom_layer(true);
}

static void register_speed_background_binding(lv_obj_t *img, lv_obj_t *driving_img)
{
    int empty_index = -1;
    int index;

    if (img == NULL && driving_img == NULL) {
        return;
    }

    for (index = 0; index < SCOOTERDEMO_SPEED_BG_BINDING_COUNT; index++) {
        if (g_speed_bg_bindings[index].img == img &&
            g_speed_bg_bindings[index].driving_img == driving_img) {
            return;
        }

        if (empty_index < 0 &&
            g_speed_bg_bindings[index].img == NULL &&
            g_speed_bg_bindings[index].driving_img == NULL) {
            empty_index = index;
        }
    }

    if (empty_index < 0) {
        empty_index = SCOOTERDEMO_SPEED_BG_BINDING_COUNT - 1;
    }

    g_speed_bg_bindings[empty_index].img = img;
    g_speed_bg_bindings[empty_index].driving_img = driving_img;
}

int rivotek_theme_component_get_active_index(void)
{
    return g_active_theme_index;
}

void rivotek_theme_component_bind_home_background(lv_obj_t *img)
{
    g_home_bg_img = img;
    refresh_home_background();
}

void rivotek_theme_component_bind_navigation_background(lv_obj_t *img,
                                                        lv_obj_t *slot)
{
    g_navigation_bg_img = img;
    g_navigation_bg_slot = slot;
    refresh_navigation_background();
}

void rivotek_theme_component_refresh_backgrounds(void)
{
    refresh_home_background();
    refresh_navigation_background();
    refresh_speed_background();
    refresh_music_right_image();
    refresh_bottom_layer();
}

void rivotek_theme_component_refresh_navigation_background(void)
{
    refresh_navigation_background();
}

bool rivotek_theme_component_apply_sidebar_item(lv_obj_t *img,
                                                int item_index,
                                                bool selected)
{
    const rivotek_theme_profile_t *theme;
    const char *path;

    if (img == NULL || item_index < 0 || item_index >= SCOOTERDEMO_SIDEBAR_ITEM_COUNT) {
        return false;
    }

    theme = get_theme_profile(g_active_theme_index);
    path = selected ? theme->sidebar_selected_paths[item_index]
                    : theme->sidebar_unselected_paths[item_index];

    if (access(path, F_OK) == 0) {
        lv_img_set_src(img, path);
    } else {
        path = NULL;
    }

    lv_obj_set_style_img_recolor(img,
                                 lv_color_hex(selected ? theme->sidebar_selected_recolor_hex
                                                       : theme->sidebar_unselected_recolor_hex),
                                 0);
    lv_obj_set_style_img_recolor_opa(img,
                                     selected ? theme->sidebar_selected_recolor_opa
                                              : theme->sidebar_unselected_recolor_opa,
                                     0);
    lv_obj_invalidate(img);
    return path != NULL;
}

void rivotek_theme_component_bind_speed_background(lv_obj_t *img, lv_obj_t *driving_img)
{
    register_speed_background_binding(img, driving_img);
    rivotek_theme_component_refresh_speed_background();
}

void rivotek_theme_component_bind_music_right_image(lv_obj_t *img)
{
    g_music_right_img = img;
    refresh_music_right_image();
}

void rivotek_theme_component_bind_bottom_layer(lv_obj_t *img)
{
    g_bottom_layer_img = img;
    rivotek_theme_component_refresh_bottom_layer();
}

void rivotek_theme_component_refresh_speed_background(void)
{
    refresh_speed_background();
}

void rivotek_theme_component_refresh_bottom_layer(void)
{
    refresh_bottom_layer();
}
