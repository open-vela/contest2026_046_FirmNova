#include <nuttx/config.h>

#include "eca_client_ui.h"
#include "eca_client_ui_common.h"
#include "eca_client_widgets.h"

void eca_client_create_screen(lv_obj_t *screen, eca_client_model_t *model)
{
    lv_disp_t *disp;
    lv_coord_t screen_width = ECA_CLIENT_SCREEN_WIDTH;
    lv_coord_t screen_height = ECA_CLIENT_SCREEN_HEIGHT;

    disp = lv_disp_get_default();
    if (disp != NULL) {
        screen_width = lv_disp_get_hor_res(disp);
        screen_height = lv_disp_get_ver_res(disp);
    }
    eca_client_ui_set_display_size(screen_width, screen_height);

    lv_obj_remove_style_all(screen);
    lv_obj_set_size(screen, screen_width, screen_height);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(screen, 0, 0);
    lv_obj_set_style_bg_color(screen, lv_color_hex(ECA_CLIENT_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);

    eca_client_header_create(screen, model);
    eca_client_health_card_create(screen, model);
    eca_client_voice_card_create(screen, model);
    eca_client_emergency_card_create(screen, model);
    eca_client_humidity_card_create(screen, model);
}
