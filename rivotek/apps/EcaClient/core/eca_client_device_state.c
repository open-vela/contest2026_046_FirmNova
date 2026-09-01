#include <nuttx/config.h>

#include <pthread.h>
#include <stdbool.h>
#include <string.h>

#include "eca_client_device_state.h"

typedef struct eca_client_device_state_s {
    pthread_mutex_t lock;
    bool initialized;
    eca_client_device_snapshot_t snapshot;
    unsigned int event_counter;
} eca_client_device_state_t;

static eca_client_device_state_t g_device_state;

static int eca_client_clamp_int(int value, int min_value, int max_value)
{
    if (value < min_value) {
        return min_value;
    }

    if (value > max_value) {
        return max_value;
    }

    return value;
}

static float eca_client_clamp_float(float value, float min_value, float max_value)
{
    if (value < min_value) {
        return min_value;
    }

    if (value > max_value) {
        return max_value;
    }

    return value;
}

int eca_client_device_state_init(void)
{
    int ret;

    if (g_device_state.initialized) {
        return 0;
    }

    memset(&g_device_state, 0, sizeof(g_device_state));
    ret = pthread_mutex_init(&g_device_state.lock, NULL);
    if (ret != 0) {
        return -ret;
    }

    g_device_state.snapshot.volume = 60;
    g_device_state.snapshot.battery = 86;
    g_device_state.snapshot.temperature = 24.6f;
    g_device_state.snapshot.humidity = 0.0f;
    g_device_state.initialized = true;
    return 0;
}

void eca_client_device_state_deinit(void)
{
    if (!g_device_state.initialized) {
        return;
    }

    pthread_mutex_destroy(&g_device_state.lock);
    memset(&g_device_state, 0, sizeof(g_device_state));
}

void eca_client_device_state_get(eca_client_device_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    pthread_mutex_lock(&g_device_state.lock);
    *snapshot = g_device_state.snapshot;
    pthread_mutex_unlock(&g_device_state.lock);
}

void eca_client_device_state_set_volume(int volume)
{
    pthread_mutex_lock(&g_device_state.lock);
    g_device_state.snapshot.volume = eca_client_clamp_int(volume, 0, 100);
    pthread_mutex_unlock(&g_device_state.lock);
}

void eca_client_device_state_set_battery(int battery)
{
    pthread_mutex_lock(&g_device_state.lock);
    g_device_state.snapshot.battery = eca_client_clamp_int(battery, 0, 100);
    pthread_mutex_unlock(&g_device_state.lock);
}

void eca_client_device_state_set_temperature(float temperature)
{
    pthread_mutex_lock(&g_device_state.lock);
    g_device_state.snapshot.temperature = eca_client_clamp_float(temperature,
                                                                 -40.0f, 85.0f);
    pthread_mutex_unlock(&g_device_state.lock);
}

void eca_client_device_state_set_humidity(float humidity)
{
    pthread_mutex_lock(&g_device_state.lock);
    g_device_state.snapshot.humidity = eca_client_clamp_float(humidity, 0.0f,
                                                              100.0f);
    pthread_mutex_unlock(&g_device_state.lock);
}

unsigned int eca_client_device_state_next_event_counter(void)
{
    unsigned int counter;

    pthread_mutex_lock(&g_device_state.lock);
    counter = ++g_device_state.event_counter;
    pthread_mutex_unlock(&g_device_state.lock);
    return counter;
}
