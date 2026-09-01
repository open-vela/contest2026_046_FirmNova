#include <unistd.h>
#include <stdio.h>
#include <string.h>

#include <lvgl/lvgl.h>

#include "scooterdemo_pages.h"
#include "theme_impl.h"

#define COLOR_NAV_BG lv_color_hex(0x000000)

static lv_obj_t *g_navigation_bg_img;
static lv_obj_t *g_navigation_img_slot;
static char g_navigation_bg_path[128];

void scooterdemo_navigation_page_set_background(const char *bg_path)
{
    if (g_navigation_bg_img == NULL || g_navigation_img_slot == NULL) {
        return;
    }

    if (bg_path == NULL || bg_path[0] == '\0') {
        g_navigation_bg_path[0] = '\0';
        rivotek_theme_component_refresh_navigation_background();
        return;
    }

    if (access(bg_path, F_OK) != 0) {
        return;
    }

    if (strcmp(g_navigation_bg_path, bg_path) != 0) {
        snprintf(g_navigation_bg_path, sizeof(g_navigation_bg_path), "%s", bg_path);
        printf("navigation_page: map=%s\n", g_navigation_bg_path);
    }

    lv_img_set_src(g_navigation_bg_img, g_navigation_bg_path);
    lv_obj_set_size(g_navigation_bg_img,
                    lv_obj_get_width(g_navigation_img_slot),
                    lv_obj_get_height(g_navigation_img_slot));
    lv_obj_align(g_navigation_bg_img, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(g_navigation_bg_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_navigation_bg_img);
    lv_obj_invalidate(g_navigation_bg_img);
}

void scooterdemo_navigation_page_build(lv_obj_t *parent,
                                       const scooterdemo_ui_metrics_t *ui)
{
    g_navigation_img_slot = lv_obj_create(parent);
    lv_obj_remove_style_all(g_navigation_img_slot);
    lv_obj_set_size(g_navigation_img_slot,
                    SCOOTERDEMO_W(ui, 800),
                    SCOOTERDEMO_H(ui, 480));
    lv_obj_align(g_navigation_img_slot, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(g_navigation_img_slot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(g_navigation_img_slot, LV_OPA_0, 0);
    lv_obj_set_style_border_width(g_navigation_img_slot, 0, 0);
    lv_obj_set_style_radius(g_navigation_img_slot, 0, 0);

    g_navigation_bg_img = lv_img_create(g_navigation_img_slot);
    lv_obj_align(g_navigation_bg_img, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(g_navigation_bg_img, LV_OBJ_FLAG_HIDDEN);

    rivotek_theme_component_bind_navigation_background(g_navigation_bg_img,
                                                       g_navigation_img_slot);
}
