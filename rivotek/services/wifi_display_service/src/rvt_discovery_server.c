#include "rvt_wifi_display_service.h"
#include <string.h>
#include <errno.h>
#if RVT_SOCKET_AVOID_POLL == 0 && !defined(KERNEL_RTTHREAD) && !defined(RT_USING_LWIP)
#include <sys/select.h>
#endif
#if !defined(KERNEL_RTTHREAD) && !defined(RT_USING_LWIP)
#include <sys/time.h>
#include <netinet/in.h>
#endif

#define RVT_DISCOVERY_PORT      6005
#define RVT_DISCOVERY_REQ       "RVT_WIFI_DISPLAY_DISCOVER"
#define RVT_DISCOVERY_RESP      "RVT_WIFI_DISPLAY 6004 5004\r\n"
#define RVT_DISCOVERY_BUF_SIZE  128
#ifndef RVT_DISCOVERY_THREAD_STACK
#define RVT_DISCOVERY_THREAD_STACK 4096
#endif
#define DISCOVERY_THREAD_STACK  RVT_DISCOVERY_THREAD_STACK
#define DISCOVERY_THREAD_PRIO   21

/* UDP discovery 用于手机在热点/STA 场景下主动发现车机 TCP/UDP 端口。 */
static int discovery_running = 0;
static int discovery_fd = -1;
static rvt_thread_t discovery_tid = RVT_NULL;

/*
 * 函数名: discovery_thread
 * 入参: arg 线程参数，当前未使用
 * 返回值: 无
 */
static void discovery_thread(void *arg)
{
    int reuse = 1;
    int broadcast = 1;
    struct sockaddr_in local_addr;
#if RVT_SOCKET_AVOID_POLL
    struct timeval recv_timeout;
#endif
    char buf[RVT_DISCOVERY_BUF_SIZE];

    (void)arg;

    discovery_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (discovery_fd < 0) {
        RVT_LOGE(RVT_WIFI_DISPLAY_LOG_TAG, "wifi display discovery: socket failed");
        discovery_running = 0;
        discovery_tid = RVT_NULL;
        return;
    }

    setsockopt(discovery_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    setsockopt(discovery_fd, SOL_SOCKET, SO_BROADCAST,
               &broadcast, sizeof(broadcast));

#if RVT_SOCKET_AVOID_POLL
    /*
     * R528/openVela 的 UDP select 会进入 NuttX poll 回调链路，实测在
     * hpwork 收到 discovery 包时可能触发 poll semaphore 状态异常。
     * 这里用 SO_RCVTIMEO + recvfrom 轮询退出标志，避免走 socket poll。
     */
    recv_timeout.tv_sec = 1;
    recv_timeout.tv_usec = 0;
    setsockopt(discovery_fd, SOL_SOCKET, SO_RCVTIMEO,
               &recv_timeout, sizeof(recv_timeout));
#endif

    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_port = htons(RVT_DISCOVERY_PORT);
    local_addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(discovery_fd, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
        RVT_LOGE(RVT_WIFI_DISPLAY_LOG_TAG,
                 "wifi display discovery: bind failed, errno=%d", errno);
        rvt_socket_close(discovery_fd);
        discovery_fd = -1;
        discovery_running = 0;
        discovery_tid = RVT_NULL;
        return;
    }

    RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG, "wifi display discovery: listening on udp %d", RVT_DISCOVERY_PORT);
    while (discovery_running) {
#if RVT_SOCKET_AVOID_POLL == 0
        fd_set read_set;
        struct timeval timeout;
#endif
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        int fd = discovery_fd;
#if RVT_SOCKET_AVOID_POLL == 0
        int ret;
#endif
        int n;

        if (fd < 0)
            break;

#if RVT_SOCKET_AVOID_POLL == 0
        FD_ZERO(&read_set);
        FD_SET(fd, &read_set);
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        ret = select(fd + 1, &read_set, RVT_NULL, RVT_NULL, &timeout);
        if (ret <= 0)
            continue;
#endif

        memset(&from_addr, 0, sizeof(from_addr));
        n = recvfrom(fd, buf, sizeof(buf) - 1, 0,
                     (struct sockaddr *)&from_addr, &from_len);
        if (n <= 0)
            continue;

        buf[n] = '\0';
        if (strncmp(buf, RVT_DISCOVERY_REQ, strlen(RVT_DISCOVERY_REQ)) != 0)
            continue;

        /* 响应内容携带 TCP 控制端口和 UDP 视频端口，手机端据此建立通道。 */
        {
            const uint8_t *ip =
                (const uint8_t *)&from_addr.sin_addr.s_addr;
            const uint8_t *port =
                (const uint8_t *)&from_addr.sin_port;
            RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                     "wifi display discovery: request from %u.%u.%u.%u:%u",
                     ip[0], ip[1], ip[2], ip[3],
                     (unsigned int)port[0] * 256U + port[1]);
        }
        if (sendto(fd, RVT_DISCOVERY_RESP, strlen(RVT_DISCOVERY_RESP), 0,
                   (struct sockaddr *)&from_addr, from_len) < 0) {
            RVT_LOGE(RVT_WIFI_DISPLAY_LOG_TAG,
                     "wifi display discovery: response failed, errno=%d", errno);
        }
    }

    if (discovery_fd >= 0) {
        rvt_socket_close(discovery_fd);
        discovery_fd = -1;
    }
    discovery_running = 0;
    discovery_tid = RVT_NULL;
}

int rvt_wifi_display_discovery_start(void)
{
    int ret;

    if (discovery_running)
        return 0;

    /* discovery 独立运行，不阻塞 start_wifi_display 主流程。 */
    discovery_running = 1;
    discovery_tid = rvt_thread_create("rvtdisc", discovery_thread, RVT_NULL,
                                     DISCOVERY_THREAD_STACK,
                                     DISCOVERY_THREAD_PRIO, 10);
    if (!discovery_tid) {
        discovery_running = 0;
        RVT_LOGE(RVT_WIFI_DISPLAY_LOG_TAG, "wifi display discovery: create thread failed");
        return -1;
    }

    ret = rvt_thread_startup(discovery_tid);
    if (ret != 0) {
        discovery_running = 0;
        discovery_tid = RVT_NULL;
        RVT_LOGE(RVT_WIFI_DISPLAY_LOG_TAG,
                 "wifi display discovery: startup thread failed, ret=%d", ret);
        return -1;
    }

    return 0;
}

void rvt_wifi_display_discovery_stop(void)
{
    int fd;

    discovery_running = 0;
    fd = discovery_fd;
    discovery_fd = -1;
    if (fd >= 0) {
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "wifi display discovery: stop close fd=%d", fd);
        rvt_socket_close(fd);
    } else {
        RVT_LOGI(RVT_WIFI_DISPLAY_LOG_TAG,
                 "wifi display discovery: stop no fd");
    }
}
