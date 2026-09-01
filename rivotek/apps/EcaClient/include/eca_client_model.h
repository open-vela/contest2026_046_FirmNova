#ifndef __VENDOR_ALLWINNERTECH_RIVOTEK_APPS_ECA_CLIENT_MODEL_H
#define __VENDOR_ALLWINNERTECH_RIVOTEK_APPS_ECA_CLIENT_MODEL_H

#include <stdbool.h>
#include <stdint.h>

#include <lvgl/lvgl.h>

#define ECA_CLIENT_TEXT_TIME_SIZE       16
#define ECA_CLIENT_TEXT_WEATHER_SIZE    32
#define ECA_CLIENT_TEXT_HINT_SIZE       48
#define ECA_CLIENT_TEXT_TITLE_SIZE      48
#define ECA_CLIENT_TEXT_VALUE_SIZE      16
#define ECA_CLIENT_TEXT_REMINDER_SIZE   96

typedef struct eca_client_model_s {
    lv_subject_t time;
    lv_subject_t weather;
    lv_subject_t temperature;
    lv_subject_t reminder_title;
    lv_subject_t reminder_text;
    lv_subject_t humidity;
    lv_subject_t hint;
    lv_subject_t voice_title;
    lv_subject_t emergency_title;
    lv_subject_t humidity_title;
    lv_subject_t registered;

    char time_buf[ECA_CLIENT_TEXT_TIME_SIZE];
    char weather_buf[ECA_CLIENT_TEXT_WEATHER_SIZE];
    char temperature_buf[ECA_CLIENT_TEXT_WEATHER_SIZE];
    char reminder_title_buf[ECA_CLIENT_TEXT_TITLE_SIZE];
    char reminder_text_buf[ECA_CLIENT_TEXT_REMINDER_SIZE];
    char humidity_buf[ECA_CLIENT_TEXT_VALUE_SIZE];
    char hint_buf[ECA_CLIENT_TEXT_HINT_SIZE];
    char voice_title_buf[ECA_CLIENT_TEXT_TITLE_SIZE];
    char emergency_title_buf[ECA_CLIENT_TEXT_TITLE_SIZE];
    char humidity_title_buf[ECA_CLIENT_TEXT_TITLE_SIZE];
} eca_client_model_t;

void eca_client_model_init(eca_client_model_t *model);
void eca_client_model_set_time(eca_client_model_t *model, const char *time_text);
void eca_client_model_set_temperature(eca_client_model_t *model,
                                      float temperature);
void eca_client_model_clear_temperature(eca_client_model_t *model);
void eca_client_model_set_humidity(eca_client_model_t *model, float humidity);
void eca_client_model_clear_humidity(eca_client_model_t *model);
void eca_client_model_set_reminder(eca_client_model_t *model,
                                   const char *title, const char *text);
void eca_client_model_set_hint(eca_client_model_t *model, const char *hint);
void eca_client_model_set_registered(eca_client_model_t *model, bool registered);
bool eca_client_model_is_registered(eca_client_model_t *model);

#endif
