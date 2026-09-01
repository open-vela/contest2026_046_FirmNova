#include "key_processor.h"

#include <stdbool.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include <lvgl/lvgl.h>
#include "rivotek_platform.h"

#define KEY_POLL_INTERVAL_MS  20U
#define KEY_MAX_READ_PER_POLL 10
#define KEY_RELEASE_RAW_VALUE ((btn_buttonset_t)0)

static int g_sidebar_btn_fd = -1;
static lv_timer_t *g_key_poll_timer;
static btn_buttonset_t g_last_raw = KEY_RELEASE_RAW_VALUE;
static const char *g_key_device_path = NULL;

static void sidebar_button_poll_cb(lv_timer_t *timer)
{
    btn_buttonset_t val;
    int count = 0;
    uint32_t now = lv_tick_get();
    (void)timer;

    if (g_sidebar_btn_fd < 0) {
        return;
    }

    while (count < KEY_MAX_READ_PER_POLL) {
        ssize_t read_size = read(g_sidebar_btn_fd, &val, sizeof(val));
        if (read_size != (ssize_t)sizeof(val)) {
            break;
        }

        now = lv_tick_get();
        if (val != g_last_raw) {
            printf("key_processor: raw=%u from %s\n", (unsigned int)val,
                   g_key_device_path != NULL ? g_key_device_path : "unknown");
            button_processor_process_raw_value(val, now);
            g_last_raw = val;
        }

        count++;
    }

    button_processor_flush(lv_tick_get());
}

void key_processor_init(void)
{
    const rivotek_platform_config_t *platform_config;
    const char *primary_path;
    const char *fallback_path;

    if (g_sidebar_btn_fd >= 0) {
        return;
    }

    platform_config = rivotek_platform_config_get();
    primary_path = platform_config->key_device_primary;
    fallback_path = platform_config->key_device_fallback;

    button_processor_init(BUTTON_PROCESSOR_DEFAULT_LONG_PRESS_MS,
                          BUTTON_PROCESSOR_DEFAULT_DOUBLE_CLICK_MS);

    g_sidebar_btn_fd = -1;

    if (primary_path != NULL && primary_path[0] != '\0') {
        g_sidebar_btn_fd = open(primary_path, O_RDONLY | O_NONBLOCK);
    }

    if (g_sidebar_btn_fd >= 0) {
        g_key_device_path = primary_path;
    } else if (fallback_path != NULL && fallback_path[0] != '\0') {
        g_sidebar_btn_fd = open(fallback_path, O_RDONLY | O_NONBLOCK);
        if (g_sidebar_btn_fd >= 0) {
            g_key_device_path = fallback_path;
        }
    }

    if (g_sidebar_btn_fd < 0) {
        printf("failed to open %s and %s: %d\n",
               primary_path != NULL ? primary_path : "(null)",
               fallback_path != NULL ? fallback_path : "(null)",
               errno);
        return;
    }

    printf("key_processor: using device %s\n", g_key_device_path);
    g_last_raw = KEY_RELEASE_RAW_VALUE;
    g_key_poll_timer = lv_timer_create(sidebar_button_poll_cb, KEY_POLL_INTERVAL_MS, NULL);
}

void key_processor_deinit(void)
{
    if (g_key_poll_timer != NULL) {
        lv_timer_del(g_key_poll_timer);
        g_key_poll_timer = NULL;
    }

    if (g_sidebar_btn_fd >= 0) {
        close(g_sidebar_btn_fd);
        g_sidebar_btn_fd = -1;
    }

    g_key_device_path = NULL;
    g_last_raw = KEY_RELEASE_RAW_VALUE;
    button_processor_reset();
}

void key_processor_set_thresholds(uint32_t long_press_ms, uint32_t double_click_ms)
{
    button_processor_set_thresholds(long_press_ms, double_click_ms);
}

void key_processor_set_double_click_mask(uint32_t key_mask)
{
    button_processor_set_double_click_mask(key_mask);
}

int key_processor_register_event_handler(button_event_handler_t handler, void *user_data)
{
    return button_processor_register_event_handler(handler, user_data);
}

int key_processor_unregister_event_handler(button_event_handler_t handler, void *user_data)
{
    return button_processor_unregister_event_handler(handler, user_data);
}

