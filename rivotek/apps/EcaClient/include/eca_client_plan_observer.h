#ifndef __VENDOR_ALLWINNERTECH_RIVOTEK_APPS_ECA_CLIENT_PLAN_OBSERVER_H
#define __VENDOR_ALLWINNERTECH_RIVOTEK_APPS_ECA_CLIENT_PLAN_OBSERVER_H

#include <stddef.h>

#include "eca_client_model.h"

typedef int (*eca_client_plan_speak_cb_t)(const char *text, void *arg);

int eca_client_plan_observer_init(eca_client_model_t *model);
void eca_client_plan_observer_poll(void);
int eca_client_plan_observer_get_latest(char *buffer, size_t buffer_size);
int eca_client_plan_observer_set_speaker(eca_client_plan_speak_cb_t cb,
                                         void *arg);
int eca_client_plan_observer_speak_latest(void);

#endif
