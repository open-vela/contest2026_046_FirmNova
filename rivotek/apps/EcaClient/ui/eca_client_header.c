#include <nuttx/config.h>

#include <stdio.h>

#include "eca_client_ai_agent.h"
#include "eca_client_ui_common.h"
#include "eca_client_widgets.h"

static void eca_client_time_event_cb(lv_event_t *event)
{
    eca_client_model_t *model;
    char text[48];

    model = (eca_client_model_t *)lv_event_get_user_data(event);
    snprintf(text, sizeof(text), "现在时间是%s", model->time_buf);
    eca_client_ai_agent_speak(text, NULL);
}

static void eca_client_temperature_event_cb(lv_event_t *event)
{
    eca_client_model_t *model;
    char text[64];

    model = (eca_client_model_t *)lv_event_get_user_data(event);
    snprintf(text, sizeof(text), "当前%s", model->temperature_buf);
    eca_client_ai_agent_speak(text, NULL);
}

static lv_obj_t *eca_client_header_hit_area(lv_obj_t *parent, int x,
                                            int width)
{
    lv_obj_t *area = lv_obj_create(parent);

    lv_obj_set_pos(area, eca_client_ui_scale_x(x),
                   eca_client_ui_scale_y(8));
    lv_obj_set_size(area, eca_client_ui_scale_x(width),
                    eca_client_ui_scale_y(48));
    lv_obj_clear_flag(area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(area, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(area, LV_OPA_20, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(area, 0, 0);
    lv_obj_set_style_radius(area, eca_client_ui_scale_x(8), 0);
    return area;
}

void eca_client_header_create(lv_obj_t *parent, eca_client_model_t *model)
{
    const eca_client_ui_fonts_t *fonts = eca_client_ui_fonts_get();

    eca_client_ui_create_label(parent, "时间:", fonts->font_bold_18,
                               ECA_CLIENT_COLOR_TEXT, 25, 24);
    eca_client_ui_create_bound_label(parent, &model->time, NULL,
                                     fonts->font_bold_24, ECA_CLIENT_COLOR_TEXT,
                                     75, 18);

    eca_client_ui_create_sun_icon(parent, 280, 19);
    eca_client_ui_create_bound_label(parent, &model->temperature, NULL,
                                     fonts->font_bold_18, ECA_CLIENT_COLOR_TEXT,
                                     313, 24);

    lv_obj_t *time_area = eca_client_header_hit_area(parent, 18, 205);
    lv_obj_t *temperature_area = eca_client_header_hit_area(parent, 270, 175);

    lv_obj_add_event_cb(time_area, eca_client_time_event_cb,
                        LV_EVENT_CLICKED, model);
    lv_obj_add_event_cb(temperature_area, eca_client_temperature_event_cb,
                        LV_EVENT_CLICKED, model);
}
