#include <stddef.h>
#include <unistd.h>

#include "scooterdemo_pages.h"
#include "scooterdemo_settings_subpages.h"

#define COLOR_TEXT lv_color_hex(0xFFFFFF)
#define COLOR_TEXT_DARK lv_color_hex(0x111111)
#define COLOR_HIGHLIGHT lv_color_hex(0x4BB2FF)
#define COLOR_MENU_SELECTED_BG lv_color_hex(0xFFFFFF)
#define COLOR_PAGE_BG_TOP lv_color_hex(0x08131D)
#define COLOR_PAGE_BG_BOTTOM lv_color_hex(0x04070C)
#define COLOR_DIVIDER lv_color_hex(0x5BA2C7)

#define SETTINGS_MENU_ITEM_COUNT 5
#define SETTINGS_MENU_THEME_INDEX 4
#define SETTINGS_MENU_ITEM_X_OFFSET 20
#define SETTINGS_MENU_ITEM_Y_START 100
#define SETTINGS_MENU_ITEM_Y_GAP 61

static const char *g_settings_menu_items[SETTINGS_MENU_ITEM_COUNT] = {
    "系统设置",
    "胎压监测",
    "驻车设置",
    "车辆信息",
    "主题切换",
};
static const char *g_settings_menu_icon_path_1[SETTINGS_MENU_ITEM_COUNT] = {
    "/resource/imgs/settings_menu_system_1_20.png",
    "/resource/imgs/settings_menu_tire_1_20.png",
    "/resource/imgs/settings_menu_parking_1_20.png",
    "/resource/imgs/settings_menu_vehicle_1_20.png",
    "/resource/imgs/settings_menu_theme_1_20.png",
};
static const char *g_settings_menu_icon_path_2[SETTINGS_MENU_ITEM_COUNT] = {
    "/resource/imgs/settings_menu_system_2_20.png",
    "/resource/imgs/settings_menu_tire_2_20.png",
    "/resource/imgs/settings_menu_parking_2_20.png",
    "/resource/imgs/settings_menu_vehicle_2_20.png",
    "/resource/imgs/settings_menu_theme_2_20.png",
};

static const char *g_settings_bg_path = "/resource/imgs/settings_bg.png";
static const char *g_settings_bg2_path = "/resource/imgs/settings_bg2.png";
static const char *g_settings_left_nav_bg_path = "/resource/imgs/settings_left_nav_bg.png";

typedef enum {
    /* 焦点在左侧一级菜单。 */
    SETTINGS_FOCUS_LEFT_MENU = 0,
    /* 焦点在右侧普通子页控件。 */
    SETTINGS_FOCUS_SUBPAGE_CONTROL,
} settings_focus_mode_t;

static lv_obj_t *g_menu_labels[SETTINGS_MENU_ITEM_COUNT];
static lv_obj_t *g_menu_items[SETTINGS_MENU_ITEM_COUNT];
static lv_obj_t *g_menu_icons[SETTINGS_MENU_ITEM_COUNT];
static lv_obj_t *g_settings_bg_img;
static lv_obj_t *g_right_panel;
static lv_obj_t *g_active_subpage_root;
static int g_selected_menu_index = 4;
static int g_active_subpage_index = -1;
static settings_focus_mode_t g_focus_mode = SETTINGS_FOCUS_LEFT_MENU;
static scooterdemo_theme_apply_cb_t g_theme_apply_cb;
static void *g_theme_apply_cb_user_data;
static scooterdemo_ui_metrics_t g_ui_metrics;


static void settings_reset_obj(lv_obj_t *obj)
{
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static void settings_create_backdrop(lv_obj_t *parent, const scooterdemo_ui_metrics_t *ui)
{
    lv_obj_t *backdrop = lv_obj_create(parent);

    settings_reset_obj(backdrop);
    lv_obj_set_size(backdrop, ui->disp_w, ui->disp_h);
    lv_obj_align(backdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(backdrop, COLOR_PAGE_BG_TOP, 0);
    lv_obj_set_style_bg_grad_color(backdrop, COLOR_PAGE_BG_BOTTOM, 0);
    lv_obj_set_style_bg_grad_dir(backdrop, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(backdrop, LV_OPA_COVER, 0);

    g_settings_bg_img = lv_img_create(backdrop);
    lv_obj_align(g_settings_bg_img, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(g_settings_bg_img, LV_OBJ_FLAG_HIDDEN);

    lv_obj_move_background(backdrop);
}

static void settings_apply_background_for_page(int page_index)
{
    const char *bg_path = g_settings_bg_path;

    if (page_index == SCOOTERDEMO_SETTINGS_SUBPAGE_TIRE ||
        page_index == SCOOTERDEMO_SETTINGS_SUBPAGE_VEHICLE) {
        bg_path = g_settings_bg2_path;
    }

    if (g_settings_bg_img == NULL) {
        return;
    }

    if (access(bg_path, F_OK) != 0) {
        bg_path = g_settings_bg_path;
    }

    if (access(bg_path, F_OK) == 0) {
        lv_img_set_src(g_settings_bg_img, bg_path);
        lv_obj_clear_flag(g_settings_bg_img, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(g_settings_bg_img, LV_OBJ_FLAG_HIDDEN);
    }
}


static void update_menu_highlight(void)
{
    for (int i = 0; i < SETTINGS_MENU_ITEM_COUNT; i++) {
        const char *icon_path;
        bool is_selected;

        if (g_menu_items[i] == NULL || g_menu_labels[i] == NULL) {
            continue;
        }

        is_selected = (i == g_selected_menu_index &&
                       (g_focus_mode == SETTINGS_FOCUS_LEFT_MENU ||
                        g_focus_mode == SETTINGS_FOCUS_SUBPAGE_CONTROL));

        if (is_selected) {
            lv_obj_set_style_bg_color(g_menu_items[i], COLOR_MENU_SELECTED_BG, 0);
            lv_obj_set_style_bg_opa(g_menu_items[i], LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(g_menu_items[i], 0, 0);
            lv_obj_set_style_border_color(g_menu_items[i], COLOR_MENU_SELECTED_BG, 0);
            lv_obj_set_style_radius(g_menu_items[i], 20, 0);
            lv_obj_set_style_text_color(g_menu_labels[i], COLOR_TEXT_DARK, 0);
        } else {
            lv_obj_set_style_bg_opa(g_menu_items[i], LV_OPA_0, 0);
            lv_obj_set_style_border_width(g_menu_items[i], 0, 0);
            lv_obj_set_style_outline_width(g_menu_items[i], 0, 0);
            lv_obj_set_style_shadow_width(g_menu_items[i], 0, 0);
            lv_obj_set_style_text_color(g_menu_labels[i], COLOR_TEXT, 0);
        }

        if (g_menu_icons[i] != NULL) {
            icon_path = is_selected ? g_settings_menu_icon_path_2[i]
                                    : g_settings_menu_icon_path_1[i];
            if (access(icon_path, F_OK) == 0) {
                lv_img_set_src(g_menu_icons[i], icon_path);
                lv_obj_clear_flag(g_menu_icons[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(g_menu_icons[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
}

/* 将“向上”导航事件分发到当前活动子页。 */
static bool dispatch_subpage_nav_up(void)
{
    switch (g_active_subpage_index) {
    case SCOOTERDEMO_SETTINGS_SUBPAGE_SYSTEM:
        return scooterdemo_settings_subpage_system_handle_nav_up();
    case SCOOTERDEMO_SETTINGS_SUBPAGE_TIRE:
        return scooterdemo_settings_subpage_tire_handle_nav_up();
    case SCOOTERDEMO_SETTINGS_SUBPAGE_PARKING:
        return scooterdemo_settings_subpage_parking_handle_nav_up();
    case SCOOTERDEMO_SETTINGS_SUBPAGE_VEHICLE:
        return scooterdemo_settings_subpage_vehicle_handle_nav_up();
    case SCOOTERDEMO_SETTINGS_SUBPAGE_THEME:
        return scooterdemo_settings_subpage_theme_handle_nav_up();
    default:
        return false;
    }
}

/* 将“向下”导航事件分发到当前活动子页。 */
static bool dispatch_subpage_nav_down(void)
{
    switch (g_active_subpage_index) {
    case SCOOTERDEMO_SETTINGS_SUBPAGE_SYSTEM:
        return scooterdemo_settings_subpage_system_handle_nav_down();
    case SCOOTERDEMO_SETTINGS_SUBPAGE_TIRE:
        return scooterdemo_settings_subpage_tire_handle_nav_down();
    case SCOOTERDEMO_SETTINGS_SUBPAGE_PARKING:
        return scooterdemo_settings_subpage_parking_handle_nav_down();
    case SCOOTERDEMO_SETTINGS_SUBPAGE_VEHICLE:
        return scooterdemo_settings_subpage_vehicle_handle_nav_down();
    case SCOOTERDEMO_SETTINGS_SUBPAGE_THEME:
        return scooterdemo_settings_subpage_theme_handle_nav_down();
    default:
        return false;
    }
}

/* 将“确认”事件分发到当前活动子页。 */
static bool dispatch_subpage_enter(void)
{
    switch (g_active_subpage_index) {
    case SCOOTERDEMO_SETTINGS_SUBPAGE_SYSTEM:
        return scooterdemo_settings_subpage_system_handle_enter();
    case SCOOTERDEMO_SETTINGS_SUBPAGE_TIRE:
        return scooterdemo_settings_subpage_tire_handle_enter();
    case SCOOTERDEMO_SETTINGS_SUBPAGE_PARKING:
        return scooterdemo_settings_subpage_parking_handle_enter();
    case SCOOTERDEMO_SETTINGS_SUBPAGE_VEHICLE:
        return scooterdemo_settings_subpage_vehicle_handle_enter();
    case SCOOTERDEMO_SETTINGS_SUBPAGE_THEME:
        return scooterdemo_settings_subpage_theme_handle_enter();
    default:
        return false;
    }
}

/* 为当前活动子页设置焦点模式（进入/退出）。 */
static void dispatch_subpage_focus(bool focused)
{
    switch (g_active_subpage_index) {
    case SCOOTERDEMO_SETTINGS_SUBPAGE_SYSTEM:
        scooterdemo_settings_subpage_system_set_focus(focused);
        break;
    case SCOOTERDEMO_SETTINGS_SUBPAGE_PARKING:
        scooterdemo_settings_subpage_parking_set_focus(focused);
        break;
    case SCOOTERDEMO_SETTINGS_SUBPAGE_THEME:
        scooterdemo_settings_subpage_theme_set_focus(focused);
        break;
    default:
        break;
    }
}

/* 卸载当前活动子页并释放关联状态。 */
static void unload_active_subpage(void)
{
    if (g_active_subpage_root == NULL) {
        return;
    }

    if (g_active_subpage_index == SCOOTERDEMO_SETTINGS_SUBPAGE_SYSTEM) {
        scooterdemo_settings_subpage_system_release();
    }

    if (g_active_subpage_index == SCOOTERDEMO_SETTINGS_SUBPAGE_TIRE) {
        scooterdemo_settings_subpage_tire_release();
    }

    if (g_active_subpage_index == SCOOTERDEMO_SETTINGS_SUBPAGE_PARKING) {
        scooterdemo_settings_subpage_parking_release();
    }

    if (g_active_subpage_index == SETTINGS_MENU_THEME_INDEX) {
        scooterdemo_settings_subpage_theme_release();
    }

    if (g_active_subpage_index == SCOOTERDEMO_SETTINGS_SUBPAGE_VEHICLE) {
        scooterdemo_settings_subpage_vehicle_release();
    }

    lv_obj_del(g_active_subpage_root);
    g_active_subpage_root = NULL;
    g_active_subpage_index = -1;
}

/*
 * 加载指定索引的右侧子页。
 * 参数 page_index: 二级菜单索引。
 */
static void load_secondary_page(int page_index)
{
    if (g_right_panel == NULL || page_index < 0 || page_index >= SETTINGS_MENU_ITEM_COUNT) {
        return;
    }

    if (g_active_subpage_index == page_index && g_active_subpage_root != NULL) {
        settings_apply_background_for_page(page_index);
        if (page_index == SETTINGS_MENU_THEME_INDEX) {
            scooterdemo_settings_subpage_theme_set_focus(g_focus_mode == SETTINGS_FOCUS_SUBPAGE_CONTROL);
        }
        return;
    }

    unload_active_subpage();

    switch (page_index) {
    case 0:
        g_active_subpage_root = scooterdemo_settings_subpage_system_build(g_right_panel,
                                                                           &g_ui_metrics);
        break;

    case 1:
        g_active_subpage_root = scooterdemo_settings_subpage_tire_build(g_right_panel,
                                                                         &g_ui_metrics);
        break;

    case 2:
        g_active_subpage_root = scooterdemo_settings_subpage_parking_build(g_right_panel,
                                                                            &g_ui_metrics);
        break;

    case 3:
        g_active_subpage_root = scooterdemo_settings_subpage_vehicle_build(g_right_panel,
                                                                            &g_ui_metrics);
        break;

    case SETTINGS_MENU_THEME_INDEX:
        g_active_subpage_root = scooterdemo_settings_subpage_theme_build(g_right_panel,
                                                                          &g_ui_metrics);
        break;

    default:
        break;
    }

    if (g_active_subpage_root != NULL) {
        g_active_subpage_index = page_index;
    }

    settings_apply_background_for_page(page_index);

    if (page_index == SETTINGS_MENU_THEME_INDEX) {
        scooterdemo_settings_subpage_theme_set_focus(g_focus_mode == SETTINGS_FOCUS_SUBPAGE_CONTROL);
    }
}

/*
 * 设置主题应用回调。
 * 参数 cb: 主题应用回调。
 * 参数 user_data: 回调透传上下文。
 */
void scooterdemo_settings_page_set_theme_apply_cb(scooterdemo_theme_apply_cb_t cb,
                                                  void *user_data)
{
    g_theme_apply_cb = cb;
    g_theme_apply_cb_user_data = user_data;
}

/* 获取当前生效主题索引。 */
int scooterdemo_settings_page_get_active_theme(void)
{
    return scooterdemo_settings_subpage_theme_get_active_index();
}

/* 设置页面显示时初始化焦点与右侧子页。 */
void scooterdemo_settings_page_on_show(void)
{
    g_selected_menu_index = SETTINGS_MENU_THEME_INDEX;
    g_focus_mode = SETTINGS_FOCUS_LEFT_MENU;
    update_menu_highlight();
    load_secondary_page(g_selected_menu_index);
    scooterdemo_settings_subpage_theme_set_focus(false);
}

/* 处理设置页向上导航事件。 */
bool scooterdemo_settings_page_handle_nav_up(void)
{
    if (g_focus_mode == SETTINGS_FOCUS_LEFT_MENU) {
        g_selected_menu_index--;
        if (g_selected_menu_index < 0) {
            g_selected_menu_index = SETTINGS_MENU_ITEM_COUNT - 1;
        }
        update_menu_highlight();
        load_secondary_page(g_selected_menu_index);
        return true;
    }

    if (g_focus_mode == SETTINGS_FOCUS_SUBPAGE_CONTROL) {
        return dispatch_subpage_nav_up();
    }

    return false;
}

/* 处理设置页向下导航事件。 */
bool scooterdemo_settings_page_handle_nav_down(void)
{
    if (g_focus_mode == SETTINGS_FOCUS_LEFT_MENU) {
        g_selected_menu_index++;
        if (g_selected_menu_index >= SETTINGS_MENU_ITEM_COUNT) {
            g_selected_menu_index = 0;
        }
        update_menu_highlight();
        load_secondary_page(g_selected_menu_index);
        return true;
    }

    if (g_focus_mode == SETTINGS_FOCUS_SUBPAGE_CONTROL) {
        return dispatch_subpage_nav_down();
    }

    return false;
}

/* 处理设置页确认事件。 */
bool scooterdemo_settings_page_handle_enter(void)
{
    if (g_focus_mode == SETTINGS_FOCUS_LEFT_MENU) {
        g_focus_mode = SETTINGS_FOCUS_SUBPAGE_CONTROL;
        update_menu_highlight();
        load_secondary_page(g_selected_menu_index);
        dispatch_subpage_focus(true);
        return true;
    }

    if (g_focus_mode == SETTINGS_FOCUS_SUBPAGE_CONTROL) {
        bool handled = dispatch_subpage_enter();

        if (handled &&
            g_active_subpage_index == SETTINGS_MENU_THEME_INDEX &&
            g_theme_apply_cb != NULL) {
            g_theme_apply_cb(scooterdemo_settings_subpage_theme_get_active_index(),
                             g_theme_apply_cb_user_data);
        }

        return handled;
    }

    return false;
}

/* 处理设置页双击事件。 */
bool scooterdemo_settings_page_handle_double_click(void)
{
    if (g_focus_mode == SETTINGS_FOCUS_SUBPAGE_CONTROL) {
        dispatch_subpage_focus(false);
        g_focus_mode = SETTINGS_FOCUS_LEFT_MENU;
        update_menu_highlight();
        return true;
    }

    return false;
}

/* 处理设置页返回事件。 */
bool scooterdemo_settings_page_handle_back(void)
{
    if (g_focus_mode == SETTINGS_FOCUS_SUBPAGE_CONTROL) {
        dispatch_subpage_focus(false);
        g_focus_mode = SETTINGS_FOCUS_LEFT_MENU;
        update_menu_highlight();
        return true;
    }

    return false;
}

/*
 * 构建设置页。
 * 参数 parent: 页面父容器。
 * 参数 ui: UI 缩放与字体参数。
 */
void scooterdemo_settings_page_build(lv_obj_t *parent,
                                     const scooterdemo_ui_metrics_t *ui)
{
    lv_obj_t *left_panel;

    g_ui_metrics = *ui;
    g_settings_bg_img = NULL;

    lv_obj_set_style_bg_color(parent, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_0, 0);
    lv_obj_set_style_border_width(parent, 0, 0);
    lv_obj_set_style_outline_width(parent, 0, 0);
    lv_obj_set_style_shadow_width(parent, 0, 0);
    lv_obj_set_style_radius(parent, 0, 0);
    lv_obj_set_style_pad_all(parent, 0, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    settings_create_backdrop(parent, ui);

    left_panel = lv_obj_create(parent);
    lv_obj_set_size(left_panel, SCOOTERDEMO_W(ui, 194), ui->disp_h);
    lv_obj_align(left_panel, LV_ALIGN_TOP_LEFT, SCOOTERDEMO_W(ui, -1), 0);
    lv_obj_set_style_bg_opa(left_panel, LV_OPA_0, 0);
    lv_obj_set_style_border_width(left_panel, 0, 0);
    lv_obj_set_style_outline_width(left_panel, 0, 0);
    lv_obj_set_style_shadow_width(left_panel, 0, 0);
    lv_obj_set_style_radius(left_panel, 0, 0);
    lv_obj_set_style_pad_all(left_panel, 0, 0);
    lv_obj_set_scrollbar_mode(left_panel, LV_SCROLLBAR_MODE_OFF);

    if (access(g_settings_left_nav_bg_path, F_OK) == 0) {
        lv_obj_t *left_panel_bg = lv_img_create(left_panel);

        lv_img_set_src(left_panel_bg, g_settings_left_nav_bg_path);
        lv_obj_align(left_panel_bg, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_clear_flag(left_panel_bg, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_move_background(left_panel_bg);
    }

    for (int i = 0; i < SETTINGS_MENU_ITEM_COUNT; i++) {
        lv_obj_t *icon_slot;

        g_menu_items[i] = lv_obj_create(left_panel);
        lv_obj_set_size(g_menu_items[i], SCOOTERDEMO_W(ui, 156), SCOOTERDEMO_H(ui, 48));
        lv_obj_align(g_menu_items[i], LV_ALIGN_TOP_LEFT,
                     SCOOTERDEMO_W(ui, SETTINGS_MENU_ITEM_X_OFFSET),
                     SCOOTERDEMO_H(ui, SETTINGS_MENU_ITEM_Y_START +
                                       i * SETTINGS_MENU_ITEM_Y_GAP));
        lv_obj_set_style_bg_opa(g_menu_items[i], LV_OPA_0, 0);
        lv_obj_set_style_border_width(g_menu_items[i], 0, 0);
        lv_obj_set_style_outline_width(g_menu_items[i], 0, 0);
        lv_obj_set_style_shadow_width(g_menu_items[i], 0, 0);
        lv_obj_set_style_pad_all(g_menu_items[i], 0, 0);
        lv_obj_clear_flag(g_menu_items[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(g_menu_items[i], LV_SCROLLBAR_MODE_OFF);

        icon_slot = lv_obj_create(g_menu_items[i]);
        settings_reset_obj(icon_slot);
        lv_obj_set_size(icon_slot, SCOOTERDEMO_W(ui, 20), SCOOTERDEMO_H(ui, 20));
        lv_obj_align(icon_slot, LV_ALIGN_LEFT_MID, SCOOTERDEMO_W(ui, 21), 0);
        g_menu_icons[i] = lv_img_create(icon_slot);
        lv_obj_center(g_menu_icons[i]);

        g_menu_labels[i] = lv_label_create(g_menu_items[i]);
        lv_label_set_text(g_menu_labels[i], g_settings_menu_items[i]);
        lv_obj_set_style_text_font(g_menu_labels[i], ui->font_24, 0);
        lv_obj_align(g_menu_labels[i], LV_ALIGN_LEFT_MID, SCOOTERDEMO_W(ui, 49), 0);
    }

    g_right_panel = lv_obj_create(parent);
    lv_obj_set_size(g_right_panel, SCOOTERDEMO_W(ui, 585), SCOOTERDEMO_H(ui, 360));
    lv_obj_align(g_right_panel, LV_ALIGN_TOP_LEFT, SCOOTERDEMO_W(ui, 206), SCOOTERDEMO_H(ui, 62));
    lv_obj_set_style_bg_opa(g_right_panel, LV_OPA_0, 0);
    lv_obj_set_style_border_width(g_right_panel, 0, 0);
    lv_obj_set_style_outline_width(g_right_panel, 0, 0);
    lv_obj_set_style_shadow_width(g_right_panel, 0, 0);
    lv_obj_set_style_pad_all(g_right_panel, 0, 0);
    lv_obj_set_scrollbar_mode(g_right_panel, LV_SCROLLBAR_MODE_OFF);

    scooterdemo_settings_page_on_show();
}
