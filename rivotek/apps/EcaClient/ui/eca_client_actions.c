#include <nuttx/config.h>

#include "eca_client_ai_agent.h"
#include "eca_client_log.h"
#include "eca_client_network.h"
#include <string.h>

#include "eca_client_ui_common.h"
#include "eca_client_widgets.h"

enum voice_ui_state_e {
    VOICE_UI_IDLE = 0,
    VOICE_UI_STARTING,
    VOICE_UI_STOPPING
};

static enum voice_ui_state_e g_voice_ui_state;

static void voice_status_timer_cb(lv_timer_t *timer)
{
    eca_client_model_t *model;
    char status[16];

    model = (eca_client_model_t *)lv_timer_get_user_data(timer);
    if (eca_client_ai_agent_voice_status(status, sizeof(status)) < 0) {
        return;
    }

    if (strcmp(status, "error") == 0) {
        g_voice_ui_state = VOICE_UI_IDLE;
        lv_subject_copy_string(&model->voice_title, "语音启动失败");
    } else if (g_voice_ui_state == VOICE_UI_STARTING &&
               strcmp(status, "recording") == 0) {
        lv_subject_copy_string(&model->voice_title, "松开结束");
    } else if (g_voice_ui_state == VOICE_UI_STOPPING &&
               strcmp(status, "processing") == 0) {
        lv_subject_copy_string(&model->voice_title, "识别中...");
    } else if (g_voice_ui_state == VOICE_UI_STOPPING &&
               strcmp(status, "idle") == 0) {
        g_voice_ui_state = VOICE_UI_IDLE;
        lv_subject_copy_string(&model->voice_title, "语音助手");
    }
}

static void voice_card_event_cb(lv_event_t *event)
{
    eca_client_model_t *model;
    lv_event_code_t code = lv_event_get_code(event);

    model = (eca_client_model_t *)lv_event_get_user_data(event);
    if (code == LV_EVENT_PRESSED && g_voice_ui_state == VOICE_UI_IDLE) {
        if (eca_client_ai_agent_voice_start() < 0) {
            lv_subject_copy_string(&model->voice_title, "语音启动失败");
            return;
        }

        g_voice_ui_state = VOICE_UI_STARTING;
        lv_subject_copy_string(&model->voice_title, "正在聆听...");
        ECA_LOGI("voice card pressed: start requested");
    } else if ((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) &&
               g_voice_ui_state == VOICE_UI_STARTING) {
        if (eca_client_ai_agent_voice_stop() < 0) {
            g_voice_ui_state = VOICE_UI_IDLE;
            lv_subject_copy_string(&model->voice_title, "语音停止失败");
            return;
        }

        g_voice_ui_state = VOICE_UI_STOPPING;
        lv_subject_copy_string(&model->voice_title, "识别中...");
        ECA_LOGI("voice card released: stop requested");
    }
}

void eca_client_voice_card_create(lv_obj_t *parent, eca_client_model_t *model)
{
    const eca_client_ui_fonts_t *fonts = eca_client_ui_fonts_get();
    lv_obj_t *card;
    lv_obj_t *circle;

    card = eca_client_ui_create_card(parent, 254, 75, 181, 80, 15);
    circle = eca_client_ui_create_circle(card, 20, 17, 54, ECA_CLIENT_COLOR_ACCENT);
    eca_client_ui_create_mic_icon(circle, -4, -6);

    eca_client_ui_create_bound_label(card, &model->voice_title, NULL,
                                     fonts->font_bold_18, ECA_CLIENT_COLOR_TEXT,
                                     88, 28);
    eca_client_ui_create_label(card, LV_SYMBOL_RIGHT, &lv_font_montserrat_28,
                               ECA_CLIENT_COLOR_MUTED, 160, 27);

    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(card, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_add_event_cb(card, voice_card_event_cb, LV_EVENT_PRESSED, model);
    lv_obj_add_event_cb(card, voice_card_event_cb, LV_EVENT_RELEASED, model);
    lv_obj_add_event_cb(card, voice_card_event_cb, LV_EVENT_PRESS_LOST, model);
    lv_timer_create(voice_status_timer_cb, 200, model);
}

static void eca_client_emergency_event_cb(lv_event_t *event)
{
    eca_client_model_t *model;

    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    model = (eca_client_model_t *)lv_event_get_user_data(event);
    eca_client_model_set_hint(model, "紧急求助已发送");
    ECA_LOGI("emergency card clicked");

    if (eca_client_ai_agent_speak_priority(
            "正在联系您的紧急联系人") < 0) {
        ECA_LOGW("emergency voice feedback failed");
    }

    if (eca_client_network_publish_emergency_alert() < 0) {
        eca_client_model_set_hint(model, "紧急求助发送失败");
        ECA_LOGW("emergency alert publish failed");
    }
}

static lv_obj_t *eca_client_action_card_create(lv_obj_t *parent, int x, int y,
                                               lv_subject_t *title)
{
    const eca_client_ui_fonts_t *fonts = eca_client_ui_fonts_get();
    lv_obj_t *card;

    card = eca_client_ui_create_card(parent, x, y, 84, 80, 11);
    eca_client_ui_create_emergency_icon(card, 20, 14);
    eca_client_ui_create_bound_label(card, title, NULL, fonts->font_14,
                                     ECA_CLIENT_COLOR_TEXT, 13, 57);
    return card;
}

void eca_client_emergency_card_create(lv_obj_t *parent, eca_client_model_t *model)
{
    lv_obj_t *card;
    lv_obj_t *hit_area;

    ECA_LOGI("emergency card create");
    card = eca_client_action_card_create(parent, 254, 167,
                                         &model->emergency_title);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card, eca_client_emergency_event_cb, LV_EVENT_CLICKED,
                        model);

    hit_area = lv_obj_create(card);
    lv_obj_set_pos(hit_area, 0, 0);
    lv_obj_set_size(hit_area, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(hit_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hit_area, 0, 0);
    lv_obj_set_style_pad_all(hit_area, 0, 0);
    lv_obj_clear_flag(hit_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(hit_area, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(hit_area, eca_client_emergency_event_cb,
                        LV_EVENT_CLICKED, model);
    lv_obj_move_foreground(hit_area);
}
