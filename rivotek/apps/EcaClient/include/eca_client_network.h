#ifndef __VENDOR_ALLWINNERTECH_RIVOTEK_APPS_ECA_CLIENT_NETWORK_H
#define __VENDOR_ALLWINNERTECH_RIVOTEK_APPS_ECA_CLIENT_NETWORK_H

#include <stdbool.h>

#include "eca_client_model.h"

typedef struct eca_client_registration_info_s {
    char device_id[32];
    char device_name[64];
    char claim_token[64];
    char http_base_url[128];
} eca_client_registration_info_t;

typedef struct eca_client_registration_status_s {
    char device_id[32];
    bool registered;
    bool bound;
    bool bound_to_current_user;
    bool available_for_claim;
    char claim_status[16];
} eca_client_registration_status_t;

int eca_client_network_start(eca_client_model_t *model);
void eca_client_network_stop(void);
int eca_client_network_get_registration_info(
    eca_client_registration_info_t *info);
int eca_client_network_get_registration_status(
    eca_client_registration_status_t *status);
void eca_client_network_notify_wifi_ready(void);
int eca_client_network_publish_voice_keyword(const char *keyword,
                                             int confidence_percent,
                                             const char *source);
int eca_client_network_publish_emergency_alert(void);

/* ── MQTT request-response ──────────────────────────────────────── */

#define ECA_CLIENT_REQUEST_ID_MAX    64
#define ECA_CLIENT_REQUEST_TIMEOUT_S 5
#define ECA_CLIENT_PENDING_REQUESTS  4
#define ECA_CLIENT_RESPONSE_DATA_MAX 768

typedef struct eca_client_mqtt_response_s {
    char request_id[ECA_CLIENT_REQUEST_ID_MAX];
    char status[16];
    char data[ECA_CLIENT_RESPONSE_DATA_MAX];
    long long received_at_ms;
} eca_client_mqtt_response_t;

typedef void (*eca_client_mqtt_response_cb_t)(
    const eca_client_mqtt_response_t *response, void *arg);

/**
 * Send an MQTT request to the server and block up to ``timeout_s``
 * seconds for the correlated response.
 *
 * Returns 0 on success, -ETIMEDOUT if the server didn't respond,
 * or another negative errno on transport errors.
 */
int eca_client_mqtt_request(const char *query_type,
                            const char *extra_json,
                            int timeout_s,
                            eca_client_mqtt_response_t *response_out);

int eca_client_mqtt_request_plans(eca_client_mqtt_response_t *resp,
                                  int timeout_s);
int eca_client_mqtt_request_alerts(eca_client_mqtt_response_t *resp,
                                   int timeout_s);
int eca_client_mqtt_request_config(eca_client_mqtt_response_t *resp,
                                   int timeout_s);
int eca_client_mqtt_request_broadcast_templates(
    eca_client_mqtt_response_t *resp, int timeout_s);

#endif
