#ifndef __VENDOR_ALLWINNERTECH_RIVOTEK_APPS_ECA_CLIENT_HUMIDITY_SENSOR_H
#define __VENDOR_ALLWINNERTECH_RIVOTEK_APPS_ECA_CLIENT_HUMIDITY_SENSOR_H

#include "eca_client_model.h"

int eca_client_humidity_sensor_init(eca_client_model_t *model);
int eca_client_humidity_sensor_update(eca_client_model_t *model, int timeout_ms);
void eca_client_humidity_sensor_deinit(void);

#endif
