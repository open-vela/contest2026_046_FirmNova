#include <nuttx/config.h>

#include <stdio.h>
#include <string.h>

#include "eca_client_model.h"

void eca_client_model_set_time(eca_client_model_t *model, const char *time_text)
{
    if (model == NULL || time_text == NULL) {
        return;
    }

    if (strcmp(model->time_buf, time_text) == 0) {
        return;
    }

    lv_subject_copy_string(&model->time, time_text);
}

void eca_client_model_clear_humidity(eca_client_model_t *model)
{
    lv_subject_copy_string(&model->humidity, "--%");
}

void eca_client_model_clear_temperature(eca_client_model_t *model)
{
    lv_subject_copy_string(&model->temperature, "温度: --°C");
}

void eca_client_model_set_temperature(eca_client_model_t *model,
                                      float temperature)
{
    char buf[ECA_CLIENT_TEXT_WEATHER_SIZE];

    if (temperature < -40.0f) {
        temperature = -40.0f;
    } else if (temperature > 85.0f) {
        temperature = 85.0f;
    }

    snprintf(buf, sizeof(buf), "温度: %.1f°C", (double)temperature);
    lv_subject_copy_string(&model->temperature, buf);
}

void eca_client_model_set_humidity(eca_client_model_t *model, float humidity)
{
    char buf[ECA_CLIENT_TEXT_VALUE_SIZE];
    int humidity_x10;

    if (humidity < 0.0f) {
        humidity = 0.0f;
    } else if (humidity > 100.0f) {
        humidity = 100.0f;
    }

    humidity_x10 = (int)(humidity * 10.0f + 0.5f);
    snprintf(buf, sizeof(buf), "%d.%d%%", humidity_x10 / 10,
             humidity_x10 % 10);
    lv_subject_copy_string(&model->humidity, buf);
}

void eca_client_model_set_reminder(eca_client_model_t *model,
                                   const char *title, const char *text)
{
    if (model == NULL) {
        return;
    }

    lv_subject_copy_string(&model->reminder_title,
                           title != NULL && title[0] != '\0' ?
                           title : "最近提醒");
    lv_subject_copy_string(&model->reminder_text,
                           text != NULL && text[0] != '\0' ?
                           text : "暂无提醒计划");
}

void eca_client_model_set_hint(eca_client_model_t *model, const char *hint)
{
    if (model == NULL) {
        return;
    }

    lv_subject_copy_string(&model->hint,
                           hint != NULL && hint[0] != '\0' ?
                           hint : "轻按说话唤醒助手");
}

void eca_client_model_set_registered(eca_client_model_t *model, bool registered)
{
    if (model == NULL) {
        return;
    }

    lv_subject_set_int(&model->registered, registered ? 1 : 0);
}

bool eca_client_model_is_registered(eca_client_model_t *model)
{
    if (model == NULL) {
        return false;
    }

    return lv_subject_get_int(&model->registered) != 0;
}

void eca_client_model_init(eca_client_model_t *model)
{
    lv_memzero(model, sizeof(*model));

    lv_subject_init_string(&model->time, model->time_buf, NULL,
                           sizeof(model->time_buf), "--:--");
    lv_subject_init_string(&model->weather, model->weather_buf, NULL,
                           sizeof(model->weather_buf), "天气: --");
    lv_subject_init_string(&model->temperature, model->temperature_buf, NULL,
                           sizeof(model->temperature_buf), "温度: --°C");
    lv_subject_init_string(&model->reminder_title, model->reminder_title_buf,
                           NULL, sizeof(model->reminder_title_buf), "最近提醒");
    lv_subject_init_string(&model->reminder_text, model->reminder_text_buf,
                           NULL, sizeof(model->reminder_text_buf),
                           "暂无提醒计划");
    lv_subject_init_string(&model->humidity, model->humidity_buf, NULL,
                           sizeof(model->humidity_buf), "--%");
    lv_subject_init_string(&model->hint, model->hint_buf, NULL,
                           sizeof(model->hint_buf), "轻按说话唤醒助手");
    lv_subject_init_string(&model->voice_title, model->voice_title_buf, NULL,
                           sizeof(model->voice_title_buf), "语音助手");
    lv_subject_init_string(&model->emergency_title, model->emergency_title_buf,
                           NULL, sizeof(model->emergency_title_buf), "紧急求助");
    lv_subject_init_string(&model->humidity_title, model->humidity_title_buf,
                           NULL, sizeof(model->humidity_title_buf), "湿度");
    lv_subject_init_int(&model->registered, 0);
}
