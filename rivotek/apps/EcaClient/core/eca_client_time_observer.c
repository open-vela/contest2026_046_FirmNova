#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <lvgl/lvgl.h>

#include "eca_client_log.h"
#include "eca_client_time_observer.h"
#include "time_impl.h"

typedef struct eca_client_time_observer_state_s {
    eca_client_model_t *model;
    lv_observer_t *registered_observer;
    bool initialized;
    bool time_sync_enabled;
    bool time_service_started;
    bool time_ready_logged;
} eca_client_time_observer_state_t;

static eca_client_time_observer_state_t g_time_observer;

static void eca_client_time_registered_observer_cb(lv_observer_t *observer,
                                                   lv_subject_t *subject)
{
    (void)observer;

    g_time_observer.time_sync_enabled = lv_subject_get_int(subject) != 0;
}

int eca_client_time_observer_init(eca_client_model_t *model)
{
    if (model == NULL) {
        return -EINVAL;
    }

    memset(&g_time_observer, 0, sizeof(g_time_observer));
    g_time_observer.model = model;
    g_time_observer.initialized = true;
    eca_client_model_set_time(model, "--:--");
    g_time_observer.registered_observer =
        lv_subject_add_observer(&model->registered,
                                eca_client_time_registered_observer_cb,
                                NULL);
    if (g_time_observer.registered_observer == NULL) {
        memset(&g_time_observer, 0, sizeof(g_time_observer));
        return -ENOMEM;
    }

    return 0;
}

void eca_client_time_observer_poll(void)
{
    const char *time_text;
    uint32_t tick;

    if (!g_time_observer.initialized || g_time_observer.model == NULL) {
        return;
    }

    if (!g_time_observer.time_sync_enabled) {
        return;
    }

    if (!g_time_observer.time_service_started) {
        rivotek_time_service_init();
        g_time_observer.time_service_started = true;
        ECA_LOGI("time observer started local time sync");
    }

    tick = lv_tick_get();
    rivotek_time_service_update(tick);

    time_text = rivotek_time_service_get_text();
    if (time_text != NULL) {
        eca_client_model_set_time(g_time_observer.model, time_text);
    }

    if (!g_time_observer.time_ready_logged &&
        rivotek_time_service_is_ready()) {
        g_time_observer.time_ready_logged = true;
        ECA_LOGI("local time ready: %s", rivotek_time_service_get_text());
    }
}
