#ifndef __VENDOR_ALLWINNERTECH_RIVOTEK_SCOOTERDEMO_DRIVER_DEMO_H
#define __VENDOR_ALLWINNERTECH_RIVOTEK_SCOOTERDEMO_DRIVER_DEMO_H

#include <stdbool.h>
#include <stdint.h>

#include "buttonProcessor.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DRIVER_DEMO_EFFECT_NONE = 0,
    DRIVER_DEMO_EFFECT_ACCEL,
    DRIVER_DEMO_EFFECT_BRAKE,
} driver_demo_effect_t;

typedef struct {
    int speed;
    char gear[2];
    driver_demo_effect_t effect;
} driver_demo_output_t;

void driver_demo_init(void);
bool driver_demo_is_running(void);
bool driver_demo_handle_button(KEYCODE keycode,
                               BUTTON_EVENT event,
                               bool home_page_active,
                               uint32_t tick_ms);
bool driver_demo_update(driver_demo_output_t *output, uint32_t tick_ms);

#ifdef __cplusplus
}
#endif

#endif