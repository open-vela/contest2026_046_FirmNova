#include "rvt_map_api.h"
#include "rvt_wifi_display_api.h"
#include "rvt_wifi_display_service.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RVT_MAP_CMD_TEXT_MAX 128
#define RVT_MAP_PENDING_CMD_MAX 192

/* AI 命令可能早于手机 TCP 连接到达，缓存一条待发送地图命令。 */
static char pending_cmd[RVT_MAP_PENDING_CMD_MAX];
static rvt_bool_t pending_cmd_valid = RVT_FALSE;
static rvt_mutex_t pending_cmd_mutex = RVT_NULL;
static rvt_bool_t map_display_active = RVT_FALSE;

/*
 * 函数名: ensure_pending_mutex
 * 入参: 无
 * 返回值: 0 表示互斥锁可用，负值表示创建失败
 */
static int ensure_pending_mutex(void)
{
    if (pending_cmd_mutex)
        return 0;

    pending_cmd_mutex = rvt_mutex_create("rvtmap");
    return pending_cmd_mutex ? 0 : -1;
}

/*
 * 函数名: cache_pending_command
 * 入参: cmd 需要延迟发送给手机端的 TCP 命令
 * 返回值: 0 表示缓存成功，负值表示参数无效或缓存失败
 */
static int cache_pending_command(const char *cmd)
{
    if (!cmd || !cmd[0] || ensure_pending_mutex() != 0)
        return -1;

    rvt_mutex_take(pending_cmd_mutex, RVT_WAIT_FOREVER);
    snprintf(pending_cmd, sizeof(pending_cmd), "%s", cmd);
    pending_cmd_valid = RVT_TRUE;
    rvt_mutex_release(pending_cmd_mutex);
    RVT_LOGI(RVT_MAP_LOG_TAG, "map: command pending until TCP connected: %s", pending_cmd);
    return 0;
}

/*
 * 函数名: send_simple_command
 * 入参: cmd 需要发送给手机端的完整 TCP 命令，必须包含换行符
 * 返回值: 0 表示发送成功，负值表示 TCP 未连接或发送失败
 */
static int send_simple_command(const char *cmd)
{
    if (!cmd)
        return -1;

    return rvt_tcp_channel_send_command(cmd);
}

/*
 * 函数名: append_nav_name
 * 入参: dst 命令缓冲区，dst_size 缓冲区长度，name 目的地名称
 * 返回值: 无
 */
static void append_nav_name(char *dst, int dst_size, const char *name)
{
    int used;
    int i;

    if (!dst || dst_size <= 0 || !name || !name[0])
        return;

    used = strlen(dst);
    if (used >= dst_size - 2)
        return;

    dst[used++] = ' ';
    for (i = 0; name[i] != '\0' && used < dst_size - 1; i++) {
        char c = name[i];
        dst[used++] = (c == '\r' || c == '\n') ? ' ' : c;
    }
    dst[used] = '\0';
}

/*
 * 函数名: append_line_end
 * 入参: dst 命令缓冲区，dst_size 缓冲区长度
 * 返回值: 无
 */
static void append_line_end(char *dst, int dst_size)
{
    int used;

    if (!dst || dst_size <= 0)
        return;

    used = strlen(dst);
    if (used >= dst_size - 2)
        return;

    snprintf(dst + used, dst_size - used, "\r\n");
}

/*
 * 函数名: send_text_command
 * 入参: prefix TCP 命令前缀，text 命令文本参数
 * 返回值: 0 表示发送成功，负值表示参数无效、TCP 未连接或发送失败
 */
static int send_text_command(const char *prefix, const char *text)
{
    char cmd[RVT_MAP_CMD_TEXT_MAX + 32];

    if (!prefix || !text || !text[0])
        return -1;

    snprintf(cmd, sizeof(cmd), "%s %s\r\n", prefix, text);
    return rvt_tcp_channel_send_command(cmd);
}

/*
 * 函数名: send_or_cache_command
 * 入参: cmd 已完整格式化的 TCP 命令，必须包含换行符。适用于无参数命令或已预先组包的命令
 * 返回值: 0 表示已发送或已缓存，负值表示参数无效或缓存失败
 */
static int send_or_cache_command(const char *cmd)
{
    if (!cmd || !cmd[0])
        return -1;

    if (rvt_tcp_channel_send_command(cmd) == 0)
        return 0;

    return cache_pending_command(cmd);
}

/*
 * 函数名: send_or_cache_text_command
 * 入参: prefix TCP 命令前缀，text 命令文本参数。适用于必须带文本入参的命令
 * 返回值: 0 表示已发送或已缓存，负值表示参数无效或缓存失败
 */
static int send_or_cache_text_command(const char *prefix, const char *text)
{
    char cmd[RVT_MAP_CMD_TEXT_MAX + 32];

    if (!prefix || !text || !text[0])
        return -1;

    snprintf(cmd, sizeof(cmd), "%s %s\r\n", prefix, text);
    return send_or_cache_command(cmd);
}

int rvt_map_set_nav_destination(int32_t latitude_e7,
        int32_t longitude_e7, const char *name)
{
    char cmd[RVT_MAP_CMD_TEXT_MAX + 64];

    if (latitude_e7 < -900000000 || latitude_e7 > 900000000 ||
        longitude_e7 < -1800000000 || longitude_e7 > 1800000000)
        return -1;

    snprintf(cmd, sizeof(cmd), "NAV_DEST %ld %ld",
             (long)latitude_e7, (long)longitude_e7);
    append_nav_name(cmd, sizeof(cmd), name);
    append_line_end(cmd, sizeof(cmd));
    return rvt_tcp_channel_send_command(cmd);
}

int rvt_map_search_destination(const char *keyword)
{
    return send_text_command("NAV_SEARCH", keyword);
}

int rvt_map_prepare_navigation(const char *keyword)
{
    int ret;

    if (!keyword || !keyword[0])
        return -1;

    map_display_active = RVT_FALSE;

    /*
     * AI 触发“去某地”时，车机先保证 WiFi 投屏服务已启动。
     * 真正拉起手机端地图依赖手机 App 已运行并完成 TCP 连接。
     */
    ret = rvt_wifi_display_start();
    if (ret != 0)
        return ret;

    return send_or_cache_text_command("NAV_GO", keyword);
}

int rvt_map_show_location(void)
{
    int ret;

    map_display_active = RVT_FALSE;

    ret = rvt_wifi_display_start();
    if (ret != 0)
        return ret;

    return send_or_cache_command("MAP_SHOW_LOCATION\r\n");
}

int rvt_map_select_destination(int select_num)
{
    char cmd[32];

    if (select_num <= 0)
        return -1;

    snprintf(cmd, sizeof(cmd), "NAV_SELECT %d\r\n", select_num);
    return rvt_tcp_channel_send_command(cmd);
}

int rvt_map_plan_route(void)
{
    return send_simple_command("NAV_ROUTE\r\n");
}

int rvt_map_nav_start(void)
{
    return send_simple_command("NAV_START\r\n");
}

int rvt_map_nav_stop(void)
{
    return send_simple_command("NAV_STOP\r\n");
}

int rvt_map_set_vehicle_speed(float speed_kmh)
{
    char cmd[64];

    if (speed_kmh < 0.0f)
        return -1;

    snprintf(cmd, sizeof(cmd), "NAV_SPEED %.1f\r\n", speed_kmh);
    return rvt_tcp_channel_send_command(cmd);
}

int rvt_map_set_direction(rvt_map_direction_t direction)
{
    if (direction == RVT_MAP_DIRECTION_ROUTE_UP)
        return send_simple_command("MAP_DIR route_up\r\n");
    if (direction == RVT_MAP_DIRECTION_NORTH_UP)
        return send_simple_command("MAP_DIR north_up\r\n");

    return -1;
}

void rvt_map_handle_turn_prompt(int distance_m,
        const char *direction, const char *prompt_text)
{
    if (!direction)
        direction = "unknown";
    if (!prompt_text)
        prompt_text = "";

    /*
     * 手机端在接近路口时回传 NAV_TURN。
     * 后续接入车机语音模块时，可在这里把 prompt_text 转成 TTS 播报。
     */
    RVT_LOGI(RVT_MAP_LOG_TAG, "map: turn prompt distance=%d direction=%s text=%s",
                 distance_m, direction, prompt_text);
}

void rvt_map_handle_navigation_arrived(void)
{
    /*
     * 手机端到达目的地后回传 NAV_ARRIVED。
     * 后续可在这里触发到达播报或通知 AI 导航状态完成。
     */
    rvt_map_handle_navigation_status(RVT_MAP_NAV_EVENT_ARRIVED,
                                     RVT_MAP_NAV_STATUS_OK, "arrived");
}

void rvt_map_handle_navigation_match(const char *status, const char *message)
{
    if (!status)
        status = "UNKNOWN";
    if (!message)
        message = "";

    /*
     * 手机端收到 map_go 后回传 NAV_MATCH。
     * 为了方便 AI 侧统一处理，这里归一到 NAV_STATUS/MATCH 语义。
     */
    rvt_map_handle_navigation_status(RVT_MAP_NAV_EVENT_MATCH, status, message);
}

void rvt_map_handle_navigation_status(const char *event,
        const char *status, const char *message)
{
    if (!event)
        event = "UNKNOWN";
    if (!status)
        status = "UNKNOWN";
    if (!message)
        message = "";

    /*
     * 手机端在搜索、规划、导航启动、收藏、交通工具选择等流程失败时回传 NAV_STATUS。
     * 后续 AI 或语音模块可根据 event/status/message 做提示和业务恢复。
     */
    RVT_LOGI(RVT_MAP_LOG_TAG, "map: navigation status event=%s status=%s text=%s",
                 event, status, message);
}

void rvt_map_handle_display_active(void)
{
    if (map_display_active)
        return;

    map_display_active = RVT_TRUE;
    RVT_LOGI(RVT_MAP_LOG_TAG, "map: display active event=%s status=%s",
             RVT_MAP_NAV_EVENT_DISPLAY, RVT_MAP_NAV_STATUS_OK);
}

void rvt_map_flush_pending_commands(void)
{
    char cmd[RVT_MAP_PENDING_CMD_MAX];

    if (ensure_pending_mutex() != 0)
        return;

    rvt_mutex_take(pending_cmd_mutex, RVT_WAIT_FOREVER);
    if (!pending_cmd_valid) {
        rvt_mutex_release(pending_cmd_mutex);
        return;
    }
    snprintf(cmd, sizeof(cmd), "%s", pending_cmd);
    rvt_mutex_release(pending_cmd_mutex);

    if (rvt_tcp_channel_send_command(cmd) == 0) {
        rvt_mutex_take(pending_cmd_mutex, RVT_WAIT_FOREVER);
        pending_cmd_valid = RVT_FALSE;
        pending_cmd[0] = '\0';
        rvt_mutex_release(pending_cmd_mutex);
        RVT_LOGI(RVT_MAP_LOG_TAG, "map: pending command sent");
    }
}

/*
 * 函数名: build_text_arg
 * 入参: argc 参数数量，argv 参数列表，start 第一个文本参数下标，out 输出缓冲区，out_size 输出缓冲区大小
 * 返回值: 0 表示拼接成功，负值表示参数无效
 */
static int build_text_arg(int argc, char *argv[], int start,
                          char *out, int out_size)
{
    int i;
    int used = 0;

    if (!out || out_size <= 0 || start >= argc)
        return -1;

    out[0] = '\0';
    for (i = start; i < argc; i++) {
        int n;

        if (used > 0) {
            if (used >= out_size - 1)
                break;
            out[used++] = ' ';
            out[used] = '\0';
        }

        n = snprintf(out + used, out_size - used, "%s", argv[i]);
        if (n < 0)
            return -1;
        if (n >= out_size - used) {
            out[out_size - 1] = '\0';
            break;
        }
        used += n;
    }

    return used > 0 ? 0 : -1;
}

/*
 * 函数名: cmd_map_locate
 * 入参: argc 参数数量，argv 参数列表，argv[1..] 为目的地关键词
 * 返回值: 0 表示命令发送成功，负值表示参数错误或 TCP 未连接
 */
int rvt_map_cmd_locate(int argc, char *argv[])
{
    char keyword[RVT_MAP_CMD_TEXT_MAX];

    if (argc < 2 || build_text_arg(argc, argv, 1,
                                   keyword, sizeof(keyword)) != 0) {
        RVT_LOGI(RVT_MAP_LOG_TAG, "usage: map_locate <keyword>");
        RVT_LOGI(RVT_MAP_LOG_TAG, "example: map_locate 南京站");
        RVT_LOGI(RVT_MAP_LOG_TAG, "example: map_locate NanjingStation");
        return -1;
    }

    return rvt_map_search_destination(keyword);
}

/*
 * 函数名: cmd_map_go
 * 入参: argc 参数数量，argv 参数列表，argv[1..] 为目的地关键词
 * 返回值: 0 表示投屏服务启动且规划路线命令发送成功，负值表示失败
 */
int rvt_map_cmd_go(int argc, char *argv[])
{
    char keyword[RVT_MAP_CMD_TEXT_MAX];

    if (argc < 2 || build_text_arg(argc, argv, 1,
                                   keyword, sizeof(keyword)) != 0) {
        RVT_LOGI(RVT_MAP_LOG_TAG, "usage: map_go <keyword>");
        RVT_LOGI(RVT_MAP_LOG_TAG, "example: map_go 南京南站");
        RVT_LOGI(RVT_MAP_LOG_TAG, "example: map_go home");
        RVT_LOGI(RVT_MAP_LOG_TAG, "example: map_go primary school");
        RVT_LOGI(RVT_MAP_LOG_TAG, "example: map_go gongsi");
        return -1;
    }

    return rvt_map_prepare_navigation(keyword);
}

/*
 * 函数名: cmd_map_show_location
 * 入参: argc 参数数量，argv 参数列表
 * 返回值: 0 表示投屏服务启动且地图定位显示命令发送成功，负值表示失败
 */
int rvt_map_cmd_show_location(int argc, char *argv[])
{
    if (argc != 1) {
        RVT_LOGI(RVT_MAP_LOG_TAG, "usage: map_show_location");
        RVT_LOGI(RVT_MAP_LOG_TAG, "example: map_show_location");
        return -1;
    }

    return rvt_map_show_location();
}

/*
 * 函数名: cmd_map_select
 * 入参: argc 参数数量，argv 参数列表，argv[1] 为搜索候选目的地序号，从 1 开始
 * 返回值: 0 表示命令发送成功，负值表示参数错误或 TCP 未连接
 */
int rvt_map_cmd_select(int argc, char *argv[])
{
    int select_num;

    if (argc != 2) {
        RVT_LOGI(RVT_MAP_LOG_TAG, "usage: map_select <num>");
        RVT_LOGI(RVT_MAP_LOG_TAG, "example: map_select 1");
        RVT_LOGI(RVT_MAP_LOG_TAG, "example: map_select 2");
        return -1;
    }

    select_num = atoi(argv[1]);
    if (select_num <= 0) {
        RVT_LOGI(RVT_MAP_LOG_TAG, "usage: map_select <num>");
        RVT_LOGI(RVT_MAP_LOG_TAG, "example: map_select 1");
        return -1;
    }

    return rvt_map_select_destination(select_num);
}

/*
 * 函数名: cmd_map_dest
 * 入参: argc 参数数量，argv 参数列表，argv[1] 纬度E7，argv[2] 经度E7，argv[3..] 可选名称
 * 返回值: 0 表示命令发送成功，负值表示参数错误或 TCP 未连接
 */
int rvt_map_cmd_dest(int argc, char *argv[])
{
    char name[RVT_MAP_CMD_TEXT_MAX];
    const char *name_ptr = RVT_NULL;

    if (argc < 3) {
        RVT_LOGI(RVT_MAP_LOG_TAG, "usage: map_dest <lat_e7> <lon_e7> [name]");
        RVT_LOGI(RVT_MAP_LOG_TAG, "example: map_dest 320887000 1187969000 南京站");
        return -1;
    }

    if (argc > 3 && build_text_arg(argc, argv, 3, name, sizeof(name)) == 0)
        name_ptr = name;

    return rvt_map_set_nav_destination((int32_t)strtol(argv[1], RVT_NULL, 10),
                                       (int32_t)strtol(argv[2], RVT_NULL, 10),
                                       name_ptr);
}

/*
 * 函数名: cmd_map_route
 * 入参: argc 参数数量，argv 参数列表
 * 返回值: 0 表示路线规划命令发送成功，负值表示参数错误或 TCP 未连接
 */
int rvt_map_cmd_route(int argc, char *argv[])
{
    if (argc != 1) {
        RVT_LOGI(RVT_MAP_LOG_TAG, "usage: map_route");
        RVT_LOGI(RVT_MAP_LOG_TAG, "example: map_route");
        return -1;
    }

    return rvt_map_plan_route();
}

/*
 * 函数名: cmd_map_nav_start
 * 入参: argc 参数数量，argv 参数列表
 * 返回值: 0 表示命令发送成功，负值表示参数错误或 TCP 未连接
 */
int rvt_map_cmd_nav_start(int argc, char *argv[])
{
    if (argc != 1) {
        RVT_LOGI(RVT_MAP_LOG_TAG, "usage: map_nav_start");
        RVT_LOGI(RVT_MAP_LOG_TAG, "example: map_nav_start");
        return -1;
    }

    return rvt_map_nav_start();
}

/*
 * 函数名: cmd_map_nav_stop
 * 入参: argc 参数数量，argv 参数列表
 * 返回值: 0 表示命令发送成功，负值表示参数错误或 TCP 未连接
 */
int rvt_map_cmd_nav_stop(int argc, char *argv[])
{
    if (argc != 1) {
        RVT_LOGI(RVT_MAP_LOG_TAG, "usage: map_nav_stop");
        RVT_LOGI(RVT_MAP_LOG_TAG, "example: map_nav_stop");
        return -1;
    }

    return rvt_map_nav_stop();
}

/*
 * 函数名: cmd_map_speed
 * 入参: argc 参数数量，argv 参数列表，argv[1] 当前车速 km/h
 * 返回值: 0 表示命令发送成功，负值表示参数错误或 TCP 未连接
 */
int rvt_map_cmd_speed(int argc, char *argv[])
{
    if (argc != 2) {
        RVT_LOGI(RVT_MAP_LOG_TAG, "usage: map_speed <kmh>");
        RVT_LOGI(RVT_MAP_LOG_TAG, "example: map_speed 35.5");
        return -1;
    }

    return rvt_map_set_vehicle_speed((float)atof(argv[1]));
}

/*
 * 函数名: cmd_map_direction
 * 入参: argc 参数数量，argv 参数列表，argv[1] 为 route_up 或 north_up
 * 返回值: 0 表示命令发送成功，负值表示参数错误或 TCP 未连接
 */
int rvt_map_cmd_direction(int argc, char *argv[])
{
    if (argc != 2) {
        RVT_LOGI(RVT_MAP_LOG_TAG, "usage: map_direction <route_up|north_up>");
        RVT_LOGI(RVT_MAP_LOG_TAG, "example: map_direction route_up");
        return -1;
    }

    if (strcmp(argv[1], "route_up") == 0)
        return rvt_map_set_direction(RVT_MAP_DIRECTION_ROUTE_UP);
    if (strcmp(argv[1], "north_up") == 0)
        return rvt_map_set_direction(RVT_MAP_DIRECTION_NORTH_UP);

    RVT_LOGI(RVT_MAP_LOG_TAG, "usage: map_direction <route_up|north_up>");
    RVT_LOGI(RVT_MAP_LOG_TAG, "example: map_direction north_up");
    return -1;
}

#ifdef RT_USING_FINSH
#include <msh.h>
/*
 * 地图调试 MSH 命令:
 *   map_locate 南京站       // 只搜索并设置目的地
 *   map_go 南京南站         // 启动投屏服务，搜索目的地并规划路线
 *   map_go home             // 优先匹配手机收藏别名“家”
 *   map_go primary school   // 优先匹配手机收藏别名“小学”
 *   map_go gongsi           // 优先匹配手机收藏别名“公司”
 *   map_show_location       // 启动投屏服务，只显示地图和定位
 *   map_select 2            // 在搜索候选列表中选择第 2 个目的地
 *   map_route              // 对当前目的地规划路线
 *   map_nav_start          // 当前路线已规划后开始导航
 *   map_nav_stop           // 退出导航
 *   map_speed 35.5         // 设置车速 km/h
 *   map_direction route_up // 设置地图方向
 */
MSH_CMD_EXPORT_ALIAS(rvt_map_cmd_locate, map_locate,
                     search map destination example map_locate NanjingStation);
MSH_CMD_EXPORT_ALIAS(rvt_map_cmd_go, map_go,
                     prepare map navigation example map_go NanjingSouthStation);
MSH_CMD_EXPORT_ALIAS(rvt_map_cmd_show_location, map_show_location,
                     show map current location example map_show_location);
MSH_CMD_EXPORT_ALIAS(rvt_map_cmd_select, map_select,
                     select map search result example map_select 2);
MSH_CMD_EXPORT_ALIAS(rvt_map_cmd_dest, map_dest,
                     set map destination by e7 coordinate example map_dest 320887000 1187969000);
MSH_CMD_EXPORT_ALIAS(rvt_map_cmd_route, map_route,
                     plan route example map_route);
MSH_CMD_EXPORT_ALIAS(rvt_map_cmd_nav_start, map_nav_start,
                     start navigation example map_nav_start);
MSH_CMD_EXPORT_ALIAS(rvt_map_cmd_nav_stop, map_nav_stop,
                     stop navigation example map_nav_stop);
MSH_CMD_EXPORT_ALIAS(rvt_map_cmd_speed, map_speed,
                     set vehicle speed example map_speed 35.5);
MSH_CMD_EXPORT_ALIAS(rvt_map_cmd_direction, map_direction,
                     set map direction example map_direction route_up);
#endif
