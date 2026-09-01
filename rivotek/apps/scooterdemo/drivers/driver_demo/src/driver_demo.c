#include "driver_demo.h"

#include <string.h>

#define DRIVER_DEMO_STAGE_RAMP_TO_10_MS     3000U
#define DRIVER_DEMO_STAGE_ACCEL_TO_25_MS    1500U
#define DRIVER_DEMO_STAGE_CRUISE_25_MS     10000U
#define DRIVER_DEMO_STAGE_BRAKE_TO_10_MS    3000U
#define DRIVER_DEMO_STAGE_HOLD_10_MS        3000U
#define DRIVER_DEMO_STAGE_BRAKE_TO_0_MS     3000U
#define DRIVER_DEMO_EFFECT_DURATION_MS      5000U

typedef enum {
    DRIVER_DEMO_PHASE_IDLE = 0,
    DRIVER_DEMO_PHASE_RAMP_TO_10,
    DRIVER_DEMO_PHASE_ACCEL_TO_25,
    DRIVER_DEMO_PHASE_CRUISE_25,
    DRIVER_DEMO_PHASE_BRAKE_TO_10,
    DRIVER_DEMO_PHASE_HOLD_10,
    DRIVER_DEMO_PHASE_BRAKE_TO_0,
} driver_demo_phase_t;

typedef struct {
    bool running;
    bool pending_reset_output;
    driver_demo_phase_t phase;
    uint32_t phase_start_tick;
    uint32_t effect_end_tick;
    driver_demo_effect_t effect;
} driver_demo_context_t;

static driver_demo_context_t g_driver_demo;

static void driver_demo_fill_output(driver_demo_output_t *output,
                                    int speed,
                                    const char *gear,
                                    driver_demo_effect_t effect)
{
    if (output == NULL) {
        return;
    }

    output->speed = speed;
    output->gear[0] = (gear != NULL && gear[0] != '\0') ? gear[0] : 'P';
    output->gear[1] = '\0';
    output->effect = effect;
}

static int interpolate_speed(int start_speed,
                             int end_speed,
                             uint32_t elapsed_ms,
                             uint32_t duration_ms)
{
    int delta;

    if (duration_ms == 0U || elapsed_ms >= duration_ms) {
        return end_speed;
    }

    delta = end_speed - start_speed;
    return start_speed + (int)((delta * (int32_t)elapsed_ms) / (int32_t)duration_ms);
}

static void driver_demo_start(uint32_t tick_ms)
{
    g_driver_demo.running = true;
    g_driver_demo.pending_reset_output = false;
    g_driver_demo.phase = DRIVER_DEMO_PHASE_RAMP_TO_10;
    g_driver_demo.phase_start_tick = tick_ms;
    g_driver_demo.effect_end_tick = 0U;
    g_driver_demo.effect = DRIVER_DEMO_EFFECT_NONE;
}

static void driver_demo_stop(void)
{
    g_driver_demo.running = false;
    g_driver_demo.pending_reset_output = true;
    g_driver_demo.phase = DRIVER_DEMO_PHASE_IDLE;
    g_driver_demo.effect_end_tick = 0U;
    g_driver_demo.effect = DRIVER_DEMO_EFFECT_NONE;
}

void driver_demo_init(void)
{
    memset(&g_driver_demo, 0, sizeof(g_driver_demo));
    g_driver_demo.phase = DRIVER_DEMO_PHASE_IDLE;
}

bool driver_demo_is_running(void)
{
    return g_driver_demo.running;
}

bool driver_demo_handle_button(KEYCODE keycode,
                               BUTTON_EVENT event,
                               bool home_page_active,
                               uint32_t tick_ms)
{
    if (event != BUTTON_EVENT_DOUBLE_CLICK) {
        return false;
    }

    if (keycode == KEYCODE_UP && home_page_active) {
        driver_demo_start(tick_ms);
        return true;
    }

    if (keycode == KEYCODE_DOWN && g_driver_demo.running && home_page_active) {
        driver_demo_stop();
        return true;
    }

    return false;
}

bool driver_demo_update(driver_demo_output_t *output, uint32_t tick_ms)
{
    uint32_t elapsed_ms;
    bool phase_changed;

    if (!g_driver_demo.running) {
        if (g_driver_demo.pending_reset_output) {
            g_driver_demo.pending_reset_output = false;
            driver_demo_fill_output(output, 0, "P", DRIVER_DEMO_EFFECT_NONE);
            return true;
        }

        return false;
    }

    do {
        phase_changed = false;
        elapsed_ms = tick_ms - g_driver_demo.phase_start_tick;

        switch (g_driver_demo.phase) {
        case DRIVER_DEMO_PHASE_RAMP_TO_10:
            if (elapsed_ms >= DRIVER_DEMO_STAGE_RAMP_TO_10_MS) {
                g_driver_demo.phase = DRIVER_DEMO_PHASE_ACCEL_TO_25;
                g_driver_demo.phase_start_tick += DRIVER_DEMO_STAGE_RAMP_TO_10_MS;
                g_driver_demo.effect = DRIVER_DEMO_EFFECT_ACCEL;
                g_driver_demo.effect_end_tick = tick_ms + DRIVER_DEMO_EFFECT_DURATION_MS;
                phase_changed = true;
            }
            break;

        case DRIVER_DEMO_PHASE_ACCEL_TO_25:
            if (elapsed_ms >= DRIVER_DEMO_STAGE_ACCEL_TO_25_MS) {
                g_driver_demo.phase = DRIVER_DEMO_PHASE_CRUISE_25;
                g_driver_demo.phase_start_tick += DRIVER_DEMO_STAGE_ACCEL_TO_25_MS;
                phase_changed = true;
            }
            break;

        case DRIVER_DEMO_PHASE_CRUISE_25:
            if (elapsed_ms >= DRIVER_DEMO_STAGE_CRUISE_25_MS) {
                g_driver_demo.phase = DRIVER_DEMO_PHASE_BRAKE_TO_10;
                g_driver_demo.phase_start_tick += DRIVER_DEMO_STAGE_CRUISE_25_MS;
                g_driver_demo.effect = DRIVER_DEMO_EFFECT_BRAKE;
                g_driver_demo.effect_end_tick = tick_ms + DRIVER_DEMO_EFFECT_DURATION_MS;
                phase_changed = true;
            }
            break;

        case DRIVER_DEMO_PHASE_BRAKE_TO_10:
            if (elapsed_ms >= DRIVER_DEMO_STAGE_BRAKE_TO_10_MS) {
                g_driver_demo.phase = DRIVER_DEMO_PHASE_HOLD_10;
                g_driver_demo.phase_start_tick += DRIVER_DEMO_STAGE_BRAKE_TO_10_MS;
                phase_changed = true;
            }
            break;

        case DRIVER_DEMO_PHASE_HOLD_10:
            if (elapsed_ms >= DRIVER_DEMO_STAGE_HOLD_10_MS) {
                g_driver_demo.phase = DRIVER_DEMO_PHASE_BRAKE_TO_0;
                g_driver_demo.phase_start_tick += DRIVER_DEMO_STAGE_HOLD_10_MS;
                phase_changed = true;
            }
            break;

        case DRIVER_DEMO_PHASE_BRAKE_TO_0:
            if (elapsed_ms >= DRIVER_DEMO_STAGE_BRAKE_TO_0_MS) {
                driver_demo_stop();
                phase_changed = true;
            }
            break;

        case DRIVER_DEMO_PHASE_IDLE:
        default:
            break;
        }
    } while (phase_changed);

    if (g_driver_demo.effect != DRIVER_DEMO_EFFECT_NONE &&
        tick_ms >= g_driver_demo.effect_end_tick) {
        g_driver_demo.effect = DRIVER_DEMO_EFFECT_NONE;
    }

    elapsed_ms = tick_ms - g_driver_demo.phase_start_tick;

    switch (g_driver_demo.phase) {
    case DRIVER_DEMO_PHASE_RAMP_TO_10:
        driver_demo_fill_output(output,
                                interpolate_speed(0, 10,
                                                  elapsed_ms,
                                                  DRIVER_DEMO_STAGE_RAMP_TO_10_MS),
                                "D",
                                g_driver_demo.effect);
        break;

    case DRIVER_DEMO_PHASE_ACCEL_TO_25:
        driver_demo_fill_output(output,
                                interpolate_speed(10, 25,
                                                  elapsed_ms,
                                                  DRIVER_DEMO_STAGE_ACCEL_TO_25_MS),
                                "D",
                                g_driver_demo.effect);
        break;

    case DRIVER_DEMO_PHASE_CRUISE_25:
        driver_demo_fill_output(output, 25, "D", g_driver_demo.effect);
        break;

    case DRIVER_DEMO_PHASE_BRAKE_TO_10:
        driver_demo_fill_output(output,
                                interpolate_speed(25, 10,
                                                  elapsed_ms,
                                                  DRIVER_DEMO_STAGE_BRAKE_TO_10_MS),
                                "D",
                                g_driver_demo.effect);
        break;

    case DRIVER_DEMO_PHASE_HOLD_10:
        driver_demo_fill_output(output, 10, "D", g_driver_demo.effect);
        break;

    case DRIVER_DEMO_PHASE_BRAKE_TO_0:
    {
        int spd = interpolate_speed(10, 0, elapsed_ms, DRIVER_DEMO_STAGE_BRAKE_TO_0_MS);
        driver_demo_fill_output(output,
                                spd,
                                (spd == 0) ? "P" : "D",
                                g_driver_demo.effect);
        break;
    }

    case DRIVER_DEMO_PHASE_IDLE:
    default:
        driver_demo_fill_output(output, 0, "P", DRIVER_DEMO_EFFECT_NONE);
        break;
    }

    return true;
}