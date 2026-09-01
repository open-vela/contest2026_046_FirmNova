#ifndef __VENDOR_ALLWINNERTECH_RIVOTEK_APPS_ECA_CLIENT_TIME_OBSERVER_H
#define __VENDOR_ALLWINNERTECH_RIVOTEK_APPS_ECA_CLIENT_TIME_OBSERVER_H

#include "eca_client_model.h"

int eca_client_time_observer_init(eca_client_model_t *model);
void eca_client_time_observer_poll(void);

#endif
