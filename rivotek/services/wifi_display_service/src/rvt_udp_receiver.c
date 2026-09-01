#include "rvt_wifi_display_service.h"
#include "rvt_display_port.h"
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#if !defined(KERNEL_RTTHREAD) && !defined(RT_USING_LWIP)
#include <sys/time.h>
#include <netinet/in.h>
#endif

#ifndef RVT_WIFI_DISPLAY_MAX_FRAME_SIZE
/* 公共默认 256KB；R528 H264 可在平台构建里放大。 */
#define RVT_WIFI_DISPLAY_MAX_FRAME_SIZE      (256 * 1024)
#endif

#ifndef RVT_WIFI_DISPLAY_UDP_RECV_BUF_SIZE
#define RVT_WIFI_DISPLAY_UDP_RECV_BUF_SIZE   RVT_WIFI_DISPLAY_MAX_FRAME_SIZE
#endif

#define MAX_FRAME_SIZE      RVT_WIFI_DISPLAY_MAX_FRAME_SIZE
/* 接收超时时间（毫秒） */
#define FRAME_TIMEOUT_MS    50
/* 每 bit 标记 1 个字节是否已收到，用于发现 UDP 中间缺片 */
#define FRAME_COVER_MAP_SIZE    ((MAX_FRAME_SIZE + 7) / 8)
/* 不完整帧日志降频，避免 UDP 接收线程被打印拖慢 */
#define FRAME_DROP_LOG_INTERVAL  30
/* UDP 接收 socket 缓冲，降低 Android 连续分片时 lwIP 丢包概率 */
#define UDP_RECV_BUF_SIZE        RVT_WIFI_DISPLAY_UDP_RECV_BUF_SIZE

#ifndef RVT_WIFI_DISPLAY_RX_H264_RAW
#define RVT_WIFI_DISPLAY_RX_H264_RAW 0
#endif

/* 丢包统计 */
static uint32_t total_packets = 0;
static uint32_t lost_packets  = 0;

/* 当前接收帧的临时缓冲区 */
static uint8_t  frame_buf[MAX_FRAME_SIZE];
static uint8_t  frame_cover_map[FRAME_COVER_MAP_SIZE];
static int      frame_len = 0;
static int      frame_received_len = 0;
static int      frame_received_pkts = 0;
static uint16_t frame_seq = 0xFFFF;
static rvt_bool_t frame_started = RVT_FALSE;
static rvt_bool_t frame_has_expected_len = RVT_FALSE;
static uint16_t frame_finished_seq = 0xFFFF;
static rvt_bool_t frame_finished_seq_valid = RVT_FALSE;
static rvt_tick_t frame_start_tick = 0;

static int udp_running = 0;
static rvt_thread_t udp_tid = RVT_NULL;
static int udp_socket_fd = -1;

static int  is_jpeg_valid(const uint8_t *data, int len);
static int  is_h264_raw_valid(const uint8_t *data, int len);
static int  enqueue_current_frame(const char *reason);

/*
 * UDP 自定义包头布局:
 * [0..1] sync, [2..3] seq, [4..7] offset, [8..11] frame_len,
 * [12..13] payload_len, [14] flags, [15] reserved。
 */

/*
 * 函数名: frame_assembly_reset
 * 入参: 无
 * 返回值: 无
 */
static void frame_assembly_reset(void)
{
    frame_len = 0;
    frame_received_len = 0;
    frame_received_pkts = 0;
    frame_has_expected_len = RVT_FALSE;
    memset(frame_cover_map, 0, sizeof(frame_cover_map));
}

/*
 * 函数名: frame_start
 * 入参: seq 当前帧序号，expected_len 当前帧期望总长度
 * 返回值: 无
 */
static void frame_start(uint16_t seq, uint32_t expected_len)
{
    /* seq 切换表示开始组一帧新的投屏数据。 */
    frame_seq = seq;
    frame_assembly_reset();
    frame_started = RVT_TRUE;
    frame_start_tick = rvt_tick_get();

    /* 新协议每个分片都带总帧长，便于接收端判断完整性。 */
    if (expected_len > 0 && expected_len <= MAX_FRAME_SIZE) {
        frame_len = (int)expected_len;
        frame_has_expected_len = RVT_TRUE;
    }
}

/*
 * 函数名: frame_byte_received
 * 入参: offset 当前帧内的字节偏移
 * 返回值: 1 表示该字节已收到，0 表示未收到或偏移非法
 */
static int frame_byte_received(uint32_t offset)
{
    if (offset >= MAX_FRAME_SIZE)
        return 0;

    return (frame_cover_map[offset >> 3] & (1U << (offset & 0x7))) != 0;
}

/*
 * 函数名: frame_mark_received
 * 入参: offset 当前分片在帧内的起始偏移，len 当前分片有效数据长度
 * 返回值: 无
 */
static void frame_mark_received(uint32_t offset, int len)
{
    uint32_t end = offset + len;
    uint32_t i;

    /* 按字节记录覆盖范围，避免重复分片导致已接收长度被重复累加。 */
    for (i = offset; i < end; i++) {
        uint8_t mask = (uint8_t)(1U << (i & 0x7));
        uint8_t *slot = &frame_cover_map[i >> 3];

        if ((*slot & mask) == 0) {
            *slot |= mask;
            frame_received_len++;
        }
    }
}

/*
 * 函数名: frame_is_complete
 * 入参: 无
 * 返回值: 1 表示当前帧已完整接收，0 表示尚未完整接收
 */
static int frame_is_complete(void)
{
    return frame_has_expected_len && frame_len > 0 && frame_received_len >= frame_len;
}

/*
 * 函数名: frame_can_try_legacy_jpeg
 * 入参: 无
 * 返回值: 1 表示无帧长包头时可按 JPEG 头尾尝试组帧，0 表示不可尝试
 */
static int frame_can_try_legacy_jpeg(void)
{
    if (frame_has_expected_len || frame_len <= 0)
        return 0;

    return frame_received_len >= frame_len && is_jpeg_valid(frame_buf, frame_len);
}

/*
 * 函数名: get_udp_statistics
 * 入参: total 用于输出接收包数量，lost 用于输出丢包或丢帧数量
 * 返回值: 无
 */
void get_udp_statistics(uint32_t *total, uint32_t *lost)
{
    *total = total_packets;
    *lost  = lost_packets;
    total_packets = 0;
    lost_packets  = 0;
}

/*
 * 函数名: is_jpeg_valid
 * 入参: data JPEG 数据首地址，len JPEG 数据长度
 * 返回值: 1 表示 JPEG 头尾合法，0 表示数据非法
 */
static int is_jpeg_valid(const uint8_t *data, int len)
{
    if (len < 4) return 0;
    if (data[0] != 0xFF || data[1] != 0xD8) return 0;
    if (data[len - 2] != 0xFF || data[len - 1] != 0xD9) return 0;
    return 1;
}

/*
 * 函数名: is_h264_raw_valid
 * 入参: data H264 Annex-B 数据首地址，len 数据长度
 * 返回值: 1 表示具备基本 H264 RAW 特征，0 表示数据非法
 */
static int is_h264_raw_valid(const uint8_t *data, int len)
{
    int i;

    if (!data || len < 5)
        return 0;

    for (i = 0; i + 4 < len && i < 16; i++) {
        if (data[i] == 0x00 && data[i + 1] == 0x00 &&
            data[i + 2] == 0x01) {
            return 1;
        }
        if (data[i] == 0x00 && data[i + 1] == 0x00 &&
            data[i + 2] == 0x00 && data[i + 3] == 0x01) {
            return 1;
        }
    }

    return 0;
}

/*
 * 函数名: frame_close_current
 * 入参: 无
 * 返回值: 无
 */
static void frame_close_current(void)
{
    frame_finished_seq = frame_seq;
    frame_finished_seq_valid = RVT_TRUE;
    frame_started = RVT_FALSE;
    frame_assembly_reset();
}

/*
 * 函数名: frame_drop_should_log
 * 入参: count 当前类型丢帧累计次数
 * 返回值: 1 表示本次需要打印日志，0 表示本次跳过日志
 */
static int frame_drop_should_log(uint32_t count)
{
    return count <= 5 || (count % FRAME_DROP_LOG_INTERVAL) == 0;
}

/*
 * 函数名: seq_is_newer
 * 入参: seq 待判断的帧序号，old_seq 已知旧帧序号
 * 返回值: 1 表示 seq 比 old_seq 新，0 表示不是新帧
 */
static int seq_is_newer(uint16_t seq, uint16_t old_seq)
{
    return (uint16_t)(seq - old_seq) < 0x8000 && seq != old_seq;
}

/*
 * 函数名: enqueue_current_frame
 * 入参: reason 当前尝试入队的触发原因
 * 返回值: 0 表示帧提交成功，负值表示当前帧被丢弃或提交失败
 */
static int enqueue_current_frame(const char *reason)
{
    int complete;
    int valid;
    rvt_bool_t log_this_frame = RVT_FALSE;
    uint8_t first0 = 0;
    uint8_t first1 = 0;
    uint8_t last0 = 0;
    uint8_t last1 = 0;
    static uint32_t incomplete_frames = 0;
    static uint32_t invalid_frames = 0;
    static uint32_t queued_frames = 0;
    static uint32_t queue_full_frames = 0;
    static uint32_t complete_frames = 0;

    if (!frame_started || frame_len <= 0)
        return -1;

    if (frame_len >= 2 && frame_byte_received(0) && frame_byte_received(1)) {
        first0 = frame_buf[0];
        first1 = frame_buf[1];
    }
    if (frame_len >= 2 && frame_byte_received(frame_len - 2) &&
        frame_byte_received(frame_len - 1)) {
        last0 = frame_buf[frame_len - 2];
        last1 = frame_buf[frame_len - 1];
    }

    complete = frame_is_complete() || frame_can_try_legacy_jpeg();
    valid = complete &&
#if RVT_WIFI_DISPLAY_RX_H264_RAW
            is_h264_raw_valid(frame_buf, frame_len);
#else
            is_jpeg_valid(frame_buf, frame_len);
#endif

    /* 不完整帧不能送硬解码，避免解码器被损坏帧反复拖慢。 */
    if (!complete) {
        lost_packets++;
        incomplete_frames++;
        log_this_frame = frame_drop_should_log(incomplete_frames);
        if (log_this_frame) {
            RVT_LOGI(RVT_UDP_LOG_TAG, "udp: discard incomplete frame seq=%u len=%d recv=%d pkts=%d (%s) first2=%02X%02X last2=%02X%02X",
                     frame_seq, frame_len, frame_received_len,
                     frame_received_pkts, reason,
                     first0, first1, last0, last1);
        }
        return -1;
    }

    /* 这里只做编码格式轻量校验，帧级 CRC 后续可在这里补。 */
    if (frame_len < 4 || !valid) {
        lost_packets++;
        invalid_frames++;
        if (frame_drop_should_log(invalid_frames)) {
            RVT_LOGI(RVT_UDP_LOG_TAG, "udp: discard invalid frame seq=%u len=%d recv=%d pkts=%d (%s) h264=%d first2=%02X%02X last2=%02X%02X",
                     frame_seq, frame_len, frame_received_len,
                     frame_received_pkts, reason, RVT_WIFI_DISPLAY_RX_H264_RAW,
                     first0, first1, last0, last1);
        }
        return -1;
    }

    complete_frames++;
    if (complete_frames <= 5 || (complete_frames % 30) == 0) {
        RVT_LOGI(RVT_UDP_LOG_TAG,
                 "udp: complete %s #%u seq=%u len=%d recv=%d pkts=%d (%s) first4=%02X%02X%02X%02X",
#if RVT_WIFI_DISPLAY_RX_H264_RAW
                 "h264",
#else
                 "jpeg",
#endif
                 complete_frames, frame_seq, frame_len, frame_received_len,
                 frame_received_pkts, reason,
                 frame_buf[0], frame_buf[1], frame_buf[2], frame_buf[3]);
    }

    /* 解码渲染在平台 port 线程中完成，UDP 线程只负责提交完整帧。 */
    if (rvt_display_port_submit_jpeg(frame_buf, frame_len) != 0) {
        lost_packets++;
        queue_full_frames++;
        if (frame_drop_should_log(queue_full_frames)) {
            RVT_LOGI(RVT_UDP_LOG_TAG, "udp: display queue submit failed seq=%u len=%d",
                     frame_seq, frame_len);
        }
        return -1;
    }

    queued_frames++;
    return 0;
}

/*
 * 函数名: udp_thread
 * 入参: arg 线程参数，当前未使用
 * 返回值: 无
 */
static void udp_thread(void *arg)
{
    int sockfd;
    struct sockaddr_in addr;
    uint8_t *buf = rvt_malloc(MAX_PACKET);
    int rcvbuf_size = UDP_RECV_BUF_SIZE;
    int reuse = 1;
    struct timeval timeout;

    if (!buf) {
        RVT_LOGE(RVT_UDP_LOG_TAG, "udp: no memory for temp buffer");
        udp_running = 0;
        udp_tid = RVT_NULL;
        return;
    }

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        RVT_LOGE(RVT_UDP_LOG_TAG, "udp: socket failed");
        rvt_free(buf);
        udp_running = 0;
        udp_tid = RVT_NULL;
        return;
    }
    udp_socket_fd = sockfd;

    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR,
                   &reuse, sizeof(reuse)) < 0) {
        RVT_LOGI(RVT_UDP_LOG_TAG,
                 "udp: set SO_REUSEADDR failed fd=%d errno=%d",
                 sockfd, errno);
    }

    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF,
                   &rcvbuf_size, sizeof(rcvbuf_size)) < 0) {
        RVT_LOGI(RVT_UDP_LOG_TAG, "udp: set SO_RCVBUF failed");
    }

    timeout.tv_sec = 0;
    timeout.tv_usec = 500 * 1000;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO,
               &timeout, sizeof(timeout));

    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT_UDP);
    addr.sin_addr.s_addr = INADDR_ANY;
    RVT_LOGI(RVT_UDP_LOG_TAG, "udp: bind start fd=%d port=%d", sockfd, PORT_UDP);
    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        RVT_LOGE(RVT_UDP_LOG_TAG, "udp: bind failed fd=%d errno=%d", sockfd, errno);
        rvt_free(buf);
        if (udp_socket_fd == sockfd)
            udp_socket_fd = -1;
        rvt_socket_close(sockfd);
        udp_running = 0;
        udp_tid = RVT_NULL;
        return;
    }
    RVT_LOGI(RVT_UDP_LOG_TAG, "udp: bind ok fd=%d port=%d", sockfd, PORT_UDP);

    while (udp_running) {
        int n = recvfrom(sockfd, buf, MAX_PACKET, 0, NULL, NULL);
        if (!udp_running)
            break;
        if (n < RVT_WIFI_DISPLAY_UDP_HEADER_LEN)
            continue;

        uint8_t  sync0 = buf[0];
        uint8_t  sync1 = buf[1];
        uint16_t seq = ((uint16_t)buf[2] << 8) | buf[3];
        uint32_t offset = ((uint32_t)buf[4] << 24) |
                          ((uint32_t)buf[5] << 16) |
                          ((uint32_t)buf[6] << 8) |
                          (uint32_t)buf[7];
        uint32_t hdr_frame_len = ((uint32_t)buf[8] << 24) |
                                 ((uint32_t)buf[9] << 16) |
                                 ((uint32_t)buf[10] << 8) |
                                 (uint32_t)buf[11];
        uint16_t hdr_payload_len = ((uint16_t)buf[12] << 8) | buf[13];
        uint8_t flags = buf[14];
        uint8_t *payload = buf + RVT_WIFI_DISPLAY_UDP_HEADER_LEN;
        int payload_len = n - RVT_WIFI_DISPLAY_UDP_HEADER_LEN;

        /* sync 不匹配说明不是投屏数据包，直接丢弃。 */
        if (sync0 != RVT_WIFI_DISPLAY_UDP_SYNC0 ||
            sync1 != RVT_WIFI_DISPLAY_UDP_SYNC1) {
            continue;
        }

        /* Android 端头里带 payload_len，优先按头部长度裁剪有效载荷。 */
        if (hdr_payload_len > 0) {
            if (hdr_payload_len > payload_len) {
                lost_packets++;
                RVT_LOGI(RVT_UDP_LOG_TAG, "udp: bad payload len seq=%u offset=%u hdr=%u actual=%d",
                         seq, (unsigned int)offset, hdr_payload_len, payload_len);
                continue;
            }
            payload_len = hdr_payload_len;
        }

        if (hdr_frame_len > MAX_FRAME_SIZE) {
            lost_packets++;
            RVT_LOGI(RVT_UDP_LOG_TAG, "udp: frame too large seq=%u len=%u",
                     seq, (unsigned int)hdr_frame_len);
            continue;
        }

        total_packets++;

        if (!frame_started || seq != frame_seq) {
            if (frame_started && !seq_is_newer(seq, frame_seq))
                continue;

            /* 新 seq 到来时，先尝试提交上一帧；上一帧不完整会在内部丢弃。 */
            if (frame_started) {
                enqueue_current_frame("seq switch");
                frame_close_current();
            }

            /* 已结束的旧 seq 重传包不再参与组帧。 */
            if (frame_finished_seq_valid && !seq_is_newer(seq, frame_finished_seq))
                continue;

            frame_start(seq, hdr_frame_len);
        } else if (hdr_frame_len > 0 && !frame_has_expected_len) {
            frame_len = (int)hdr_frame_len;
            frame_has_expected_len = RVT_TRUE;
        } else if (hdr_frame_len > 0 && frame_has_expected_len &&
                   frame_len != (int)hdr_frame_len) {
            lost_packets++;
            RVT_LOGI(RVT_UDP_LOG_TAG, "udp: frame len changed seq=%u old=%d new=%u, discard",
                     seq, frame_len, (unsigned int)hdr_frame_len);
            frame_close_current();
            continue;
        }

        if (frame_started &&
            rvt_tick_get() - frame_start_tick >= rvt_tick_from_millisecond(FRAME_TIMEOUT_MS)) {
            enqueue_current_frame("timeout");
            frame_close_current();
            continue;
        }

        if (frame_started && seq == frame_seq) {
            if (offset + payload_len > MAX_FRAME_SIZE) {
                RVT_LOGI(RVT_UDP_LOG_TAG, "udp: offset+len exceeds max frame size, discard frame");
                frame_close_current();
                continue;
            }

            if (frame_has_expected_len && offset + payload_len > (uint32_t)frame_len) {
                RVT_LOGI(RVT_UDP_LOG_TAG, "udp: packet exceeds frame len seq=%u offset=%u payload=%d frame_len=%d",
                         seq, (unsigned int)offset, payload_len, frame_len);
                frame_close_current();
                continue;
            }

            /* 通过 offset 直接拷贝，支持 UDP 分片乱序到达。 */
            memcpy(frame_buf + offset, payload, payload_len);
            frame_mark_received(offset, payload_len);
            frame_received_pkts++;

            if (!frame_has_expected_len && offset + payload_len > frame_len)
                frame_len = offset + payload_len;

            frame_start_tick = rvt_tick_get();

            if (frame_is_complete() ||
                ((flags & RVT_WIFI_DISPLAY_UDP_FLAG_EOF) && frame_is_complete())) {
                enqueue_current_frame((flags & RVT_WIFI_DISPLAY_UDP_FLAG_EOF) ?
                                      "eof" : "complete");
                frame_close_current();
            }
        }
    }

    rvt_free(buf);
    if (udp_socket_fd == sockfd)
        udp_socket_fd = -1;
    rvt_socket_close(sockfd);
    frame_started = RVT_FALSE;
    frame_assembly_reset();
    udp_running = 0;
    udp_tid = RVT_NULL;
}

/*
 * 函数名: udp_receiver_init
 * 入参: 无
 * 返回值: 0 表示 UDP 接收通道启动成功，负值表示启动失败
 */
int udp_receiver_init(void)
{
    RVT_LOGI(RVT_UDP_LOG_TAG,
             "udp: init enter running=%d tid=%p fd=%d",
             udp_running, udp_tid, udp_socket_fd);

    if (udp_running) {
        RVT_LOGI(RVT_UDP_LOG_TAG, "udp: already running");
        return -1;
    }

    if (udp_socket_fd >= 0) {
        RVT_LOGI(RVT_UDP_LOG_TAG,
                 "udp: stale fd mark invalid fd=%d", udp_socket_fd);
        udp_socket_fd = -1;
    }

    if (rvt_display_port_init() != 0)
        return -1;

    /* 每次启动 UDP 接收都清掉上一轮残留组帧状态。 */
    frame_assembly_reset();
    frame_started = RVT_FALSE;
    frame_finished_seq_valid = RVT_FALSE;

    udp_running = 1;
    udp_tid = rvt_thread_create("udprx", udp_thread, RVT_NULL, 4096, 26, 10);
    if (udp_tid) {
        rvt_thread_startup(udp_tid);
    } else {
        udp_running = 0;
        rvt_display_port_deinit();
        return -1;
    }

    RVT_LOGI(RVT_UDP_LOG_TAG, "udp: started");
    return 0;
}

/*
 * 函数名: udp_receiver_stop
 * 入参: 无
 * 返回值: 无
 */
void udp_receiver_stop(void)
{
    int fd;

    udp_running = 0;
    fd = udp_socket_fd;
    udp_socket_fd = -1;
    if (fd >= 0) {
        RVT_LOGI(RVT_UDP_LOG_TAG, "udp: stop close fd=%d", fd);
        rvt_socket_close(fd);
    }
    frame_started = RVT_FALSE;
    frame_assembly_reset();
    rvt_display_port_flush();
    RVT_LOGI(RVT_UDP_LOG_TAG, "udp: stopped");
}

/*
 * 函数名: udp_receiver_clear_stream
 * 入参: 无
 * 返回值: 无
 */
void udp_receiver_clear_stream(void)
{
    /* STREAM_STOP 只清当前画面和解码渲染缓存，不关闭 TCP/UDP 通道。 */
    frame_started = RVT_FALSE;
    frame_assembly_reset();
    frame_finished_seq_valid = RVT_FALSE;
    rvt_display_port_deinit();
    RVT_LOGI(RVT_UDP_LOG_TAG, "udp: stream cleared");
}
