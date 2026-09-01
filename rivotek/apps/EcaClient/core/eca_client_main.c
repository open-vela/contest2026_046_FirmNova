#include <nuttx/config.h>

#ifdef CONFIG_ECA_CLIENT_APP

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

#include <lvgl/lvgl.h>

#ifdef CONFIG_LV_USE_NUTTX_LIBUV
#include <uv.h>
#endif

#include "eca_client_ai_agent.h"
#include "eca_client_humidity_sensor.h"
#include "eca_client_device_state.h"
#include "eca_client_log.h"
#include "eca_client_model.h"
#include "eca_client_network.h"
#include "eca_client_plan_observer.h"
#include "eca_client_registration.h"
#include "eca_client_time_observer.h"
#include "eca_client_ui.h"
#include "eca_client_voice_keyword.h"
#include "rivotek_platform.h"

#ifndef CONFIG_ECA_CLIENT_APP_TOUCH_DEVPATH
#define CONFIG_ECA_CLIENT_APP_TOUCH_DEVPATH "/dev/input0"
#endif

#define ECA_CLIENT_CARE_TIMER_MS 60000
#define ECA_CLIENT_CARE_STARTUP_QUIET_SEC (10 * 60)
#define ECA_CLIENT_CARE_CONFIRM_SEC (5 * 60)
#define ECA_CLIENT_CARE_COOLDOWN_SEC (2 * 60 * 60)

enum eca_client_care_type {
    ECA_CLIENT_CARE_NONE = 0,
    ECA_CLIENT_CARE_HOT,
    ECA_CLIENT_CARE_COLD,
    ECA_CLIENT_CARE_HUMID,
    ECA_CLIENT_CARE_BEDTIME,
    ECA_CLIENT_CARE_COUNT,
};

static time_t g_care_started_at;
static time_t g_care_condition_since[ECA_CLIENT_CARE_COUNT];
static time_t g_care_last_spoken;
static int g_care_last_day[ECA_CLIENT_CARE_COUNT];

static time_t eca_client_monotonic_seconds(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) {
        return 0;
    }

    return now.tv_sec;
}

static void eca_client_humidity_timer_cb(lv_timer_t *timer)
{
    eca_client_model_t *model;

    model = (eca_client_model_t *)lv_timer_get_user_data(timer);
    eca_client_humidity_sensor_update(model, 0);
}

static void eca_client_registration_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    eca_client_registration_poll();
}

static void eca_client_time_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    eca_client_time_observer_poll();
}

static void eca_client_plan_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    eca_client_plan_observer_poll();
}

static void eca_client_care_timer_cb(lv_timer_t *timer)
{
    eca_client_device_snapshot_t state;
    struct tm local_tm;
    time_t wall_now;
    time_t monotonic_now;
    enum eca_client_care_type selected = ECA_CLIENT_CARE_NONE;
    const char *message = NULL;
    bool active[ECA_CLIENT_CARE_COUNT] = { false };
    int day_key;
    int type;

    (void)timer;
    wall_now = time(NULL);
    monotonic_now = eca_client_monotonic_seconds();
    if (monotonic_now == 0 ||
        monotonic_now < g_care_started_at + ECA_CLIENT_CARE_STARTUP_QUIET_SEC ||
        localtime_r(&wall_now, &local_tm) == NULL || local_tm.tm_year < 120) {
        return;
    }

    eca_client_device_state_get(&state);
    active[ECA_CLIENT_CARE_HOT] = state.temperature > 30.0f;
    active[ECA_CLIENT_CARE_COLD] = state.temperature < 10.0f;
    active[ECA_CLIENT_CARE_HUMID] = state.humidity > 70.0f;
    active[ECA_CLIENT_CARE_BEDTIME] = local_tm.tm_hour == 22 &&
                                      local_tm.tm_min <= 30;

    for (type = ECA_CLIENT_CARE_HOT; type < ECA_CLIENT_CARE_COUNT; type++) {
        if (!active[type]) {
            g_care_condition_since[type] = 0;
        } else if (g_care_condition_since[type] == 0) {
            g_care_condition_since[type] = monotonic_now;
        }
    }

    if (g_care_last_spoken != 0 &&
        monotonic_now - g_care_last_spoken < ECA_CLIENT_CARE_COOLDOWN_SEC) {
        return;
    }

    day_key = (local_tm.tm_year + 1900) * 1000 + local_tm.tm_yday;
    for (type = ECA_CLIENT_CARE_HOT; type < ECA_CLIENT_CARE_COUNT; type++) {
        if (active[type] && g_care_condition_since[type] != 0 &&
            monotonic_now - g_care_condition_since[type] >=
                ECA_CLIENT_CARE_CONFIRM_SEC &&
            g_care_last_day[type] != day_key) {
            selected = type;
            break;
        }
    }

    switch (selected) {
    case ECA_CLIENT_CARE_HOT:
        message = "天气炎热，您要注意防暑，小心中暑哦。";
        break;
    case ECA_CLIENT_CARE_COLD:
        message = "天气转冷了，记得加件衣服，别着凉了。";
        break;
    case ECA_CLIENT_CARE_HUMID:
        message = "今天空气很潮湿，地面可能湿滑，您走路要小心安全。";
        break;
    case ECA_CLIENT_CARE_BEDTIME:
        message = "时间不早了，您早点休息吧，祝您好梦。";
        break;
    default:
        return;
    }

    if (!eca_client_ai_agent_is_idle() ||
        eca_client_ai_agent_speak(message, NULL) < 0) {
        return;
    }

    g_care_last_day[selected] = day_key;
    g_care_last_spoken = monotonic_now;
    ECA_LOGI("proactive care type=%d queued", selected);
}

int main(int argc, FAR char *argv[])
{
    static eca_client_model_t model;
    lv_nuttx_dsc_t info;
    lv_nuttx_result_t result;
#ifdef CONFIG_LV_USE_NUTTX_LCD
    const rivotek_platform_config_t *platform_config;
#endif
    bool network_started = false;
    lv_obj_t *screen;

    (void)argc;
    (void)argv;

    if (lv_is_initialized()) {
        ECA_LOGE("LVGL already initialized");
        return -1;
    }

    if (rivotek_platform_board_init() < 0) {
        ECA_LOGE("board init failed");
        return -1;
    }

    lv_init();

    lv_nuttx_dsc_init(&info);

#ifdef CONFIG_LV_USE_NUTTX_LCD
    platform_config = rivotek_platform_config_get();
    info.fb_path = platform_config->fb_path;
    while (access(info.fb_path, F_OK) < 0) {
        ECA_LOGI("wait for %s", info.fb_path);
        usleep(100000);
    }
#endif

#ifdef CONFIG_LV_USE_NUTTX_TOUCHSCREEN
    info.input_path = CONFIG_ECA_CLIENT_APP_TOUCH_DEVPATH;
    ECA_LOGI("LVGL input path=%s", info.input_path);
#endif

    lv_nuttx_init(&info, &result);
    usleep(100000);

    if (result.disp == NULL) {
        ECA_LOGE("LVGL NuttX backend init failed");
        return -1;
    }

    screen = lv_scr_act();

    eca_client_model_init(&model);
    if (eca_client_device_state_init() < 0) {
        ECA_LOGE("device state init failed");
        return -1;
    }

    eca_client_ai_agent_start();
    g_care_started_at = eca_client_monotonic_seconds();
    lv_timer_create(eca_client_care_timer_cb,
                    ECA_CLIENT_CARE_TIMER_MS, NULL);

    eca_client_create_screen(screen, &model);
    if (eca_client_plan_observer_init(&model) < 0) {
        ECA_LOGW("plan observer init failed");
    } else {
        eca_client_plan_observer_set_speaker(
            eca_client_ai_agent_speak, NULL);
        lv_timer_create(eca_client_plan_timer_cb, 200, NULL);
    }

    if (eca_client_time_observer_init(&model) == 0) {
        lv_timer_create(eca_client_time_timer_cb, 500, NULL);
    }

    if (eca_client_registration_init(screen, &model) == 0) {
        lv_timer_create(eca_client_registration_timer_cb, 50, NULL);
        if (eca_client_registration_show_qrcode() < 0) {
            ECA_LOGW("registration qrcode show failed");
        }
    }

    if (eca_client_network_start(&model) < 0) {
        ECA_LOGE("network start failed");
    } else {
        network_started = true;
    }

    if (network_started && eca_client_voice_keyword_start(&model) < 0) {
        ECA_LOGW("voice keyword listener start failed");
    }

    if (eca_client_humidity_sensor_init(&model) >= 0) {
        lv_timer_create(eca_client_humidity_timer_cb, 1000, &model);
    }

#ifdef CONFIG_LV_USE_NUTTX_LIBUV
    {
        uv_loop_t ui_loop;
        lv_nuttx_uv_t uv_info;
        void *data;

        uv_loop_init(&ui_loop);
        lv_memzero(&uv_info, sizeof(uv_info));
        uv_info.loop = &ui_loop;
        uv_info.disp = result.disp;
        uv_info.indev = result.indev;
#ifdef CONFIG_UINPUT_TOUCH
        uv_info.uindev = result.utouch_indev;
#endif
        data = lv_nuttx_uv_init(&uv_info);
        uv_run(&ui_loop, UV_RUN_DEFAULT);
        lv_nuttx_uv_deinit(&data);
    }
#else
    while (1) {
        uint32_t sleep_ms = lv_timer_handler();

        if (sleep_ms < 1) {
            sleep_ms = 1;
        } else if (sleep_ms > 20) {
            sleep_ms = 20;
        }

        usleep(sleep_ms * 1000);
    }
#endif

    eca_client_voice_keyword_stop();
    eca_client_network_stop();
    eca_client_humidity_sensor_deinit();
    eca_client_device_state_deinit();
    return 0;
}

#endif
