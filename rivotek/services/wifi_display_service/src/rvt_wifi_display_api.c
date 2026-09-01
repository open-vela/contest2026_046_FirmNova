#include "rvt_wifi_display_service.h"
#include "rvt_display_port.h"
#include "rvt_wifi_display_network_control.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef RVT_WIFI_DISPLAY_CMD_AS_SERVICE_TASK
#define RVT_WIFI_DISPLAY_CMD_AS_SERVICE_TASK 0
#endif

#if RVT_WIFI_DISPLAY_CMD_AS_SERVICE_TASK
#include <sched.h>
#include <unistd.h>
#endif

#define RVT_WIFI_DISPLAY_DEFAULT_WIDTH   800
#define RVT_WIFI_DISPLAY_DEFAULT_HEIGHT  480

#ifndef RVT_START_OTHER_CHANNELS_DELAY_MS
#define RVT_START_OTHER_CHANNELS_DELAY_MS 0
#endif

#ifndef RVT_WIFI_DISPLAY_TCP_WAIT_MS
#define RVT_WIFI_DISPLAY_TCP_WAIT_MS 30000
#endif

#if RVT_WIFI_DISPLAY_CMD_AS_SERVICE_TASK
#ifndef RVT_WIFI_DISPLAY_SERVICE_TASK_PRIORITY
#define RVT_WIFI_DISPLAY_SERVICE_TASK_PRIORITY 100
#endif
#ifndef RVT_WIFI_DISPLAY_SERVICE_TASK_STACK
#define RVT_WIFI_DISPLAY_SERVICE_TASK_STACK    16384
#endif
#ifndef RVT_WIFI_DISPLAY_SERVICE_START_WAIT_MS
#define RVT_WIFI_DISPLAY_SERVICE_START_WAIT_MS 12000
#endif
#ifndef RVT_WIFI_DISPLAY_SERVICE_STOP_WAIT_MS
#define RVT_WIFI_DISPLAY_SERVICE_STOP_WAIT_MS  5000
#endif
#define RVT_WIFI_DISPLAY_SERVICE_STOPPED  0
#define RVT_WIFI_DISPLAY_SERVICE_STARTING 1
#define RVT_WIFI_DISPLAY_SERVICE_RUNNING  2

static volatile int wifi_display_service_state = RVT_WIFI_DISPLAY_SERVICE_STOPPED;
static volatile int wifi_display_service_stop_requested = 0;
static volatile int wifi_display_service_start_result = 0;
static int wifi_display_service_pid = -1;
static rvt_wifi_display_link_mode_t wifi_display_service_mode =
    RVT_WIFI_DISPLAY_LINK_STA;
#endif

/* 缓存车机能力，避免每次手机连接都重复访问显示驱动。 */
static struct rvt_display_caps cached_caps = {0};
static rvt_bool_t caps_valid = RVT_FALSE;
/* 投屏服务整体运行状态，用于避免重复 start/stop。 */
static rvt_bool_t wifi_display_running = RVT_FALSE;
/* 记录本次启动采用的链路角色，stop 时按该角色释放 WiFi。 */
static rvt_wifi_display_link_mode_t running_link_mode = RVT_WIFI_DISPLAY_LINK_STA;
/* 外部应用/调试命令指定的能力覆盖值，0 表示使用平台默认能力。 */
static int screen_width_override = 0;
static int screen_height_override = 0;
static uint32_t codec_mask_override = 0;
/* 发给手机端的投屏数据流配置，CAP/RES/ROT 等 TCP 命令来源于这里。 */
static struct rvt_wifi_display_stream_config stream_config = {
    0, 0, 0, 0, 0
};

/*
 * 函数名: apply_capability_overrides
 * 入参: caps 待修正的显示和解码能力
 * 返回值: 无
 */
static void apply_capability_overrides(struct rvt_display_caps *caps)
{
    if (screen_width_override > 0)
        caps->width = screen_width_override;
    if (screen_height_override > 0)
        caps->height = screen_height_override;
    if (codec_mask_override != 0)
        caps->codec_mask = codec_mask_override;
}

int rvt_wifi_display_get_caps(struct rvt_display_caps *caps)
{
    if (!caps)
        return -1;

    if (!caps_valid) {
        memset(&cached_caps, 0, sizeof(cached_caps));
        /* 优先从平台 port 获取屏幕尺寸和解码格式，失败则使用兜底值。 */
        if (rvt_display_port_get_caps(&cached_caps) != 0)
            memset(&cached_caps, 0, sizeof(cached_caps));

        if (cached_caps.width <= 0)
            cached_caps.width = RVT_WIFI_DISPLAY_DEFAULT_WIDTH;
        if (cached_caps.height <= 0)
            cached_caps.height = RVT_WIFI_DISPLAY_DEFAULT_HEIGHT;

        apply_capability_overrides(&cached_caps);
        caps_valid = RVT_TRUE;
    }

    memcpy(caps, &cached_caps, sizeof(*caps));
    return 0;
}

/*
 * 函数名: ensure_stream_config
 * 入参: 无
 * 返回值: 无
 */
static void ensure_stream_config(void)
{
    struct rvt_display_caps caps;

    if (stream_config.width > 0 && stream_config.height > 0 &&
        stream_config.codec_mask != 0)
        return;

    if (rvt_wifi_display_get_caps(&caps) != 0)
        return;

    /* stream_config 只补空字段，保留外部已设置的分辨率/码率/旋转。 */
    if (stream_config.width <= 0)
        stream_config.width = caps.width;
    if (stream_config.height <= 0)
        stream_config.height = caps.height;
    if (stream_config.codec_mask == 0)
        stream_config.codec_mask = caps.codec_mask;
}

int rvt_wifi_display_get_stream_config(struct rvt_wifi_display_stream_config *config)
{
    if (!config)
        return -1;

    ensure_stream_config();
    memcpy(config, &stream_config, sizeof(*config));
    return 0;
}

int rvt_wifi_display_set_screen_size(int width, int height)
{
    if (width <= 0 || height <= 0)
        return -1;

    screen_width_override = width;
    screen_height_override = height;
    /* 屏幕尺寸覆盖后，同步作为默认投屏输出分辨率发给手机端。 */
    stream_config.width = width;
    stream_config.height = height;
    if (caps_valid) {
        cached_caps.width = width;
        cached_caps.height = height;
    }
    return 0;
}

int rvt_wifi_display_set_stream_resolution(int width, int height)
{
    if (width <= 0 || height <= 0)
        return -1;

    stream_config.width = width;
    stream_config.height = height;
    return 0;
}

int rvt_wifi_display_set_rotation(int rotation)
{
    if (rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270)
        return -1;

    stream_config.rotation = rotation;
    return 0;
}

int rvt_wifi_display_set_bitrate(int bitrate_kbps)
{
    if (bitrate_kbps < 0)
        return -1;

    stream_config.bitrate_kbps = bitrate_kbps;
    return 0;
}

int rvt_wifi_display_set_codec_mask(uint32_t codec_mask)
{
    codec_mask_override = codec_mask;
    /* codec_mask 同时影响 CAP 能力和本次投屏希望选择的编码格式。 */
    stream_config.codec_mask = codec_mask;
    if (caps_valid)
        cached_caps.codec_mask = codec_mask;
    return 0;
}

/*
 * 函数名: prepare_wifi_link
 * 入参: mode 投屏链路模式，STA 表示车机连接手机热点，SAP 表示车机开启热点
 * 返回值: 0 表示链路就绪，负值表示链路未就绪
 */
static int prepare_wifi_link(rvt_wifi_display_link_mode_t mode)
{
    if (mode == RVT_WIFI_DISPLAY_LINK_SAP) {
        if (rvt_wifi_display_sap_is_active())
            return 0;
        return rvt_wifi_display_sap_start();
    }

    return rvt_wifi_display_sta_prepare();
}

/*
 * 函数名: release_wifi_link
 * 入参: mode 投屏链路模式
 * 返回值: 无
 */
static void release_wifi_link(rvt_wifi_display_link_mode_t mode)
{
    if (mode == RVT_WIFI_DISPLAY_LINK_SAP)
        rvt_wifi_display_sap_stop();
}

/*
 * 函数名: rvt_wifi_display_start_with_mode
 * 入参: mode 投屏链路模式
 * 返回值: 0 表示启动成功，负值表示启动失败
 */
static int rvt_wifi_display_start_with_mode(rvt_wifi_display_link_mode_t mode)
{
    int ret;

    if (wifi_display_running) {
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG, "wifi display: already running");
        return 0;
    }

    /* 先补齐本次投屏参数，手机 TCP 连接后立即下发。 */
    ensure_stream_config();

    /* WiFi 链路先就绪，再启动 TCP/Discovery/UDP 三个投屏通道。 */
    ret = prepare_wifi_link(mode);
    if (ret != 0)
        return ret;

    ret = rvt_tcp_channel_init();
    if (ret != 0) {
        /* 后续任一通道启动失败，都按已启动的逆序清理。 */
        release_wifi_link(mode);
        return ret;
    }

#if RVT_START_OTHER_CHANNELS_DELAY_MS > 0
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG, "wifi display: delay aux channels %d ms",
             RVT_START_OTHER_CHANNELS_DELAY_MS);
    rvt_thread_mdelay(RVT_START_OTHER_CHANNELS_DELAY_MS);
#endif

    ret = rvt_wifi_display_discovery_start();
    if (ret != 0) {
        rvt_tcp_channel_stop();
        release_wifi_link(mode);
        return ret;
    }

    ret = udp_receiver_init();
    if (ret != 0) {
        rvt_wifi_display_discovery_stop();
        rvt_tcp_channel_stop();
        release_wifi_link(mode);
        return ret;
    }

    /* 三个通道全部启动成功后才标记服务运行，避免 stop 误释放半初始化状态。 */
    running_link_mode = mode;
    wifi_display_running = RVT_TRUE;
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG, "wifi display: started, link=%s",
                mode == RVT_WIFI_DISPLAY_LINK_SAP ? "sap" : "sta");
    return 0;
}

#if RVT_WIFI_DISPLAY_CMD_AS_SERVICE_TASK
static const char *link_mode_name(rvt_wifi_display_link_mode_t mode)
{
    return mode == RVT_WIFI_DISPLAY_LINK_SAP ? "sap" : "sta";
}

static int rvt_wifi_display_service_task_main(int argc, char *argv[])
{
    int ret;
    rvt_wifi_display_link_mode_t mode = wifi_display_service_mode;
    rvt_tick_t tcp_wait_start;

    (void)argc;
    (void)argv;

    wifi_display_service_pid = getpid();
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "wifi display service: task enter pid=%d mode=%s",
             wifi_display_service_pid, link_mode_name(mode));

    ret = rvt_wifi_display_start_with_mode(mode);
    wifi_display_service_start_result = ret;
    if (ret != 0) {
        RVT_LOGE(RVT_WIFI_DISPLAY_LOG_TAG,
                 "wifi display service: start failed ret=%d", ret);
        wifi_display_service_pid = -1;
        wifi_display_service_stop_requested = 0;
        wifi_display_service_state = RVT_WIFI_DISPLAY_SERVICE_STOPPED;
        return ret;
    }

    tcp_wait_start = rvt_tick_get();
    wifi_display_service_state = RVT_WIFI_DISPLAY_SERVICE_RUNNING;
    while (!wifi_display_service_stop_requested && wifi_display_running) {
        if (mode == RVT_WIFI_DISPLAY_LINK_STA &&
            !rvt_tcp_channel_has_client() &&
            rvt_tick_get() - tcp_wait_start >=
            rvt_tick_from_millisecond(RVT_WIFI_DISPLAY_TCP_WAIT_MS)) {
            RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                     "wifi display service: tcp wait timeout, stop");
            rvt_wifi_display_stop();
            break;
        }

        if (rvt_tcp_channel_has_client())
            tcp_wait_start = rvt_tick_get();

        rvt_thread_mdelay(200);
    }

    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "wifi display service: task leave stop=%d running=%d",
             wifi_display_service_stop_requested, wifi_display_running);

    if (wifi_display_running)
        rvt_wifi_display_stop();

    wifi_display_service_pid = -1;
    wifi_display_service_stop_requested = 0;
    wifi_display_service_state = RVT_WIFI_DISPLAY_SERVICE_STOPPED;
    return 0;
}

static int rvt_wifi_display_start_service_task(rvt_wifi_display_link_mode_t mode)
{
    int pid;
    int wait_ms = 0;

    if (wifi_display_service_state != RVT_WIFI_DISPLAY_SERVICE_STOPPED ||
        wifi_display_running) {
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "wifi display service: already active state=%d pid=%d running=%d",
                 wifi_display_service_state, wifi_display_service_pid,
                 wifi_display_running);
        return 0;
    }

    wifi_display_service_mode = mode;
    wifi_display_service_stop_requested = 0;
    wifi_display_service_start_result = -1;
    wifi_display_service_state = RVT_WIFI_DISPLAY_SERVICE_STARTING;

    pid = task_create("rvtdisp",
                      RVT_WIFI_DISPLAY_SERVICE_TASK_PRIORITY,
                      RVT_WIFI_DISPLAY_SERVICE_TASK_STACK,
                      rvt_wifi_display_service_task_main,
                      RVT_NULL);
    if (pid < 0) {
        RVT_LOGE(RVT_WIFI_DISPLAY_LOG_TAG,
                 "wifi display service: task_create failed ret=%d", pid);
        wifi_display_service_pid = -1;
        wifi_display_service_state = RVT_WIFI_DISPLAY_SERVICE_STOPPED;
        return -1;
    }

    wifi_display_service_pid = pid;
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "wifi display service: task created pid=%d mode=%s",
             pid, link_mode_name(mode));

    while (wifi_display_service_state == RVT_WIFI_DISPLAY_SERVICE_STARTING &&
           wait_ms < RVT_WIFI_DISPLAY_SERVICE_START_WAIT_MS) {
        rvt_thread_mdelay(50);
        wait_ms += 50;
    }

    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "wifi display service: start wait state=%d ret=%d wait=%d",
             wifi_display_service_state, wifi_display_service_start_result,
             wait_ms);

    if (wifi_display_service_state == RVT_WIFI_DISPLAY_SERVICE_RUNNING)
        return 0;

    if (wifi_display_service_state == RVT_WIFI_DISPLAY_SERVICE_STARTING)
        return 0;

    return wifi_display_service_start_result == 0 ? 0 : -1;
}

static void rvt_wifi_display_stop_service_task(void)
{
    int wait_ms = 0;

    if (wifi_display_service_state == RVT_WIFI_DISPLAY_SERVICE_STOPPED) {
        if (wifi_display_running)
            rvt_wifi_display_stop();
        else
            RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                     "wifi display service: stop no active task");
        return;
    }

    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "wifi display service: stop request state=%d pid=%d",
             wifi_display_service_state, wifi_display_service_pid);
    wifi_display_service_stop_requested = 1;

    while (wifi_display_service_state != RVT_WIFI_DISPLAY_SERVICE_STOPPED &&
           wait_ms < RVT_WIFI_DISPLAY_SERVICE_STOP_WAIT_MS) {
        rvt_thread_mdelay(50);
        wait_ms += 50;
    }

    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "wifi display service: stop wait state=%d wait=%d",
             wifi_display_service_state, wait_ms);
}
#endif

int rvt_wifi_display_start(void)
{
#if RVT_WIFI_DISPLAY_CMD_AS_SERVICE_TASK
    return rvt_wifi_display_start_service_task(RVT_WIFI_DISPLAY_LINK_STA);
#else
    return rvt_wifi_display_start_with_mode(RVT_WIFI_DISPLAY_LINK_STA);
#endif
}

int rvt_wifi_display_is_running(void)
{
    return wifi_display_running == RVT_TRUE ? 1 : 0;
}

void rvt_wifi_display_stop(void)
{
    uint32_t total;
    uint32_t lost;

    /* stop 允许重复调用，各子模块内部负责处理未启动状态。 */
    rvt_wifi_display_discovery_stop();
    rvt_tcp_channel_stop();
    udp_receiver_stop();
    /* 平台 port 内部会清解码队列、关闭 video layer 并恢复 UI layer。 */
    rvt_display_port_deinit();
    get_udp_statistics(&total, &lost);
    release_wifi_link(running_link_mode);
    wifi_display_running = RVT_FALSE;
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG, "wifi display: stopped and cache cleared");
}

/*
 * 函数名: cmd_wifi_display_start
 * 入参: argc 参数数量，argv 参数列表
 * 返回值: 0 表示执行成功，负值表示参数错误或启动失败
 */
int rvt_wifi_display_cmd_start(int argc, char *argv[])
{
    rvt_wifi_display_link_mode_t mode = RVT_WIFI_DISPLAY_LINK_STA;

    if (argc > 2) {
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG, "usage: start_wifi_display [sta|sap]");
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG, "example: start_wifi_display");
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG, "example: start_wifi_display sap");
        return -1;
    }

    if (argc == 2) {
        if (strcmp(argv[1], "sta") == 0) {
            mode = RVT_WIFI_DISPLAY_LINK_STA;
        } else if (strcmp(argv[1], "sap") == 0) {
            mode = RVT_WIFI_DISPLAY_LINK_SAP;
        } else {
            RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG, "usage: start_wifi_display [sta|sap]");
            RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG, "example: start_wifi_display sta");
            RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG, "example: start_wifi_display sap");
            return -1;
        }
    }

#if RVT_WIFI_DISPLAY_CMD_AS_SERVICE_TASK
    return rvt_wifi_display_start_service_task(mode);
#else
    return rvt_wifi_display_start_with_mode(mode);
#endif
}

/*
 * 函数名: cmd_wifi_display_stop
 * 入参: argc 参数数量，argv 参数列表
 * 返回值: 0 表示执行成功，负值表示参数错误
 */
int rvt_wifi_display_cmd_stop(int argc, char *argv[])
{
    if (argc != 1) {
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG, "usage: stop_wifi_display");
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG, "example: stop_wifi_display");
        return -1;
    }

#if RVT_WIFI_DISPLAY_CMD_AS_SERVICE_TASK
    rvt_wifi_display_stop_service_task();
#else
    rvt_wifi_display_stop();
#endif
    return 0;
}

/*
 * 函数名: cmd_wifi_display_screen_size
 * 入参: argc 参数数量，argv 参数列表，argv[1] 为宽度，argv[2] 为高度
 * 返回值: 0 表示设置成功，负值表示参数错误或设置失败
 */
int rvt_wifi_display_cmd_screen_size(int argc, char *argv[])
{
    if (argc != 3) {
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG, "usage: wifi_display_screen_size <width> <height>");
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG, "example: wifi_display_screen_size 800 480");
        return -1;
    }

    return rvt_wifi_display_set_screen_size(atoi(argv[1]), atoi(argv[2]));
}

/*
 * 函数名: cmd_wifi_display_rotation
 * 入参: argc 参数数量，argv 参数列表，argv[1] 为旋转角度
 * 返回值: 0 表示设置成功，负值表示参数错误或设置失败
 */
int rvt_wifi_display_cmd_rotation(int argc, char *argv[])
{
    if (argc != 2) {
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG, "usage: wifi_display_rotation <0|90|180|270>");
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG, "example: wifi_display_rotation 90");
        return -1;
    }

    return rvt_wifi_display_set_rotation(atoi(argv[1]));
}

#ifdef RT_USING_FINSH
#include <msh.h>
/*
 * MSH commands intentionally mirror the four external app APIs:
 *   start_wifi_display [sta|sap]      // 无参时默认车机是sta模式，sap 则车机热点模式
 *   stop_wifi_display                 // 无参时停止默认模式
 *   wifi_display_screen_size 800 480  // 无参时默认默认分辨率
 *   wifi_display_rotation 90          // 无参时默认默认旋转角度
 */
MSH_CMD_EXPORT_ALIAS(rvt_wifi_display_cmd_start, start_wifi_display,
                     start wifi display example start_wifi_display sap);
MSH_CMD_EXPORT_ALIAS(rvt_wifi_display_cmd_stop, stop_wifi_display,
                     stop wifi display example stop_wifi_display);
MSH_CMD_EXPORT_ALIAS(rvt_wifi_display_cmd_screen_size, wifi_display_screen_size,
                     set screen size example wifi_display_screen_size 800 480);
MSH_CMD_EXPORT_ALIAS(rvt_wifi_display_cmd_rotation, wifi_display_rotation,
                     set rotation example wifi_display_rotation 90);
#endif
