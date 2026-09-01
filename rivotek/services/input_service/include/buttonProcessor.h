#ifndef __VENDOR_ALLWINNERTECH_RIVOTEK_SCOOTERDEMO_BUTTONPROCESSOR_H
#define __VENDOR_ALLWINNERTECH_RIVOTEK_SCOOTERDEMO_BUTTONPROCESSOR_H

#include <stdint.h>
#include <nuttx/input/buttons.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BUTTON_PROCESSOR_DEFAULT_LONG_PRESS_MS   2000U
#define BUTTON_PROCESSOR_DEFAULT_DOUBLE_CLICK_MS 400U
#define BUTTON_PROCESSOR_KEY_MASK_UP             (1U << 0)
#define BUTTON_PROCESSOR_KEY_MASK_DOWN           (1U << 1)
#define BUTTON_PROCESSOR_KEY_MASK_OK             (1U << 2)

typedef enum {
    KEYCODE_NONE = 0,
    KEYCODE_UP,
    KEYCODE_DOWN,
    KEYCODE_OK,
} KEYCODE;

typedef enum {
    BUTTON_EVENT_SHORT_PRESS = 0,
    BUTTON_EVENT_LONG_PRESS,
    BUTTON_EVENT_DOUBLE_CLICK,
} BUTTON_EVENT;

typedef void (*button_event_handler_t)(KEYCODE keycode, BUTTON_EVENT event, void *user_data);

void button_processor_init(uint32_t long_press_ms, uint32_t double_click_ms);
void button_processor_reset(void);
void button_processor_set_thresholds(uint32_t long_press_ms, uint32_t double_click_ms);
void button_processor_set_double_click_mask(uint32_t key_mask);

int button_processor_register_event_handler(button_event_handler_t handler, void *user_data);
int button_processor_unregister_event_handler(button_event_handler_t handler, void *user_data);

void button_processor_process_raw_value(btn_buttonset_t raw_val, uint32_t tick_ms);
void button_processor_flush(uint32_t tick_ms);

#ifdef __cplusplus
}
#endif

#endif