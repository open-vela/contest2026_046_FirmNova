#ifndef __VENDOR_ALLWINNERTECH_RIVOTEK_APPS_ECA_CLIENT_REGISTRATION_H
#define __VENDOR_ALLWINNERTECH_RIVOTEK_APPS_ECA_CLIENT_REGISTRATION_H

#include <stdbool.h>

#include <lvgl/lvgl.h>

#include "eca_client_model.h"

int eca_client_registration_init(lv_obj_t *screen, eca_client_model_t *model);
int eca_client_registration_show_qrcode(void);
void eca_client_registration_complete(void);
void eca_client_registration_poll(void);
bool eca_client_registration_is_registered(void);

#endif
