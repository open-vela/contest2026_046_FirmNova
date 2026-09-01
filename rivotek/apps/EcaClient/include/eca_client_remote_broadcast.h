#ifndef __VENDOR_ALLWINNERTECH_RIVOTEK_APPS_ECA_CLIENT_REMOTE_BROADCAST_H
#define __VENDOR_ALLWINNERTECH_RIVOTEK_APPS_ECA_CLIENT_REMOTE_BROADCAST_H

#include <stddef.h>

#define ECA_CLIENT_REMOTE_BROADCAST_KIND_MAX      32
#define ECA_CLIENT_REMOTE_BROADCAST_TITLE_MAX     128
#define ECA_CLIENT_REMOTE_BROADCAST_TEXT_MAX      256
#define ECA_CLIENT_REMOTE_BROADCAST_AUDIO_URL_MAX 256
#define ECA_CLIENT_REMOTE_BROADCAST_SOURCE_MAX    32
#define ECA_CLIENT_REMOTE_BROADCAST_PAYLOAD_JSON_MAX 768
#define ECA_CLIENT_REMOTE_BROADCAST_COMMAND_ID_MAX 64
#define ECA_CLIENT_REMOTE_BROADCAST_QUEUE_SIZE    16

typedef struct eca_client_remote_broadcast_s {
    char command_id[ECA_CLIENT_REMOTE_BROADCAST_COMMAND_ID_MAX];
    char kind[ECA_CLIENT_REMOTE_BROADCAST_KIND_MAX];
    char title[ECA_CLIENT_REMOTE_BROADCAST_TITLE_MAX];
    char text[ECA_CLIENT_REMOTE_BROADCAST_TEXT_MAX];
    char audio_url[ECA_CLIENT_REMOTE_BROADCAST_AUDIO_URL_MAX];
    char source[ECA_CLIENT_REMOTE_BROADCAST_SOURCE_MAX];
    char payload_json[ECA_CLIENT_REMOTE_BROADCAST_PAYLOAD_JSON_MAX];
    long long received_at_ms;
} eca_client_remote_broadcast_t;

typedef int (*eca_client_remote_broadcast_handler_t)(
    const eca_client_remote_broadcast_t *broadcast, void *arg);

int eca_client_remote_broadcast_submit(
    const eca_client_remote_broadcast_t *broadcast);
int eca_client_remote_broadcast_set_handler(
    eca_client_remote_broadcast_handler_t handler, void *arg);
int eca_client_remote_broadcast_pop(
    eca_client_remote_broadcast_t *broadcast);

#endif
