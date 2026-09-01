#include "buttonProcessor.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#define BUTTON_PROCESSOR_MAX_HANDLERS 8
#define BUTTON_PROCESSOR_DEBOUNCE_MS  30U

typedef struct {
    bool is_pressed;
    bool long_reported;
    bool wait_single;
    uint32_t press_tick;
    uint32_t release_tick;
} button_key_state_t;

typedef struct {
    button_event_handler_t handler;
    void *user_data;
} button_handler_slot_t;

static uint32_t g_long_press_ms = BUTTON_PROCESSOR_DEFAULT_LONG_PRESS_MS;
static uint32_t g_double_click_ms = BUTTON_PROCESSOR_DEFAULT_DOUBLE_CLICK_MS;
static uint32_t g_double_click_mask = BUTTON_PROCESSOR_KEY_MASK_OK;
static KEYCODE g_active_pressed_key = KEYCODE_NONE;

static button_key_state_t g_key_states[3]; /* UP/DOWN/OK */
static button_handler_slot_t g_handlers[BUTTON_PROCESSOR_MAX_HANDLERS];

static bool is_release_raw_value(btn_buttonset_t raw_val)
{
    return raw_val == 0U || raw_val >= (btn_buttonset_t)50;
}

static uint32_t keycode_to_mask(KEYCODE keycode)
{
    switch (keycode) {
    case KEYCODE_UP:
        return BUTTON_PROCESSOR_KEY_MASK_UP;
    case KEYCODE_DOWN:
        return BUTTON_PROCESSOR_KEY_MASK_DOWN;
    case KEYCODE_OK:
        return BUTTON_PROCESSOR_KEY_MASK_OK;
    default:
        return 0U;
    }
}

static bool keycode_supports_double_click(KEYCODE keycode)
{
    return (g_double_click_mask & keycode_to_mask(keycode)) != 0U;
}

static int keycode_to_index(KEYCODE keycode)
{
    switch (keycode) {
    case KEYCODE_UP:
        return 0;
    case KEYCODE_DOWN:
        return 1;
    case KEYCODE_OK:
        return 2;
    default:
        return -1;
    }
}

static KEYCODE raw_to_keycode(btn_buttonset_t raw_val)
{
    if (raw_val == BUTTON_PROCESSOR_KEY_MASK_DOWN) {
        return KEYCODE_UP;
    }

    if (raw_val == BUTTON_PROCESSOR_KEY_MASK_UP) {
        return KEYCODE_DOWN;
    }

    if (raw_val == BUTTON_PROCESSOR_KEY_MASK_OK) {
        return KEYCODE_OK;
    }

    if (raw_val == 9 || raw_val == 34 || raw_val == 1) {
        return KEYCODE_UP;
    }

    if (raw_val == 18 || raw_val == 2) {
        return KEYCODE_DOWN;
    }

    if (raw_val == 40 || raw_val == 8 || raw_val == 16) {
        return KEYCODE_OK;
    }

    return KEYCODE_NONE;
}

static void emit_event(KEYCODE keycode, BUTTON_EVENT event)
{
    int i;
    printf("buttonProcessor: emit_event keycode=%d, event=%d\n", keycode, event);

    for (i = 0; i < BUTTON_PROCESSOR_MAX_HANDLERS; i++) {
        if (g_handlers[i].handler != NULL) {
            g_handlers[i].handler(keycode, event, g_handlers[i].user_data);
        }
    }
}

static void process_press(KEYCODE keycode, uint32_t tick_ms)
{
    int index = keycode_to_index(keycode);
    button_key_state_t *state;

    if (index < 0) {
        return;
    }

    state = &g_key_states[index];

    /* 如果当前已有按键按下，且新按键不同，说明是ADC采样噪声或者是无效的组合按键。
     * 直接忽略新按键，保持原有按键状态，直到松开。
     * 这样可以防止 UP/DOWN 过程中的中间阻值误触发 OK 按键。 */
    if (g_active_pressed_key != KEYCODE_NONE && g_active_pressed_key != keycode) {
        return;
    }

    if (!state->is_pressed && state->release_tick != 0U &&
        (tick_ms - state->release_tick) < BUTTON_PROCESSOR_DEBOUNCE_MS) {
        return;
    }

    if (state->is_pressed) {
        return;
    }

    state->is_pressed = true;
    state->long_reported = false;
    state->press_tick = tick_ms;
    g_active_pressed_key = keycode;
}

static void process_release(uint32_t tick_ms)
{
    uint32_t pressed_ms;
    int index;
    button_key_state_t *state;
    KEYCODE keycode = g_active_pressed_key;

    if (keycode == KEYCODE_NONE) {
        return;
    }

    index = keycode_to_index(keycode);
    if (index < 0) {
        g_active_pressed_key = KEYCODE_NONE;
        return;
    }

    state = &g_key_states[index];
    if (!state->is_pressed) {
        g_active_pressed_key = KEYCODE_NONE;
        return;
    }

    state->is_pressed = false;
    g_active_pressed_key = KEYCODE_NONE;
    pressed_ms = tick_ms - state->press_tick;

    if (state->long_reported) {
        state->wait_single = false;
        return;
    }

    if (pressed_ms >= g_long_press_ms) {
        state->long_reported = true;
        state->wait_single = false;
        emit_event(keycode, BUTTON_EVENT_LONG_PRESS);
        return;
    }

    if (!keycode_supports_double_click(keycode)) {
        state->wait_single = false;
        state->release_tick = tick_ms;
        emit_event(keycode, BUTTON_EVENT_SHORT_PRESS);
        return;
    }

    if (state->wait_single && (tick_ms - state->release_tick <= g_double_click_ms)) {
        state->wait_single = false;
        emit_event(keycode, BUTTON_EVENT_DOUBLE_CLICK);
    } else {
        state->wait_single = true;
        state->release_tick = tick_ms;
    }
}

static void process_hold_and_pending(uint32_t tick_ms)
{
    int i;

    for (i = 0; i < 3; i++) {
        KEYCODE keycode;
        button_key_state_t *state = &g_key_states[i];

        keycode = (i == 0) ? KEYCODE_UP : ((i == 1) ? KEYCODE_DOWN : KEYCODE_OK);

        if (state->is_pressed && !state->long_reported &&
            (tick_ms - state->press_tick >= g_long_press_ms)) {
            state->long_reported = true;
            state->wait_single = false;
            emit_event(keycode, BUTTON_EVENT_LONG_PRESS);
        }

        if (state->wait_single && (tick_ms - state->release_tick > g_double_click_ms)) {
            state->wait_single = false;
            emit_event(keycode, BUTTON_EVENT_SHORT_PRESS);
        }
    }
}

void button_processor_init(uint32_t long_press_ms, uint32_t double_click_ms)
{
    button_processor_set_thresholds(long_press_ms, double_click_ms);
    button_processor_reset();
}

void button_processor_reset(void)
{
    int i;

    g_active_pressed_key = KEYCODE_NONE;

    for (i = 0; i < 3; i++) {
        g_key_states[i].is_pressed = false;
        g_key_states[i].long_reported = false;
        g_key_states[i].wait_single = false;
        g_key_states[i].press_tick = 0;
        g_key_states[i].release_tick = 0;
    }
}

void button_processor_set_thresholds(uint32_t long_press_ms, uint32_t double_click_ms)
{
    g_long_press_ms = (long_press_ms == 0U) ? BUTTON_PROCESSOR_DEFAULT_LONG_PRESS_MS : long_press_ms;
    g_double_click_ms = (double_click_ms == 0U) ? BUTTON_PROCESSOR_DEFAULT_DOUBLE_CLICK_MS : double_click_ms;
}

void button_processor_set_double_click_mask(uint32_t key_mask)
{
    g_double_click_mask = key_mask;
}

int button_processor_register_event_handler(button_event_handler_t handler, void *user_data)
{
    int i;

    if (handler == NULL) {
        return -1;
    }

    for (i = 0; i < BUTTON_PROCESSOR_MAX_HANDLERS; i++) {
        if (g_handlers[i].handler == handler && g_handlers[i].user_data == user_data) {
            return 0;
        }
    }

    for (i = 0; i < BUTTON_PROCESSOR_MAX_HANDLERS; i++) {
        if (g_handlers[i].handler == NULL) {
            g_handlers[i].handler = handler;
            g_handlers[i].user_data = user_data;
            return 0;
        }
    }

    return -1;
}

int button_processor_unregister_event_handler(button_event_handler_t handler, void *user_data)
{
    int i;

    for (i = 0; i < BUTTON_PROCESSOR_MAX_HANDLERS; i++) {
        if (g_handlers[i].handler == handler && g_handlers[i].user_data == user_data) {
            g_handlers[i].handler = NULL;
            g_handlers[i].user_data = NULL;
            return 0;
        }
    }

    return -1;
}

void button_processor_process_raw_value(btn_buttonset_t raw_val, uint32_t tick_ms)
{
    KEYCODE keycode;

    process_hold_and_pending(tick_ms);

    if (is_release_raw_value(raw_val)) {
        process_release(tick_ms);
        return;
    }

    keycode = raw_to_keycode(raw_val);
    if (keycode != KEYCODE_NONE) {
        process_press(keycode, tick_ms);
    }
}

void button_processor_flush(uint32_t tick_ms)
{
    process_hold_and_pending(tick_ms);
}
