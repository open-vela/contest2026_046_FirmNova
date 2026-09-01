#ifndef __VENDOR_ALLWINNERTECH_RIVOTEK_APPS_ECA_CLIENT_COMMAND_DISPATCHER_H
#define __VENDOR_ALLWINNERTECH_RIVOTEK_APPS_ECA_CLIENT_COMMAND_DISPATCHER_H

typedef void (*eca_client_command_ack_cb_t)(const char *command_id,
                                            const char *status,
                                            const char *error,
                                            void *arg);

typedef void (*eca_client_command_event_cb_t)(const char *event,
                                              void *arg);

typedef struct eca_client_command_dispatcher_observer_s {
    eca_client_command_event_cb_t event_cb;
    void *event_arg;
} eca_client_command_dispatcher_observer_t;

void eca_client_command_dispatcher_set_observer(
    const eca_client_command_dispatcher_observer_t *observer);

int eca_client_command_dispatcher_start(const char *device_id,
                                        eca_client_command_ack_cb_t ack_cb,
                                        void *ack_arg);
void eca_client_command_dispatcher_stop(void);
int eca_client_command_dispatcher_update_device_id(const char *device_id);
int eca_client_command_dispatcher_enqueue(const char *payload);

#endif
