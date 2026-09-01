#include <nuttx/config.h>
#include <unistd.h>

#include <lvgl/lvgl.h>

#include "scooterdemo_pages.h"

#define BOOT_DEMO_GIF_RESOURCE_PATH "/resource/imgs/boot_demo.gif"
#define BOOT_DEMO_DURATION_MS 5000
#define BOOT_DEMO_BG_COLOR lv_color_hex(0x000000)

void scooterdemo_boot_demo_run(lv_obj_t *screen)
{
    lv_obj_t *overlay;
    lv_obj_t *gif;
    uint32_t start_tick;

    if (screen == NULL) {
        return;
    }

    overlay = lv_obj_create(screen);
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, lv_obj_get_width(screen), lv_obj_get_height(screen));
    lv_obj_align(overlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(overlay, BOOT_DEMO_BG_COLOR, 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
    // 增加边框和阴影效果，提高对比度
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_set_style_shadow_width(overlay, 0, 0);
    lv_obj_move_foreground(overlay);

    gif = lv_gif_create(overlay);
    lv_obj_center(gif);
    if (access(BOOT_DEMO_GIF_RESOURCE_PATH, F_OK) == 0) {
        lv_gif_set_src(gif, BOOT_DEMO_GIF_RESOURCE_PATH);
    } else {
        lv_obj_t *label = lv_label_create(overlay);
        lv_label_set_text(label, "boot demo");
        lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_center(label);
    }

    lv_obj_invalidate(overlay);
    lv_refr_now(NULL);

    start_tick = lv_tick_get();
    while (lv_tick_elaps(start_tick) < BOOT_DEMO_DURATION_MS) {
        lv_timer_handler();
        usleep(5000);
    }

    lv_obj_del(overlay);
    lv_refr_now(NULL);
}