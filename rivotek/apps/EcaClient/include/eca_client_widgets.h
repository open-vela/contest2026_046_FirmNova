#ifndef __VENDOR_ALLWINNERTECH_RIVOTEK_APPS_ECA_CLIENT_WIDGETS_H
#define __VENDOR_ALLWINNERTECH_RIVOTEK_APPS_ECA_CLIENT_WIDGETS_H

#include <lvgl/lvgl.h>

#include "eca_client_model.h"

void eca_client_header_create(lv_obj_t *parent, eca_client_model_t *model);
void eca_client_health_card_create(lv_obj_t *parent, eca_client_model_t *model);
void eca_client_voice_card_create(lv_obj_t *parent, eca_client_model_t *model);
void eca_client_emergency_card_create(lv_obj_t *parent, eca_client_model_t *model);
void eca_client_humidity_card_create(lv_obj_t *parent, eca_client_model_t *model);

#endif
