#ifndef __VENDOR_ALLWINNERTECH_RIVOTEK_SCOOTERDEMO_KEY_PROCESSOR_H
#define __VENDOR_ALLWINNERTECH_RIVOTEK_SCOOTERDEMO_KEY_PROCESSOR_H

#include <stdint.h>
#include "buttonProcessor.h"

#ifdef __cplusplus
extern "C" {
#endif

void key_processor_init(void);
void key_processor_deinit(void);
void key_processor_set_thresholds(uint32_t long_press_ms, uint32_t double_click_ms);
void key_processor_set_double_click_mask(uint32_t key_mask);

int key_processor_register_event_handler(button_event_handler_t handler, void *user_data);
int key_processor_unregister_event_handler(button_event_handler_t handler, void *user_data);

#ifdef __cplusplus
}
#endif

#endif
