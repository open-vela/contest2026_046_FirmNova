#ifndef __VENDOR_ALLWINNERTECH_RIVOTEK_APPS_ECA_CLIENT_PLAN_H
#define __VENDOR_ALLWINNERTECH_RIVOTEK_APPS_ECA_CLIENT_PLAN_H

#include "eca_client_remote_broadcast.h"

typedef struct eca_client_plan_s {
    char command_id[ECA_CLIENT_REMOTE_BROADCAST_COMMAND_ID_MAX];
    char title[ECA_CLIENT_REMOTE_BROADCAST_TITLE_MAX];
    char text[ECA_CLIENT_REMOTE_BROADCAST_TEXT_MAX];
    char source[ECA_CLIENT_REMOTE_BROADCAST_SOURCE_MAX];
    char payload_json[ECA_CLIENT_REMOTE_BROADCAST_PAYLOAD_JSON_MAX];
    long long received_at_ms;
} eca_client_plan_t;

typedef int (*eca_client_plan_handler_t)(const eca_client_plan_t *plan,
                                         void *arg);

int eca_client_plan_submit(const eca_client_plan_t *plan);
int eca_client_plan_set_handler(eca_client_plan_handler_t handler, void *arg);
int eca_client_plan_pop(eca_client_plan_t *plan);

#endif
