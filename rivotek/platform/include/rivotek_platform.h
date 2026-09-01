#ifndef __VENDOR_ALLWINNERTECH_RIVOTEK_PLATFORM_H
#define __VENDOR_ALLWINNERTECH_RIVOTEK_PLATFORM_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *fb_path;
    const char *key_device_primary;
    const char *key_device_fallback;
    const char *bt_uart_path;
    bool need_board_init;
} rivotek_platform_config_t;

const rivotek_platform_config_t *rivotek_platform_config_get(void);

int rivotek_platform_board_init(void);

void rivotek_platform_customize(rivotek_platform_config_t *config);

#ifdef __cplusplus
}
#endif

#endif
