#include <nuttx/config.h>

#include <stdbool.h>

#include "rivotek_platform.h"

#if defined(NEED_BOARDINIT)
#include <sys/boardctl.h>
#endif

#ifndef CONFIG_RIVOTEK_PLATFORM_FB_PATH
#define CONFIG_RIVOTEK_PLATFORM_FB_PATH "/dev/lcd0"
#endif

#ifndef CONFIG_RIVOTEK_PLATFORM_KEY_DEVICE_PRIMARY
#define CONFIG_RIVOTEK_PLATFORM_KEY_DEVICE_PRIMARY "/dev/buttons"
#endif

#ifndef CONFIG_RIVOTEK_PLATFORM_KEY_DEVICE_FALLBACK
#define CONFIG_RIVOTEK_PLATFORM_KEY_DEVICE_FALLBACK "/dev/input/event1"
#endif

#ifndef CONFIG_RIVOTEK_PLATFORM_BT_UART_PATH
#define CONFIG_RIVOTEK_PLATFORM_BT_UART_PATH "/dev/uart1"
#endif

#ifndef CONFIG_RIVOTEK_PLATFORM_NEED_BOARDINIT
#define CONFIG_RIVOTEK_PLATFORM_NEED_BOARDINIT 1
#endif

void __attribute__((weak)) rivotek_platform_customize(rivotek_platform_config_t *config)
{
    (void)config;
}

const rivotek_platform_config_t *rivotek_platform_config_get(void)
{
    static rivotek_platform_config_t config = {
        .fb_path = CONFIG_RIVOTEK_PLATFORM_FB_PATH,
        .key_device_primary = CONFIG_RIVOTEK_PLATFORM_KEY_DEVICE_PRIMARY,
        .key_device_fallback = CONFIG_RIVOTEK_PLATFORM_KEY_DEVICE_FALLBACK,
        .bt_uart_path = CONFIG_RIVOTEK_PLATFORM_BT_UART_PATH,
        .need_board_init = (CONFIG_RIVOTEK_PLATFORM_NEED_BOARDINIT != 0),
    };
    static bool initialized;

    if (!initialized) {
        rivotek_platform_customize(&config);
        initialized = true;
    }

    return &config;
}

int rivotek_platform_board_init(void)
{
#if defined(NEED_BOARDINIT)
    const rivotek_platform_config_t *config = rivotek_platform_config_get();

    if (config->need_board_init) {
        return boardctl(BOARDIOC_INIT, 0);
    }
#endif

    return 0;
}
