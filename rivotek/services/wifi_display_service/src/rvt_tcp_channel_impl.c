#ifndef RVT_TCP_CHANNEL_IMPL_ROLE_INCLUDED
#error "rvt_tcp_channel_impl.c is included by rvt_tcp_server.c or rvt_tcp_client.c; do not compile it directly"
#endif

#include "rvt_wifi_display_service.h"
#include "rvt_wifi_display_network_control.h"
#include "rvt_map_api.h"
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#if RVT_TCP_ROLE_CLIENT
#include <arpa/inet.h>
#endif

#ifndef RVT_TCP_CLIENT_ASYNC_CONNECT
#define RVT_TCP_CLIENT_ASYNC_CONNECT 0
#endif

#if RVT_TCP_CLIENT_NONBLOCK_CONNECT && !RVT_TCP_CLIENT_ASYNC_CONNECT && !defined(KERNEL_RTTHREAD) && !defined(RT_USING_LWIP)
static int tcp_client_socket_created_nonblock = 0;
#endif
#if !defined(KERNEL_RTTHREAD) && !defined(RT_USING_LWIP)
#include <sys/select.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#endif

#define TCP_BUF_SIZE        256
#define HEARTBEAT_STR       "HEARTBEAT"
#define HEARTBEAT_TIMEOUT   8   // 8秒无数据断开
#define STAT_INTERVAL_MS    5000
#define TCP_START_WAIT_MS   2000
#define TCP_ACCEPT_IDLE_LOG_MS 5000
#define TCP_CLIENT_RETRY_MS 1000
#define TCP_CLIENT_CONNECT_LOG_MS 1000
#define TCP_IDLE_RECV_LOG_MS 3000
#define TCP_RECV_IDLE_SLEEP_MS 20
#define TCP_SEND_TRANSIENT_RETRY_MS 10
#define TCP_SEND_TRANSIENT_RETRY_LIMIT_MS 2000
#define RVT_TCP_LISTEN_BACKLOG  4
#define TCP_THREAD_PRIORITY 20
#define TCP_LISTEN_PENDING  0
#define TCP_LISTEN_READY    1
#define TCP_LISTEN_FAILED   -1

#if RVT_TCP_ROLE_CLIENT
static void tcp_client_stage_mark(const char *stage)
{
    if (stage)
        write(1, stage, strlen(stage));
}
#endif

#ifndef RVT_TCP_CLIENT_PRECONNECT_SETUP
#define RVT_TCP_CLIENT_PRECONNECT_SETUP 0
#endif

#if RVT_SOCKET_AVOID_POLL && defined(MSG_DONTWAIT)
#define TCP_RECV_FLAGS MSG_DONTWAIT
#else
#define TCP_RECV_FLAGS 0
#endif

/* TCP 服务线程运行标志，stop 时置 0 并关闭 socket 唤醒阻塞调用。 */
static int tcp_running = 0;
/* 定时触发 STAT 丢包率上报，实际发送放在线程上下文中完成。 */
static void *stat_timer = RVT_NULL;
static rvt_thread_t tcp_tid = RVT_NULL;
/* 多个业务接口可能同时通过 TCP 发命令，发送时需要串行化。 */
static rvt_mutex_t tcp_send_mutex = RVT_NULL;
static int tcp_listen_fd = -1;
/* client 角色 connect 阶段的临时 socket，用于 stop 时打断阻塞 connect。 */
static int tcp_connect_fd = -1;
/* 已接入但未必收到首个 HEARTBEAT 的 socket，用于 stop 时统一唤醒 recv。 */
static int tcp_active_fd = -1;
/* 当前手机端连接，用于地图 API 等外部调用直接发送 TCP 命令。 */
static int current_client_fd = -1;
/* init 等待监听线程进入 ready/failed，避免 start 返回过早。 */
static volatile int tcp_listen_state = TCP_LISTEN_PENDING;
static volatile int stat_pending = 0;
#if RVT_TCP_ROLE_CLIENT && !RVT_TCP_CLIENT_ASYNC_CONNECT
static volatile int tcp_connect_watchdog_running = 0;
static volatile int tcp_connect_watchdog_done = 0;
static volatile int tcp_connect_watchdog_fd = -1;
static volatile int tcp_connect_watchdog_port = 0;
static char tcp_connect_watchdog_peer[32];
static char tcp_connect_watchdog_local[32];
static rvt_tick_t tcp_connect_watchdog_start = 0;
#endif

static void tcp_handle_connected_socket(int client_fd);
#if RVT_TCP_ROLE_CLIENT
static void tcp_client_abort_connect_socket(int fd, const char *reason);
#endif
/*
 * 函数名: send_all
 * 入参: fd TCP 客户端 socket 描述符，data 待发送数据，len 待发送长度
 * 返回值: 0 表示全部发送成功，负值表示发送失败
 */
static int send_all(int fd, const char *data, int len)
{
    int sent = 0;
    int ret = 0;
    int transient_wait_ms = 0;

    if (tcp_send_mutex)
        rvt_mutex_take(tcp_send_mutex, RVT_WAIT_FOREVER);

    while (sent < len) {
        int n = send(fd, data + sent, len - sent, 0);
        if (n > 0) {
            sent += n;
            transient_wait_ms = 0;
            continue;
        }

        if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) &&
            tcp_running && transient_wait_ms < TCP_SEND_TRANSIENT_RETRY_LIMIT_MS) {
            if (transient_wait_ms == 0) {
                RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                         "tcp: send transient wait fd=%d sent=%d/%d errno=%d",
                         fd, sent, len, errno);
            }
            rvt_thread_mdelay(TCP_SEND_TRANSIENT_RETRY_MS);
            transient_wait_ms += TCP_SEND_TRANSIENT_RETRY_MS;
            continue;
        }

        if (n <= 0) {
            RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                     "tcp: send failed fd=%d sent=%d/%d n=%d errno=%d",
                     fd, sent, len, n, errno);
            ret = -1;
            break;
        }
    }

    if (tcp_send_mutex)
        rvt_mutex_release(tcp_send_mutex);

    return ret;
}

int rvt_tcp_channel_send_command(const char *cmd)
{
    int fd;

    if (!cmd)
        return -1;

    /* 没有手机连接时，地图控制命令会返回失败，由调用方决定是否重试。 */
    fd = current_client_fd;
    if (fd < 0)
        return -1;

    return send_all(fd, cmd, strlen(cmd));
}

int rvt_tcp_channel_has_client(void)
{
    return tcp_running && current_client_fd >= 0;
}

/*
 * 函数名: send_capability
 * 入参: fd TCP 客户端 socket 描述符
 * 返回值: 无
 */
static void send_capability(int fd)
{
    char cmd[128];
    struct rvt_display_caps display_caps;

    if (rvt_wifi_display_get_caps(&display_caps) != 0) {
        memset(&display_caps, 0, sizeof(display_caps));
    }
    /* CAP 用于手机端选择编码格式和输出尺寸，连接建立后只需发送一次。 */
    snprintf(cmd, sizeof(cmd), "CAP %d %d %d %d %lu\r\n",
             display_caps.width,
             display_caps.height,
             display_caps.width_mm,
             display_caps.height_mm,
             (unsigned long)display_caps.codec_mask);
    if (send_all(fd, cmd, strlen(cmd)) != 0) {
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "tcp: send capability failed, errno=%d", errno);
    } else {
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "tcp: send CAP %dx%d mm=%dx%d codec=0x%lx",
                 display_caps.width, display_caps.height,
                 display_caps.width_mm, display_caps.height_mm,
                 (unsigned long)display_caps.codec_mask);
    }
}

/*
 * 函数名: send_resolution
 * 入参: fd TCP 客户端 socket 描述符，width 数据流宽度，height 数据流高度
 * 返回值: 无
 */
static void send_resolution(int fd, int width, int height)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "RES %d %d\r\n", width, height);
    if (send_all(fd, cmd, strlen(cmd)) != 0) {
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "tcp: send resolution failed, errno=%d", errno);
    } else {
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "tcp: send RES %d %d", width, height);
    }
}

/*
 * 函数名: send_rotation
 * 入参: fd TCP 客户端 socket 描述符，rotation 旋转角度
 * 返回值: 无
 */
static void send_rotation(int fd, int rotation)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "ROT %d\r\n", rotation);
    if (send_all(fd, cmd, strlen(cmd)) != 0) {
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "tcp: send rotation failed, errno=%d", errno);
    } else {
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "tcp: send ROT %d", rotation);
    }
}

/*
 * 函数名: send_stream_config
 * 入参: fd TCP 客户端 socket 描述符
 * 返回值: 无
 */
static void send_stream_config(int fd)
{
    struct rvt_wifi_display_stream_config config;

    if (rvt_wifi_display_get_stream_config(&config) != 0)
        return;

    /* RES/ROT 是当前数据流配置，手机端据此重建编码器或调整画面。 */
    send_resolution(fd, config.width, config.height);
    send_rotation(fd, config.rotation);
}

/*
 * 函数名: send_statistics
 * 入参: fd TCP 客户端 socket 描述符
 * 返回值: 无
 */
static void send_statistics(int fd)
{
    uint32_t total, lost;
    get_udp_statistics(&total, &lost);
    float loss = total > 0 ? (float)lost / total : 0.0f;
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "STAT %.2f %lu %lu\r\n", loss,
             (unsigned long)lost, (unsigned long)total);
    if (send_all(fd, cmd, strlen(cmd)) != 0)
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG, "tcp: send statistics failed");
}

/*
 * 函数名: stat_timer_cb
 * 入参: param 定时器用户参数，当前未使用
 * 返回值: 无
 */
static void stat_timer_cb(void *param)
{
    /* 定时器回调只置位，避免在 timer 上下文里直接访问 socket。 */
    if (current_client_fd > 0)
        stat_pending = 1;
}

/*
 * 函数名: handle_nav_turn_command
 * 入参: line 手机端回传的 NAV_TURN 行，格式为 NAV_TURN <distance> <direction> <text>
 * 返回值: 无
 */
static void handle_nav_turn_command(const char *line)
{
    const char *p;
    char *end;
    char direction[32];
    int direction_len = 0;
    int distance_m;

    if (!line)
        return;

    p = line + strlen("NAV_TURN");
    while (*p == ' ')
        p++;

    distance_m = (int)strtol(p, &end, 10);
    if (end == p) {
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG, "map: invalid NAV_TURN command: %s", line);
        return;
    }

    p = end;
    while (*p == ' ')
        p++;

    while (p[direction_len] != '\0' && p[direction_len] != ' ' &&
           direction_len < (int)sizeof(direction) - 1) {
        direction[direction_len] = p[direction_len];
        direction_len++;
    }
    direction[direction_len] = '\0';

    p += direction_len;
    while (*p == ' ')
        p++;

    rvt_map_handle_turn_prompt(distance_m, direction, p);
}

/*
 * 函数名: handle_nav_match_command
 * 入参: line 手机端回传的 NAV_MATCH 行，格式为 NAV_MATCH <status> <text>
 * 返回值: 无
 */
static void handle_nav_match_command(const char *line)
{
    const char *p;
    char status[16];
    int status_len = 0;

    if (!line)
        return;

    p = line + strlen("NAV_MATCH");
    while (*p == ' ')
        p++;

    while (p[status_len] != '\0' && p[status_len] != ' ' &&
           status_len < (int)sizeof(status) - 1) {
        status[status_len] = p[status_len];
        status_len++;
    }
    status[status_len] = '\0';

    p += status_len;
    while (*p == ' ')
        p++;

    rvt_map_handle_navigation_match(status, p);
}

/*
 * 函数名: handle_nav_status_command
 * 入参: line 手机端回传的 NAV_STATUS 行，格式为 NAV_STATUS <event> <status> <text>
 * 返回值: 无
 */
static void handle_nav_status_command(const char *line)
{
    const char *p;
    char event[32];
    char status[16];
    int event_len = 0;
    int status_len = 0;

    if (!line)
        return;

    p = line + strlen("NAV_STATUS");
    while (*p == ' ')
        p++;

    while (p[event_len] != '\0' && p[event_len] != ' ' &&
           event_len < (int)sizeof(event) - 1) {
        event[event_len] = p[event_len];
        event_len++;
    }
    event[event_len] = '\0';

    p += event_len;
    while (*p == ' ')
        p++;

    while (p[status_len] != '\0' && p[status_len] != ' ' &&
           status_len < (int)sizeof(status) - 1) {
        status[status_len] = p[status_len];
        status_len++;
    }
    status[status_len] = '\0';

    p += status_len;
    while (*p == ' ')
        p++;

    rvt_map_handle_navigation_status(event, status, p);
}

static void tcp_handle_connected_socket(int client_fd)
{
    char buf[TCP_BUF_SIZE];
    char line_buf[TCP_BUF_SIZE];
    int line_len = 0;
    int line_count = 0;
    int heartbeat_count = 0;
    int first_recv_wait_logged = 0;
    int first_recv_attempt_logged = 0;
    rvt_tick_t last_idle_log;

    tcp_active_fd = client_fd;
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "tcp: connected socket active, fd=%d", client_fd);
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "tcp: connected socket entering recv loop fd=%d tcp_running=%d",
             client_fd, tcp_running);
    rvt_wifi_display_log_network_state("connected");
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "tcp: connected socket ready fd=%d active=%d current=%d running=%d",
             client_fd, tcp_active_fd, current_client_fd, tcp_running);

#if RVT_SOCKET_AVOID_POLL
    {
        struct timeval recv_timeout;
        int opt_ret;
        recv_timeout.tv_sec = 1;
        recv_timeout.tv_usec = 0;
        /*
         * 用接收超时驱动心跳检查，避免在 NuttX/openVela 上走
         * select/poll socket 通知链路。
         */
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "tcp: set recv timeout start fd=%d", client_fd);
        opt_ret = setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO,
                             &recv_timeout, sizeof(recv_timeout));
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "tcp: set recv timeout done fd=%d ret=%d errno=%d",
                 client_fd, opt_ret, errno);
    }
#endif

    /* 手机刚连上时先下发能力和投屏参数，再接收心跳/控制命令。 */
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "tcp: initial capability send begin fd=%d", client_fd);
    send_capability(client_fd);
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "tcp: initial capability send done fd=%d", client_fd);
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "tcp: initial stream config send begin fd=%d", client_fd);
    send_stream_config(client_fd);
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "tcp: initial stream config send done fd=%d", client_fd);

    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "tcp: recv loop ready fd=%d flags=0x%x avoid_poll=%d",
             client_fd, TCP_RECV_FLAGS, RVT_SOCKET_AVOID_POLL);

    /*
     * 此时连接可能只是手机端发现阶段的 TCP 探测。
     * 等收到 HEARTBEAT 后再开放给地图 API，避免控制命令发到短连接。
     */
    rvt_tick_t last_heartbeat = rvt_tick_get();
    int pending_flushed = 0;
    stat_pending = 0;
    last_idle_log = last_heartbeat;

    while (tcp_running)
    {
#if RVT_SOCKET_AVOID_POLL == 0
        fd_set read_set;
        struct timeval timeout;
        int ret;
#endif
        int n;

        if (stat_pending) {
            stat_pending = 0;
            RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                     "tcp: stat pending send fd=%d", client_fd);
            send_statistics(client_fd);
        }

#if RVT_SOCKET_AVOID_POLL
        if (!first_recv_wait_logged) {
            RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                     "tcp: recv wait first packet fd=%d flags=0x%x",
                     client_fd, TCP_RECV_FLAGS);
            first_recv_wait_logged = 1;
        }
        if (!first_recv_attempt_logged) {
            RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                     "tcp: recv first attempt fd=%d flags=0x%x current=%d active=%d running=%d",
                     client_fd, TCP_RECV_FLAGS, current_client_fd, tcp_active_fd,
                     tcp_running);
            first_recv_attempt_logged = 1;
        }
        n = recv(client_fd, buf, sizeof(buf), TCP_RECV_FLAGS);
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK ||
                      errno == EINTR || errno == ETIMEDOUT)) {
            rvt_tick_t now = rvt_tick_get();

            if (now - last_idle_log >=
                rvt_tick_from_millisecond(TCP_IDLE_RECV_LOG_MS)) {
                RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                         "tcp: recv idle fd=%d errno=%d heartbeat_age=%lu ms current=%d",
                         client_fd, errno,
                         (unsigned long)(now - last_heartbeat),
                         current_client_fd);
                last_idle_log = now;
            }
            if (rvt_tick_get() - last_heartbeat >
                rvt_tick_from_millisecond(HEARTBEAT_TIMEOUT * 1000)) {
                if (current_client_fd == client_fd) {
                    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                             "tcp: heartbeat timeout fd=%d count=%d",
                             client_fd, heartbeat_count);
                    /* 心跳超时认为投屏链路断开，统一退出投屏业务。 */
                    rvt_wifi_display_stop();
                }
                break;
            }
            rvt_thread_mdelay(TCP_RECV_IDLE_SLEEP_MS);
            continue;
        }
#else
        /* select 1s 超时用于同时处理心跳超时和周期统计。 */
        FD_ZERO(&read_set);
        FD_SET(client_fd, &read_set);
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        ret = select(client_fd + 1, &read_set, RVT_NULL, RVT_NULL, &timeout);
        if (ret == 0) {
            if (rvt_tick_get() - last_heartbeat >
                rvt_tick_from_millisecond(HEARTBEAT_TIMEOUT * 1000)) {
                if (current_client_fd == client_fd) {
                    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG, "tcp: heartbeat timeout");
                    /* 心跳超时认为投屏链路断开，统一退出投屏业务。 */
                    rvt_wifi_display_stop();
                }
                break;
            }
            continue;
        }

        if (ret < 0) {
            RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG, "tcp: select failed");
            if (current_client_fd == client_fd)
                rvt_wifi_display_stop();
            break;
        }

        n = recv(client_fd, buf, sizeof(buf), 0);
#endif

        if (n == 0) {
            RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                     "tcp: phone disconnected fd=%d n=0 heartbeat=%d current=%d active=%d",
                     client_fd, heartbeat_count, current_client_fd, tcp_active_fd);
            if (current_client_fd == client_fd) {
                RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                         "tcp: phone disconnected fd=%d heartbeat=%d",
                         client_fd, heartbeat_count);
                /* 手机主动断开或 WiFi 断链时，车机回到初始投屏状态。 */
                rvt_wifi_display_stop();
            }
            break;
        }

        if (n < 0) {
            RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                     "tcp: recv error fd=%d errno=%d heartbeat=%d current=%d active=%d",
                     client_fd, errno, heartbeat_count, current_client_fd, tcp_active_fd);
            if (current_client_fd == client_fd)
                rvt_wifi_display_stop();
            break;
        }

        if (heartbeat_count == 0) {
            RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                     "tcp: recv first data fd=%d len=%d first=0x%02X",
                     client_fd, n, (unsigned char)buf[0]);
        }

        /* 按行解析指令 */
        for (int i = 0; i < n; i++) {
            char c = buf[i];
            if (c == '\n') {
                line_buf[line_len] = '\0';
                line_count++;
                if (line_count <= 6 || strncmp(line_buf, HEARTBEAT_STR, 9) == 0) {
                    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                             "tcp: recv line #%d fd=%d len=%d text=%s",
                             line_count, client_fd, line_len, line_buf);
                }
                if (strncmp(line_buf, HEARTBEAT_STR, 9) == 0) {
                    const char *ack = "HEARTBEAT_ACK\r\n";
                    /* 手机端发 HEARTBEAT，车机回复 ACK 并刷新断连计时。 */
                    heartbeat_count++;
                    last_heartbeat = rvt_tick_get();
                    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                             "tcp: heartbeat recv fd=%d count=%d current=%d active=%d line=%s",
                             client_fd, heartbeat_count, current_client_fd,
                             tcp_active_fd, line_buf);
                    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                             "tcp: heartbeat ack send begin fd=%d count=%d",
                             client_fd, heartbeat_count);
                    if (send_all(client_fd, ack, strlen(ack)) != 0) {
                        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                                 "tcp: heartbeat ack send failed, errno=%d", errno);
                        rvt_wifi_display_stop();
                        break;
                    }
                    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                             "tcp: heartbeat ack send done fd=%d count=%d current=%d active=%d",
                             client_fd, heartbeat_count, current_client_fd,
                             tcp_active_fd);
                    if (heartbeat_count <= 5 || heartbeat_count % 10 == 0) {
                        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                                 "tcp: HEARTBEAT #%d acked", heartbeat_count);
                    }
                    if (current_client_fd < 0) {
                        current_client_fd = client_fd;
                        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                                 "tcp: current client bound by heartbeat fd=%d",
                                 client_fd);
                    }
                    if (!pending_flushed) {
                        /*
                         * 手机端可能先用短连接探测 6004 端口。
                         * 等收到 HEARTBEAT 后再补发地图命令，避免探测连接吃掉 NAV_GO。
                         */
                        rvt_map_flush_pending_commands();
                        pending_flushed = 1;
                        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                                 "tcp: pending commands flushed after heartbeat fd=%d",
                                 client_fd);
                    }
                } else if (strcmp(line_buf, "STREAM_STOP") == 0) {
                    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG, "tcp: stream stop requested");
                    /* 切换投屏模式时只清画面缓存，不关闭 TCP/UDP 通道。 */
                    udp_receiver_clear_stream();
                } else if (strcmp(line_buf, "NAV_ARRIVED") == 0) {
                    rvt_map_handle_navigation_arrived();
                } else if (strncmp(line_buf, "NAV_STATUS ", 11) == 0) {
                    handle_nav_status_command(line_buf);
                } else if (strncmp(line_buf, "NAV_MATCH ", 10) == 0) {
                    handle_nav_match_command(line_buf);
                } else if (strncmp(line_buf, "NAV_TURN ", 9) == 0) {
                    handle_nav_turn_command(line_buf);
                }
                line_len = 0;
            } else if (c != '\r') {
                if (line_len < TCP_BUF_SIZE - 1) {
                    line_buf[line_len++] = c;
                }
            }
        }
    }

    if (current_client_fd == client_fd)
        current_client_fd = -1;
    if (tcp_active_fd == client_fd)
        tcp_active_fd = -1;
    rvt_socket_close(client_fd);
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "tcp: connected socket closed, fd=%d heartbeat=%d",
             client_fd, heartbeat_count);
}

#if RVT_TCP_ROLE_CLIENT
#if defined(KERNEL_RTTHREAD) || defined(RT_USING_LWIP)
extern int inet_aton(const char *cp, struct in_addr *inp);
#endif

#if RVT_TCP_CLIENT_ASYNC_CONNECT
struct tcp_client_async_connect_ctx {
    int fd;
    struct sockaddr_storage addr;
    socklen_t addr_len;
    volatile int done;
    int ret;
    int err;
    rvt_tick_t start_tick;
};

static void tcp_client_async_connect_thread(void *arg)
{
    struct tcp_client_async_connect_ctx *ctx =
        (struct tcp_client_async_connect_ctx *)arg;
    int ret;
    int err;

    if (!ctx)
        return;

    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "tcp: async connect worker enter fd=%d", ctx->fd);
    errno = 0;
    ret = connect(ctx->fd, (struct sockaddr *)&ctx->addr, ctx->addr_len);
    err = errno;
    ctx->ret = ret;
    ctx->err = err;
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "tcp: async worker return fd=%d ret=%d", ctx->fd, ret);
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "tcp: async worker errno=%d cost=%lu",
             err, (unsigned long)(rvt_tick_get() - ctx->start_tick));
    ctx->done = 1;
}
#endif

static int tcp_client_socket_is_connected(int fd)
{
    struct sockaddr_in peer;
    socklen_t peer_len = sizeof(peer);

    memset(&peer, 0, sizeof(peer));
    return getpeername(fd, (struct sockaddr *)&peer, &peer_len) == 0;
}

#if !RVT_TCP_CLIENT_ASYNC_CONNECT
static int tcp_client_connect_pending_errno(int err)
{
    return err == EINPROGRESS || err == EALREADY ||
           err == EWOULDBLOCK || err == EAGAIN;
}

#if RVT_TCP_CLIENT_PRECONNECT_SETUP
static void tcp_client_log_socket_addrs(int fd, const char *stage)
{
    struct sockaddr_in local;
    struct sockaddr_in peer;
    socklen_t local_len = sizeof(local);
    socklen_t peer_len = sizeof(peer);
    char local_ip[32];
    char peer_ip[32];
    int local_port = 0;
    int peer_port = 0;
    int local_ret;
    int peer_ret;

    memset(&local, 0, sizeof(local));
    memset(&peer, 0, sizeof(peer));
    memset(local_ip, 0, sizeof(local_ip));
    memset(peer_ip, 0, sizeof(peer_ip));

    local_ret = getsockname(fd, (struct sockaddr *)&local, &local_len);
    peer_ret = getpeername(fd, (struct sockaddr *)&peer, &peer_len);
    if (local_ret == 0) {
        snprintf(local_ip, sizeof(local_ip), "%s", inet_ntoa(local.sin_addr));
        local_port = ntohs(local.sin_port);
    }
    if (peer_ret == 0) {
        snprintf(peer_ip, sizeof(peer_ip), "%s", inet_ntoa(peer.sin_addr));
        peer_port = ntohs(peer.sin_port);
    }

    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "tcp: socket addr[%s] fd=%d local=%s:%d ret=%d peer=%s:%d ret=%d errno=%d",
             stage ? stage : "",
             fd,
             local_ret == 0 ? local_ip : "-",
             local_port,
             local_ret,
             peer_ret == 0 ? peer_ip : "-",
             peer_port,
             peer_ret,
             errno);
}

static int tcp_client_bind_local_ip(int fd)
{
    char local_ip[32];
    struct sockaddr_in local_addr;

    if (rvt_wifi_display_get_local_ip(local_ip, sizeof(local_ip)) != 0 ||
        local_ip[0] == '\0') {
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "tcp: local bind skipped, no local ip");
        return 0;
    }

    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_port = 0;
    if (inet_aton(local_ip, &local_addr.sin_addr) == 0) {
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "tcp: local bind skipped, invalid local ip %s", local_ip);
        return 0;
    }

    if (bind(fd, (struct sockaddr *)&local_addr, sizeof(local_addr)) != 0) {
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "tcp: bind local %s failed fd=%d errno=%d",
                 local_ip, fd, errno);
        return -1;
    }

    snprintf(tcp_connect_watchdog_local,
             sizeof(tcp_connect_watchdog_local), "%s", local_ip);
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "tcp: bound local %s fd=%d", local_ip, fd);
    tcp_client_log_socket_addrs(fd, "after_bind");
    return 0;
}

static void tcp_client_set_debug_timeouts(int fd)
{
    struct timeval timeout;

    timeout.tv_sec = 5;
    timeout.tv_usec = 0;

    if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO,
                   &timeout, sizeof(timeout)) != 0) {
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "tcp: set SO_SNDTIMEO failed fd=%d errno=%d", fd, errno);
    } else {
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "tcp: set SO_SNDTIMEO 5000ms fd=%d", fd);
    }

    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO,
                   &timeout, sizeof(timeout)) != 0) {
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "tcp: set SO_RCVTIMEO failed fd=%d errno=%d", fd, errno);
    }
}
#endif

static void tcp_client_connect_watchdog(void *arg)
{
    int last_sec = -1;

    (void)arg;

    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "tcp: connect watchdog enter fd=%d peer=%s:%d running=%d done=%d",
             tcp_connect_watchdog_fd,
             tcp_connect_watchdog_peer[0] ? tcp_connect_watchdog_peer : "-",
             tcp_connect_watchdog_port,
             tcp_connect_watchdog_running,
             tcp_connect_watchdog_done);

    while (tcp_connect_watchdog_running && !tcp_connect_watchdog_done) {
        int elapsed_ms;
        int elapsed_sec;
        int fd;

        rvt_thread_mdelay(TCP_CLIENT_CONNECT_LOG_MS);
        if (!tcp_connect_watchdog_running || tcp_connect_watchdog_done)
            break;

        elapsed_ms = (int)(rvt_tick_get() - tcp_connect_watchdog_start);
        elapsed_sec = elapsed_ms / 1000;
        if (elapsed_sec == last_sec)
            continue;
        last_sec = elapsed_sec;
        fd = tcp_connect_watchdog_fd;

        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "tcp: connect waiting %d ms fd=%d local=%s peer=%s:%d running=%d",
                 elapsed_ms,
                 fd,
                 tcp_connect_watchdog_local[0] ? tcp_connect_watchdog_local : "-",
                 tcp_connect_watchdog_peer[0] ? tcp_connect_watchdog_peer : "-",
                 tcp_connect_watchdog_port,
                 tcp_running);
        rvt_wifi_display_log_network_state("connect_wait");
#if RVT_TCP_CLIENT_PRECONNECT_SETUP
        if (fd >= 0)
            tcp_client_log_socket_addrs(fd, "connect_wait");
#endif
        if (elapsed_ms >= RVT_TCP_CLIENT_CONNECT_TIMEOUT_MS) {
            RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                     "tcp: connect watchdog timeout fd=%d elapsed=%d",
                     fd, elapsed_ms);
            tcp_connect_watchdog_done = 1;
            tcp_connect_watchdog_running = 0;
            tcp_client_abort_connect_socket(fd, "watchdog timeout");
            break;
        }
    }

    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "tcp: connect watchdog exit running=%d done=%d fd=%d",
             tcp_connect_watchdog_running,
             tcp_connect_watchdog_done,
             tcp_connect_watchdog_fd);
}

static void tcp_client_start_connect_watchdog(int fd, const char *peer_ip, int port)
{
    rvt_thread_t tid;
    int startup_ret;

    tcp_connect_watchdog_done = 0;
    tcp_connect_watchdog_running = 1;
    tcp_connect_watchdog_fd = fd;
    tcp_connect_watchdog_port = port;
    tcp_connect_watchdog_start = rvt_tick_get();
    snprintf(tcp_connect_watchdog_peer,
             sizeof(tcp_connect_watchdog_peer), "%s", peer_ip ? peer_ip : "");

    tid = rvt_thread_create("tcpcwd", tcp_client_connect_watchdog, RVT_NULL,
                            RVT_TCP_THREAD_STACK, TCP_THREAD_PRIORITY, 10);
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "tcp: connect watchdog create tid=%p fd=%d peer=%s:%d",
             tid, fd, peer_ip ? peer_ip : "-", port);
    startup_ret = tid ? rvt_thread_startup(tid) : -1;
    if (!tid || startup_ret != 0) {
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "tcp: connect watchdog start failed ret=%d", startup_ret);
        tcp_connect_watchdog_running = 0;
    } else {
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "tcp: connect watchdog start ok tid=%p fd=%d", tid, fd);
    }
}
#endif

static void tcp_client_abort_connect_socket(int fd, const char *reason)
{
    if (fd < 0 || tcp_connect_fd != fd)
        return;

    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "tcp: abort connect socket fd=%d reason=%s",
             fd, reason ? reason : "-");
    tcp_connect_fd = -1;
    rvt_socket_close(fd);
}

#if !RVT_TCP_CLIENT_ASYNC_CONNECT
static void tcp_client_stop_connect_watchdog(void)
{
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "tcp: connect watchdog stop fd=%d running=%d done=%d",
             tcp_connect_watchdog_fd,
             tcp_connect_watchdog_running,
             tcp_connect_watchdog_done);
    tcp_connect_watchdog_done = 1;
    tcp_connect_watchdog_running = 0;
    tcp_connect_watchdog_fd = -1;
    tcp_connect_watchdog_port = 0;
}
#endif

static int tcp_client_connect_socket(int fd, const struct sockaddr *addr,
                                     socklen_t addr_len)
{
#if RVT_TCP_CLIENT_ASYNC_CONNECT
    struct tcp_client_async_connect_ctx *ctx;
    rvt_thread_t tid;
    int startup_ret;
    int wait_ms = 0;

    ctx = (struct tcp_client_async_connect_ctx *)rvt_malloc(sizeof(*ctx));
    if (!ctx) {
        errno = ENOMEM;
        return -1;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->fd = fd;
    ctx->addr_len = addr_len;
    ctx->err = EINPROGRESS;
    ctx->start_tick = rvt_tick_get();
    memcpy(&ctx->addr, addr, addr_len);

    tid = rvt_thread_create("tcpconn", tcp_client_async_connect_thread,
                            ctx, RVT_TCP_THREAD_STACK, TCP_THREAD_PRIORITY, 10);
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "tcp: async connect create tid=%p fd=%d", tid, fd);
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "tcp: async connect timeout=%d",
             RVT_TCP_CLIENT_CONNECT_TIMEOUT_MS);
    startup_ret = tid ? rvt_thread_startup(tid) : -1;
    if (!tid || startup_ret != 0) {
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "tcp: async connect start failed ret=%d", startup_ret);
        rvt_free(ctx);
        errno = EAGAIN;
        return -1;
    }

    while (tcp_running && wait_ms < RVT_TCP_CLIENT_CONNECT_TIMEOUT_MS) {
        if (ctx->done) {
            int ret = ctx->ret;
            int err = ctx->err;

            RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                     "tcp: async done fd=%d ret=%d", fd, ret);
            RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                     "tcp: async done errno=%d wait=%d", err, wait_ms);
            rvt_free(ctx);
            errno = err;
            return ret;
        }

        if (tcp_client_socket_is_connected(fd)) {
            RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                     "tcp: async connect observed connected fd=%d wait=%d",
                     fd, wait_ms);
            errno = 0;
            /* worker may still be blocked in connect(); leave ctx valid. */
            return 0;
        }

        if (wait_ms <= 500 || wait_ms % 1000 == 0) {
            int so_error = 0;
            socklen_t so_len = sizeof(so_error);
            int so_ret = getsockopt(fd, SOL_SOCKET, SO_ERROR,
                                    &so_error, &so_len);

            RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                     "tcp: async wait fd=%d ms=%d done=%d",
                     fd, wait_ms, ctx->done);
            RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                     "tcp: async so ret=%d err=%d running=%d",
                     so_ret, so_error, tcp_running);
            if (so_ret == 0 && so_error != 0) {
                /* worker may still be blocked in connect(); leave ctx valid. */
                errno = so_error;
                return -1;
            }
        }

        rvt_thread_mdelay(RVT_TCP_CLIENT_CONNECT_POLL_MS);
        wait_ms += RVT_TCP_CLIENT_CONNECT_POLL_MS;
    }

    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "tcp: async timeout fd=%d wait=%d", fd, wait_ms);
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "tcp: async timeout done=%d running=%d", ctx->done, tcp_running);
    /* worker may still be blocked in connect(); leave ctx valid. */
    errno = tcp_running ? ETIMEDOUT : EINTR;
    return -1;
#else
#if RVT_TCP_CLIENT_NONBLOCK_CONNECT
    int wait_ms = 0;
    int ret;
    int saved_errno;
#if !defined(KERNEL_RTTHREAD) && !defined(RT_USING_LWIP)
    if (!tcp_client_socket_created_nonblock) {
        if (rvt_socket_set_nonblocking(fd) != 0) {
            RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                     "tcp: set nonblock connect failed, errno=%d",
                     errno);
            return -1;
        }
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "tcp: set nonblock before connect ok fd=%d", fd);
    } else {
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "tcp: socket created nonblock, skip set fd=%d", fd);
    }

    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "tcp: nonblock connect begin fd=%d timeout=%d poll=%d",
             fd, RVT_TCP_CLIENT_CONNECT_TIMEOUT_MS,
             RVT_TCP_CLIENT_CONNECT_POLL_MS);
    errno = 0;
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "tcp: nonblock connect call fd=%d", fd);
    ret = connect(fd, addr, addr_len);
    saved_errno = errno;
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "tcp: nonblock connect first return fd=%d ret=%d errno=%d running=%d",
             fd, ret, saved_errno, tcp_running);
    if (ret == 0) {
        goto connected;
    }
    if (saved_errno == EISCONN && tcp_client_socket_is_connected(fd)) {
        goto connected;
    }
    if (!tcp_client_connect_pending_errno(saved_errno)) {
        errno = saved_errno;
        return -1;
    }
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "tcp: nonblock connect pending fd=%d errno=%d", fd, saved_errno);

    while (tcp_running && wait_ms < RVT_TCP_CLIENT_CONNECT_TIMEOUT_MS) {
        rvt_thread_mdelay(RVT_TCP_CLIENT_CONNECT_POLL_MS);
        wait_ms += RVT_TCP_CLIENT_CONNECT_POLL_MS;

        if (tcp_client_socket_is_connected(fd)) {
            RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                     "tcp: nonblock connect ready in %d ms", wait_ms);
            goto connected;
        }

        saved_errno = errno;
        if (wait_ms <= 500 || wait_ms % 1000 == 0) {
            int so_error = 0;
            socklen_t so_len = sizeof(so_error);
            int so_ret = getsockopt(fd, SOL_SOCKET, SO_ERROR,
                                    &so_error, &so_len);
            RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                     "tcp: nonblock connect wait fd=%d wait=%d peer_errno=%d so_ret=%d so_error=%d running=%d",
                     fd, wait_ms, saved_errno, so_ret, so_error,
                     tcp_running);
            if (so_ret == 0 && so_error != 0) {
                errno = so_error;
                return -1;
            }
        }
    }

    errno = tcp_running ? ETIMEDOUT : EINTR;
    return -1;

connected:
    if (rvt_socket_set_blocking(fd) != 0) {
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "tcp: restore blocking after connect failed fd=%d errno=%d",
                 fd, errno);
    } else {
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "tcp: restore blocking after connect ok fd=%d", fd);
    }
    errno = 0;
    return 0;
#else
    return connect(fd, addr, addr_len);
#endif
#else
    return connect(fd, addr, addr_len);
#endif
#endif
}

static int tcp_connect_to_phone_server(void)
{
    char peer_ip[32];
    struct sockaddr_in addr;
    int fd;
    int socket_type = SOCK_STREAM;
    int connect_ret;
    int connect_errno;
    rvt_tick_t connect_start;

#if RVT_TCP_CLIENT_NONBLOCK_CONNECT && !RVT_TCP_CLIENT_ASYNC_CONNECT && !defined(KERNEL_RTTHREAD) && !defined(RT_USING_LWIP) && defined(SOCK_NONBLOCK)
    socket_type |= SOCK_NONBLOCK;
    tcp_client_socket_created_nonblock = 1;
#else
#if RVT_TCP_CLIENT_NONBLOCK_CONNECT && !RVT_TCP_CLIENT_ASYNC_CONNECT && !defined(KERNEL_RTTHREAD) && !defined(RT_USING_LWIP)
    tcp_client_socket_created_nonblock = 0;
#endif
#endif

    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG, "tcp: resolve phone peer start");
    rvt_wifi_display_log_network_state("before_resolve_peer");
    if (rvt_wifi_display_get_tcp_peer_ip(peer_ip, sizeof(peer_ip)) != 0) {
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG, "tcp: resolve phone peer failed");
        return -1;
    }
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG, "tcp: phone peer ip %s", peer_ip);
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG, "tcp: client mode async=%d nonblock=%d",
             RVT_TCP_CLIENT_ASYNC_CONNECT, RVT_TCP_CLIENT_NONBLOCK_CONNECT);
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG, "tcp: client timeout=%d poll=%d setup=%d",
             RVT_TCP_CLIENT_CONNECT_TIMEOUT_MS,
             RVT_TCP_CLIENT_CONNECT_POLL_MS,
             RVT_TCP_CLIENT_PRECONNECT_SETUP);

    tcp_client_stage_mark("wifi_display: tcp stage socket-begin\n");
    fd = socket(AF_INET, socket_type, 0);
    tcp_client_stage_mark("wifi_display: tcp stage socket-end\n");
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG, "tcp: socket fd=%d errno=%d type=%d",
             fd, errno, socket_type);
    if (fd < 0) {
        RVT_LOGE(RVT_WIFI_DISPLAY_LOG_TAG, "tcp: client socket failed, errno=%d", errno);
        return -1;
    }
    tcp_connect_fd = fd;
#if !RVT_TCP_CLIENT_ASYNC_CONNECT
    tcp_connect_watchdog_local[0] = '\0';
#endif
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "tcp: client socket fd=%d", fd);
#if RVT_TCP_CLIENT_PRECONNECT_SETUP
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "tcp: pre-connect setup enter fd=%d", fd);
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "tcp: set debug timeouts start fd=%d", fd);
    tcp_client_set_debug_timeouts(fd);
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "tcp: set debug timeouts done fd=%d errno=%d", fd, errno);
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "tcp: bind local start fd=%d", fd);
    if (tcp_client_bind_local_ip(fd) != 0) {
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "tcp: bind local returned error fd=%d errno=%d", fd, errno);
    }
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "tcp: bind local done fd=%d errno=%d", fd, errno);
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "tcp: pre-connect setup leave fd=%d", fd);
#else
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "tcp: pre-connect timeout/bind skipped fd=%d", fd);
#endif
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "tcp: build peer sockaddr begin fd=%d", fd);
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT_TCP);
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "tcp: inet_aton peer start ip=%s fd=%d", peer_ip, fd);
    if (inet_aton(peer_ip, &addr.sin_addr) == 0) {
        RVT_LOGE(RVT_WIFI_DISPLAY_LOG_TAG, "tcp: invalid peer ip %s", peer_ip);
        if (tcp_connect_fd == fd)
            tcp_connect_fd = -1;
        rvt_socket_close(fd);
        return -1;
    }
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "tcp: inet_aton peer done ip=%s fd=%d", peer_ip, fd);

    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "tcp: connect phone %s:%d start", peer_ip, PORT_TCP);
#if RVT_TCP_CLIENT_PRECONNECT_SETUP
    tcp_client_log_socket_addrs(fd, "before_connect");
#endif
#if !RVT_TCP_CLIENT_ASYNC_CONNECT
    tcp_client_start_connect_watchdog(fd, peer_ip, PORT_TCP);
#endif
    connect_start = rvt_tick_get();
    connect_ret = tcp_client_connect_socket(fd, (struct sockaddr *)&addr,
                                            sizeof(addr));
    connect_errno = errno;
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "tcp: connect raw return fd=%d ret=%d errno=%d running=%d cost=%lu ms",
             fd, connect_ret, connect_errno, tcp_running,
             (unsigned long)(rvt_tick_get() - connect_start));
    if (connect_ret == 0) {
        /*
         * connect 已经成功后先把 fd 从 connect_fd 提升为 active_fd。
         * 避免 watchdog/stop 路径在竞态窗口里把已建立连接当成连接中 socket 关闭。
         */
        tcp_active_fd = fd;
        if (tcp_connect_fd == fd)
            tcp_connect_fd = -1;
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "tcp: connect promoted fd=%d active=%d connect=%d",
                 fd, tcp_active_fd, tcp_connect_fd);
    }
#if !RVT_TCP_CLIENT_ASYNC_CONNECT
    tcp_client_stop_connect_watchdog();
#endif
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "tcp: connect phone %s:%d return ret=%d errno=%d cost=%lu ms",
             peer_ip,
             PORT_TCP,
             connect_ret,
             connect_errno,
             (unsigned long)(rvt_tick_get() - connect_start));
#if RVT_TCP_CLIENT_PRECONNECT_SETUP
    tcp_client_log_socket_addrs(fd, "after_connect");
#endif
    if (connect_ret < 0) {
        errno = connect_errno;
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "tcp: connect phone %s:%d failed, errno=%d",
                 peer_ip, PORT_TCP, errno);
        if (tcp_connect_fd == fd) {
            tcp_client_abort_connect_socket(fd, "connect failed");
        } else {
            if (tcp_active_fd == fd)
                tcp_active_fd = -1;
            rvt_socket_close(fd);
        }
        return -1;
    }
    if (!tcp_running) {
        if (tcp_connect_fd == fd) {
            tcp_client_abort_connect_socket(fd, "stopped while connecting");
        } else {
            if (tcp_active_fd == fd)
                tcp_active_fd = -1;
            rvt_socket_close(fd);
        }
        return -1;
    }
    if (tcp_connect_fd == fd)
        tcp_connect_fd = -1;
    tcp_active_fd = fd;

    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG, "tcp: connected to phone %s:%d",
             peer_ip, PORT_TCP);
#if RVT_TCP_CLIENT_PRECONNECT_SETUP
    tcp_client_log_socket_addrs(fd, "connected");
#endif
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "tcp: connect phone %s:%d success, enter handler fd=%d",
             peer_ip, PORT_TCP, fd);
    return fd;
}

static void tcp_thread(void *arg)
{
    int connect_try = 0;

    (void)arg;

    tcp_listen_state = TCP_LISTEN_READY;
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "tcp: client role, connect phone server port %d", PORT_TCP);

    while (tcp_running) {
        int client_fd = tcp_connect_to_phone_server();
        connect_try++;
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "tcp: client connect try #%d result fd=%d running=%d",
                 connect_try, client_fd, tcp_running);
        if (client_fd < 0) {
            rvt_thread_mdelay(TCP_CLIENT_RETRY_MS);
            continue;
        }

        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "tcp: client connect try #%d handler start fd=%d",
                 connect_try, client_fd);
        tcp_handle_connected_socket(client_fd);
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "tcp: client connected handler returned fd=%d running=%d",
                 client_fd, tcp_running);
    }

    tcp_running = 0;
    tcp_tid = RVT_NULL;
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG, "tcp: thread exit");
}
#else

/*
 * 函数名: tcp_thread
 * 入参: arg 线程参数，当前未使用
 * 返回值: 无
 */
static void tcp_thread(void *arg)
{
    int client_fd;
    int reuse = 1;
    int last_accept_errno = 0;
    rvt_tick_t last_accept_log = 0;
    struct sockaddr_in addr;

    /*
     * R528/openVela 也使用普通 TCP socket 创建，再在 listen 成功后
     * 切到非阻塞 accept。这样可以避免部分 NuttX 版本在 socket 创建
     * 阶段携带 SOCK_NONBLOCK 时 listen 表状态异常，导致外部连接被 RST。
     */
    tcp_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_listen_fd < 0) {
        RVT_LOGE(RVT_WIFI_DISPLAY_LOG_TAG, "tcp: socket failed, errno=%d", errno);
        tcp_listen_state = TCP_LISTEN_FAILED;
        tcp_running = 0;
        tcp_tid = RVT_NULL;
        return;
    }

    if (setsockopt(tcp_listen_fd, SOL_SOCKET, SO_REUSEADDR,
                   &reuse, sizeof(reuse)) < 0) {
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG, "tcp: set reuseaddr failed, errno=%d", errno);
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT_TCP);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(tcp_listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        RVT_LOGE(RVT_WIFI_DISPLAY_LOG_TAG, "tcp: bind failed, errno=%d", errno);
        rvt_socket_close(tcp_listen_fd);
        tcp_listen_fd = -1;
        tcp_listen_state = TCP_LISTEN_FAILED;
        tcp_running = 0;
        tcp_tid = RVT_NULL;
        return;
    }

    if (listen(tcp_listen_fd, RVT_TCP_LISTEN_BACKLOG) < 0) {
        RVT_LOGE(RVT_WIFI_DISPLAY_LOG_TAG, "tcp: listen failed, errno=%d", errno);
        rvt_socket_close(tcp_listen_fd);
        tcp_listen_fd = -1;
        tcp_listen_state = TCP_LISTEN_FAILED;
        tcp_running = 0;
        tcp_tid = RVT_NULL;
        return;
    }

#if RVT_TCP_ACCEPT_NONBLOCK
    /*
     * 再显式设置一次非阻塞，兼容不支持 SOCK_NONBLOCK 创建标志的平台。
     * RT-Thread/lwIP 默认不打开 RVT_TCP_ACCEPT_NONBLOCK，保持原阻塞行为。
     */
    if (rvt_socket_set_nonblocking(tcp_listen_fd) != 0) {
        RVT_LOGE(RVT_WIFI_DISPLAY_LOG_TAG, "tcp: set nonblock failed, errno=%d", errno);
        rvt_socket_close(tcp_listen_fd);
        tcp_listen_fd = -1;
        tcp_listen_state = TCP_LISTEN_FAILED;
        tcp_running = 0;
        tcp_tid = RVT_NULL;
        return;
    }
#endif

    tcp_listen_state = TCP_LISTEN_READY;
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG, "tcp: listening on port %d", PORT_TCP);
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG, "tcp: accept=%s recv_wait=%s",
             RVT_TCP_ACCEPT_NONBLOCK ? "nonblock" : "block",
             RVT_SOCKET_AVOID_POLL ? "timeout" : "select");

    while (tcp_running)
    {
        /* 当前实现一次只服务一个手机端连接。新连接需等待旧连接断开。 */
        client_fd = accept(tcp_listen_fd, RVT_NULL, RVT_NULL);
        if (client_fd < 0) {
#if RVT_TCP_ACCEPT_NONBLOCK
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                rvt_tick_t now = rvt_tick_get();

                if (last_accept_log == 0 ||
                    now - last_accept_log >= rvt_tick_from_millisecond(TCP_ACCEPT_IDLE_LOG_MS)) {
                    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                             "tcp: accept loop alive, fd=%d errno=%d", tcp_listen_fd, errno);
                    last_accept_log = now;
                }
                rvt_thread_mdelay(100);
                continue;
            }
#endif

            if (tcp_running) {
                if (last_accept_errno != errno) {
                    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                             "tcp: accept failed, fd=%d errno=%d", tcp_listen_fd, errno);
                    last_accept_errno = errno;
                }
                rvt_thread_mdelay(100);
            }
            continue;
        }

        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG, "tcp: phone connected");
        tcp_handle_connected_socket(client_fd);
    }

    if (tcp_listen_fd >= 0) {
        rvt_socket_close(tcp_listen_fd);
        tcp_listen_fd = -1;
    }
    tcp_running = 0;
    tcp_tid = RVT_NULL;
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG, "tcp: thread exit");
}
#endif

int rvt_tcp_channel_init(void)
{
    int wait_ms = 0;

    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "tcp: init enter running=%d tid=%p current=%d active=%d connect=%d listen=%d role=%s avoid_poll=%d",
             tcp_running, tcp_tid, current_client_fd, tcp_active_fd,
             tcp_connect_fd, tcp_listen_fd,
             RVT_TCP_ROLE_CLIENT ? "client" : "server",
             RVT_SOCKET_AVOID_POLL);
    if (!tcp_running && tcp_tid) {
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "tcp: stale thread handle cleared tid=%p", tcp_tid);
        tcp_tid = RVT_NULL;
    }

    if (tcp_running) {
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG, "tcp: already running");
        return -1;
    }

    if (!tcp_send_mutex) {
        tcp_send_mutex = rvt_mutex_create("tcpsm");
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "tcp: send mutex create %s", tcp_send_mutex ? "ok" : "failed");
    }

    /* 创建定期上报丢包率的定时器 */
    stat_timer = rvt_timer_create("sttm", stat_timer_cb, RVT_NULL,
                                  STAT_INTERVAL_MS);
    if (stat_timer) {
        int timer_ret = rvt_timer_start(stat_timer);
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "tcp: stat timer start ret=%d timer=%p", timer_ret, stat_timer);
    } else {
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "tcp: stat timer create failed");
    }

    tcp_running = 1;
    stat_pending = 0;
    tcp_listen_state = TCP_LISTEN_PENDING;
    /* TCP 线程负责 listen/accept/recv，init 等待其完成 listen 后再返回。 */
    tcp_tid = rvt_thread_create("tcps", tcp_thread, RVT_NULL, RVT_TCP_THREAD_STACK,
                               TCP_THREAD_PRIORITY, 10);
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "tcp: create thread tid=%p stack=%d", tcp_tid, RVT_TCP_THREAD_STACK);
    if (tcp_tid) {
        if (rvt_thread_startup(tcp_tid) != 0) {
            RVT_LOGE(RVT_WIFI_DISPLAY_LOG_TAG, "tcp: startup thread failed");
            tcp_running = 0;
            tcp_tid = RVT_NULL;
            if (stat_timer) {
                rvt_timer_stop(stat_timer);
                rvt_timer_delete(stat_timer);
                stat_timer = RVT_NULL;
            }
            return -1;
        }
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "tcp: startup thread ok tid=%p", tcp_tid);
    } else {
        tcp_running = 0;
        if (stat_timer) {
            rvt_timer_stop(stat_timer);
            rvt_timer_delete(stat_timer);
            stat_timer = RVT_NULL;
        }
        return -1;
    }

    while (tcp_listen_state == TCP_LISTEN_PENDING &&
           wait_ms < TCP_START_WAIT_MS) {
        rvt_thread_mdelay(10);
        wait_ms += 10;
    }
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "tcp: start wait done state=%d wait=%d running=%d",
             tcp_listen_state, wait_ms, tcp_running);

    if (tcp_listen_state != TCP_LISTEN_READY) {
        RVT_LOGE(RVT_WIFI_DISPLAY_LOG_TAG, "tcp: start wait failed, state=%d", tcp_listen_state);
        rvt_tcp_channel_stop();
        return -1;
    }

    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG, "tcp: started");
    return 0;
}

void rvt_tcp_channel_stop(void)
{
    int fd;

    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
             "tcp: stop enter running=%d tid=%p current=%d active=%d connect=%d listen=%d",
             tcp_running, tcp_tid, current_client_fd, tcp_active_fd,
             tcp_connect_fd, tcp_listen_fd);

    tcp_running = 0;

    if (stat_timer) {
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "tcp: stop stat timer=%p", stat_timer);
        rvt_timer_stop(stat_timer);
        rvt_timer_delete(stat_timer);
        stat_timer = RVT_NULL;
    }

    fd = current_client_fd;
    current_client_fd = -1;
    /* 关闭 client socket 用于唤醒 recv/select，并阻止后续业务命令继续发送。 */
    if (fd >= 0) {
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "tcp: stop close current fd=%d", fd);
        rvt_socket_close(fd);
    }

    {
        int active_fd = tcp_active_fd;
        tcp_active_fd = -1;
        if (active_fd >= 0 && active_fd != fd) {
            RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                     "tcp: stop close active fd=%d", active_fd);
            rvt_socket_close(active_fd);
        }
    }

    {
        int connect_fd = tcp_connect_fd;
        tcp_connect_fd = -1;
        if (connect_fd >= 0 && connect_fd != fd) {
            RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                     "tcp: stop close connect fd=%d", connect_fd);
            rvt_socket_close(connect_fd);
        }
    }

    fd = tcp_listen_fd;
    tcp_listen_fd = -1;
    /* 关闭 listen socket 用于唤醒 accept，使 TCP 线程自然退出。 */
    if (fd >= 0) {
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "tcp: stop close server fd=%d", fd);
        rvt_socket_close(fd);
    }
    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG, "tcp: stop leave");
}
