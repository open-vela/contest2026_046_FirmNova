#include <nuttx/config.h>

#include <stdio.h>

#include "eca_client_ai_agent.h"
#include "eca_client_ui_common.h"
#include "eca_client_widgets.h"

static void eca_client_humidity_event_cb(lv_event_t *event)
{
    eca_client_model_t *model;
    char text[48];

    model = (eca_client_model_t *)lv_event_get_user_data(event);
    snprintf(text, sizeof(text), "当前湿度%s", model->humidity_buf);
    eca_client_ai_agent_speak(text, NULL);
}

void eca_client_humidity_card_create(lv_obj_t *parent, eca_client_model_t *model)
{
    const eca_client_ui_fonts_t *fonts = eca_client_ui_fonts_get();
    lv_obj_t *card;

    card = eca_client_ui_create_card(parent, 350, 167, 84, 80, 11);

    eca_client_ui_create_humidity_icon(card, 21, 0);
    eca_client_ui_create_bound_label(card, &model->humidity, NULL,
                                     fonts->font_bold_18,
                                     ECA_CLIENT_COLOR_TEXT, 12, 45);
    eca_client_ui_create_bound_label(card, &model->humidity_title, NULL,
                                     fonts->font_14, ECA_CLIENT_COLOR_MUTED,
                                     20, 62);

    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(card, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_add_event_cb(card, eca_client_humidity_event_cb,
                        LV_EVENT_CLICKED, model);
}
