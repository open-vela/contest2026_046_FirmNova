#include <nuttx/config.h>

#include "eca_client_ui_common.h"
#include "eca_client_widgets.h"

void eca_client_health_card_create(lv_obj_t *parent, eca_client_model_t *model)
{
    const eca_client_ui_fonts_t *fonts = eca_client_ui_fonts_get();
    lv_obj_t *card;
    lv_obj_t *hint_dot;
    lv_obj_t *reminder_title;
    lv_obj_t *reminder_text;

    card = eca_client_ui_create_card(parent, 23, 75, 184, 157, 16);
    eca_client_ui_create_label(card, "提醒计划", fonts->font_bold_24,
                               ECA_CLIENT_COLOR_TEXT, 24, 26);
    reminder_title = eca_client_ui_create_bound_label(
        card, &model->reminder_title, NULL, fonts->font_bold_18,
        ECA_CLIENT_COLOR_TEXT, 24, 66);
    lv_obj_set_width(reminder_title, eca_client_ui_scale_x(136));
    lv_label_set_long_mode(reminder_title, LV_LABEL_LONG_DOT);
    reminder_text = eca_client_ui_create_bound_label(
        card, &model->reminder_text, NULL, fonts->font_14,
        ECA_CLIENT_COLOR_TEXT, 24, 96);
    lv_obj_set_width(reminder_text, eca_client_ui_scale_x(136));
    lv_label_set_long_mode(reminder_text, LV_LABEL_LONG_WRAP);

    eca_client_ui_create_location_icon(card, 24, 139);
    eca_client_ui_create_bound_label(card, &model->hint, NULL, fonts->font_14,
                                     ECA_CLIENT_COLOR_MUTED, 46, 135);

    hint_dot = eca_client_ui_create_circle(card, 151, 28, 9, 0xFFE7A5);
    lv_obj_set_style_bg_opa(hint_dot, LV_OPA_60, 0);
    hint_dot = eca_client_ui_create_circle(card, 158, 28, 9, 0xFFE7A5);
    lv_obj_set_style_bg_opa(hint_dot, LV_OPA_60, 0);
}
