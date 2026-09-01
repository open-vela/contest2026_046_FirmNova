#ifndef __VENDOR_ALLWINNERTECH_RIVOTEK_APPS_ECA_CLIENT_DEVICE_STATE_H
#define __VENDOR_ALLWINNERTECH_RIVOTEK_APPS_ECA_CLIENT_DEVICE_STATE_H

#include <stdint.h>

typedef struct eca_client_device_snapshot_s {
    int volume;
    int battery;
    float temperature;
    float humidity;
} eca_client_device_snapshot_t;

int eca_client_device_state_init(void);
void eca_client_device_state_deinit(void);
void eca_client_device_state_get(eca_client_device_snapshot_t *snapshot);
void eca_client_device_state_set_volume(int volume);
void eca_client_device_state_set_battery(int battery);
void eca_client_device_state_set_temperature(float temperature);
void eca_client_device_state_set_humidity(float humidity);
unsigned int eca_client_device_state_next_event_counter(void);

#endif
