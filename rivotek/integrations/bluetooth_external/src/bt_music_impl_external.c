#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <termios.h>
#include <time.h>
#include <debug.h>
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif
#include <unistd.h>

#include "bt_music_impl.h"
#include "audio_focus_manager.h"
#include "rivotek_platform.h"

#define SCOOTERDEMO_BT_UART_BAUD B115200
#define SCOOTERDEMO_BT_POLL_US (100 * 1000)
#define SCOOTERDEMO_BT_THREAD_STACKSIZE 8192
#define SCOOTERDEMO_BT_CMD_QUEUE_DEPTH 8
#define SCOOTERDEMO_BT_CMD_DEDUP_MS 350U

// 协议常量与类型定义（移植自 bluetooth_comm.cpp）
#define BT_COMM_RX_BUFFER_SIZE 512
#define VELA_DATA_LOG 0x80
#define VELA_DATA_BLUETOOTH_MUSIC 0x81
#define VELA_DATA_STATUS 0x82
#define VELA_CMD_BLUETOOTH 0x01

typedef enum {
    VELA_AVRC_ATTR_TITLE = 0x01,
    VELA_AVRC_ATTR_ARTIST = 0x02,
    VELA_AVRC_ATTR_ALBUM = 0x03,
    VELA_AVRC_ATTR_PLAY_POS = 0x04,
    VELA_AVRC_ATTR_PLAYING_TIME = 0x05,
} vela_avrc_attr_t;

typedef enum {
    VELA_STATUS_BLUETOOTH = 0x00,
    VELA_STATUS_PLAY_STATE = 0x01,
    VELA_STATUS_AUDIO_SOURCE = 0x02,
} vela_status_type_t;

typedef enum {
    VELA_BT_STATE_DISCONNECT = 0,
    VELA_BT_STATE_CONNECTED = 1,
} vela_bt_state_t;

typedef enum {
    VELA_BT_PLAY_STATE_NONE = 0,
    VELA_BT_PLAY_STATE_PLAYING = 1,
    VELA_BT_PLAY_STATE_PAUSE = 2,
} vela_bt_play_state_t;

typedef enum {
    VELA_BT_CMD_GET_STATUS = 0x00,
    VELA_BT_CMD_GET_PLAY_STATUS = 0x01,
    VELA_BT_CMD_PREV = 0x02,
    VELA_BT_CMD_PLAY = 0x03,
    VELA_BT_CMD_NEXT = 0x04,
} vela_bt_cmd_t;

typedef enum {
    VELA_CMD_EXECUTE_FAIL = 0x00,
    VELA_CMD_EXECUTE_SUCCESS = 0x01,
} vela_cmd_result_t;

// 扩展状态结构体，兼容 music_info
typedef struct {
    bool valid;
    bool connected;
    bool playing;
    char device_name[SCOOTERDEMO_BT_MUSIC_DEVICE_NAME_MAX];
    char title[SCOOTERDEMO_BT_MUSIC_TITLE_MAX];
    char artist[SCOOTERDEMO_BT_MUSIC_ARTIST_MAX];
    char album[SCOOTERDEMO_BT_MUSIC_ARTIST_MAX];
    uint32_t duration_ms;
    vela_bt_state_t bt_state;
    vela_bt_play_state_t play_state;
    uint32_t play_pos;
    uint32_t playing_time;
} scooterdemo_bt_music_state_t;

typedef enum {
    SCOOTERDEMO_BT_CMD_NONE = 0,
    SCOOTERDEMO_BT_CMD_CONNECT_TOGGLE,
    SCOOTERDEMO_BT_CMD_PLAY,
    SCOOTERDEMO_BT_CMD_PAUSE,
    SCOOTERDEMO_BT_CMD_STOP,
    SCOOTERDEMO_BT_CMD_PREV,
    SCOOTERDEMO_BT_CMD_NEXT,
    SCOOTERDEMO_BT_CMD_METADATA,
} scooterdemo_bt_cmd_t;

static const scooterdemo_bt_music_ops_t *g_bt_music_ops;
static void *g_bt_music_ops_ctx;
static pthread_mutex_t g_bt_music_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_bt_music_cmd_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_bt_music_cmd_cond = PTHREAD_COND_INITIALIZER;
static pthread_t g_bt_music_reader_tid;
static pthread_t g_bt_music_cmd_tid;
static bool g_bt_music_reader_running;
static bool g_bt_music_reader_started;
static bool g_bt_music_cmd_running;
static bool g_bt_music_cmd_started;
static int g_bt_uart_fd = -1;
static int g_bt_uart_last_open_errno;
static scooterdemo_bt_cmd_t g_bt_music_cmd_queue[SCOOTERDEMO_BT_CMD_QUEUE_DEPTH];
static unsigned int g_bt_music_cmd_head;
static unsigned int g_bt_music_cmd_tail;
static unsigned int g_bt_music_cmd_count;
static scooterdemo_bt_music_state_t g_bt_music_state;
static scooterdemo_bt_cmd_t g_bt_music_last_cmd = SCOOTERDEMO_BT_CMD_NONE;
static uint64_t g_bt_music_last_cmd_ms;
static bool g_bt_music_focus_cb_registered;
static bool g_bt_music_focus_requested;
static bool g_bt_music_resume_after_ai;
static int g_bt_music_focus_owner_cached = AUDIO_FOCUS_IDLE;

static int scooterdemo_bt_music_exec_cmd_async(scooterdemo_bt_cmd_t cmd);
static void scooterdemo_bt_music_sync_focus_owner_fallback(void);

static void scooterdemo_bt_music_maybe_request_focus(bool playing)
{
    int ret;
    bool should_request = false;

    if (!playing) {
        return;
    }

    pthread_mutex_lock(&g_bt_music_lock);
    if (g_bt_music_focus_cb_registered && !g_bt_music_focus_requested) {
        should_request = true;
    }
    pthread_mutex_unlock(&g_bt_music_lock);

    if (!should_request) {
        return;
    }

    ret = audio_focus_request(AUDIO_FOCUS_MUSIC);
    if (ret == 0) {
        pthread_mutex_lock(&g_bt_music_lock);
        g_bt_music_focus_requested = true;
        pthread_mutex_unlock(&g_bt_music_lock);
        syslog(LOG_ERR, "bt_music(ext): focus request MUSIC success\n");
    } else {
        syslog(LOG_ERR, "bt_music(ext): focus request MUSIC failed: %d\n", ret);
    }
}

static void scooterdemo_bt_music_focus_change_cb(int change)
{
    bool was_playing = false;
    bool should_resume = false;
    bool is_playing_now = false;

    syslog(LOG_ERR, "bt_music(ext): focus change callback change=%d\n", change);

    pthread_mutex_lock(&g_bt_music_lock);
    if (change == AUDIO_FOCUS_CHANGE_LOSS ||
        change == AUDIO_FOCUS_CHANGE_LOSS_TRANSIENT ||
        change == AUDIO_FOCUS_CHANGE_LOSS_TRANSIENT_CAN_DUCK) {
        g_bt_music_focus_requested = false;
        g_bt_music_focus_owner_cached = AUDIO_FOCUS_AI;
        was_playing = g_bt_music_state.playing;
        if (was_playing) {
            g_bt_music_resume_after_ai = true;
            g_bt_music_state.playing = false;
        }
    } else if (change == AUDIO_FOCUS_CHANGE_GAIN) {
        g_bt_music_focus_requested = true;
        g_bt_music_focus_owner_cached = AUDIO_FOCUS_MUSIC;
        should_resume = g_bt_music_resume_after_ai;
        is_playing_now = g_bt_music_state.playing;
        g_bt_music_resume_after_ai = false;
    }
    pthread_mutex_unlock(&g_bt_music_lock);

    if (was_playing) {
        syslog(LOG_ERR, "bt_music(ext): focus loss=%d, auto pause\n", change);
        (void)scooterdemo_bt_music_exec_cmd_async(SCOOTERDEMO_BT_CMD_PAUSE);
    }

    if (change == AUDIO_FOCUS_CHANGE_GAIN && should_resume && !is_playing_now) {
        syslog(LOG_ERR, "bt_music(ext): focus gain, auto resume\n");
        (void)scooterdemo_bt_music_exec_cmd_async(SCOOTERDEMO_BT_CMD_PLAY);
    }
}

static void scooterdemo_bt_music_sync_focus_owner_fallback(void)
{
    int owner;
    int prev_owner;
    bool need_pause = false;
    bool need_resume = false;
    bool need_request_focus = false;
    bool log_transition = false;

    owner = audio_focus_get_owner();
    if (owner < 0) {
        return;
    }

    pthread_mutex_lock(&g_bt_music_lock);
    prev_owner = g_bt_music_focus_owner_cached;
    if (owner != prev_owner) {
        g_bt_music_focus_owner_cached = owner;
        log_transition = true;

        if (owner == AUDIO_FOCUS_AI) {
            g_bt_music_focus_requested = false;
            if (g_bt_music_state.playing) {
                g_bt_music_resume_after_ai = true;
                g_bt_music_state.playing = false;
                need_pause = true;
            }
        } else if (owner == AUDIO_FOCUS_MUSIC) {
            g_bt_music_focus_requested = true;
            if (prev_owner == AUDIO_FOCUS_AI &&
                g_bt_music_resume_after_ai &&
                !g_bt_music_state.playing) {
                g_bt_music_resume_after_ai = false;
                need_resume = true;
            }
        } else if (owner == AUDIO_FOCUS_IDLE) {
            g_bt_music_focus_requested = false;
            if (prev_owner == AUDIO_FOCUS_AI &&
                g_bt_music_resume_after_ai &&
                !g_bt_music_state.playing) {
                g_bt_music_resume_after_ai = false;
                need_request_focus = true;
                need_resume = true;
            }
        } else {
            g_bt_music_focus_requested = false;
        }
    }
    pthread_mutex_unlock(&g_bt_music_lock);

    if (log_transition) {
        syslog(LOG_ERR,
               "bt_music(ext): focus owner fallback %d -> %d\n",
               prev_owner,
               owner);
    }

    if (need_pause) {
        syslog(LOG_ERR, "bt_music(ext): fallback owner=AI, auto pause\n");
        (void)scooterdemo_bt_music_exec_cmd_async(SCOOTERDEMO_BT_CMD_PAUSE);
    }

    if (need_request_focus) {
        int ret = audio_focus_request(AUDIO_FOCUS_MUSIC);

        if (ret == 0) {
            pthread_mutex_lock(&g_bt_music_lock);
            g_bt_music_focus_requested = true;
            g_bt_music_focus_owner_cached = AUDIO_FOCUS_MUSIC;
            pthread_mutex_unlock(&g_bt_music_lock);
            syslog(LOG_ERR, "bt_music(ext): fallback owner=IDLE, request MUSIC focus success\n");
        } else {
            syslog(LOG_ERR, "bt_music(ext): fallback owner=IDLE, request MUSIC focus failed: %d\n",
                   ret);
        }
    }

    if (need_resume) {
        syslog(LOG_ERR, "bt_music(ext): fallback auto resume\n");
        (void)scooterdemo_bt_music_exec_cmd_async(SCOOTERDEMO_BT_CMD_PLAY);
    }
}

static const char *scooterdemo_bt_music_cmd_name(scooterdemo_bt_cmd_t cmd)
{
    switch (cmd) {
    case SCOOTERDEMO_BT_CMD_CONNECT_TOGGLE:
        return "connect_toggle";
    case SCOOTERDEMO_BT_CMD_PLAY:
        return "play";
    case SCOOTERDEMO_BT_CMD_PAUSE:
        return "pause";
    case SCOOTERDEMO_BT_CMD_STOP:
        return "stop";
    case SCOOTERDEMO_BT_CMD_PREV:
        return "prev";
    case SCOOTERDEMO_BT_CMD_NEXT:
        return "next";
    case SCOOTERDEMO_BT_CMD_METADATA:
        return "metadata";
    default:
        return "unknown";
    }
}

static uint64_t scooterdemo_bt_music_now_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }

    return ((uint64_t)ts.tv_sec * 1000ULL) +
           ((uint64_t)ts.tv_nsec / 1000000ULL);
}

static bool scooterdemo_bt_music_is_control_cmd(scooterdemo_bt_cmd_t cmd)
{
    return cmd == SCOOTERDEMO_BT_CMD_PLAY ||
           cmd == SCOOTERDEMO_BT_CMD_PAUSE ||
           cmd == SCOOTERDEMO_BT_CMD_STOP ||
           cmd == SCOOTERDEMO_BT_CMD_PREV ||
           cmd == SCOOTERDEMO_BT_CMD_NEXT;
}

static bool scooterdemo_bt_music_queue_contains_locked(scooterdemo_bt_cmd_t cmd)
{
    unsigned int i;
    unsigned int index;

    for (i = 0; i < g_bt_music_cmd_count; i++) {
        index = (g_bt_music_cmd_head + i) % SCOOTERDEMO_BT_CMD_QUEUE_DEPTH;
        if (g_bt_music_cmd_queue[index] == cmd) {
            return true;
        }
    }

    return false;
}

static void scooterdemo_bt_music_state_reset_locked(void)
{
    memset(&g_bt_music_state, 0, sizeof(g_bt_music_state));
}

static void scooterdemo_bt_music_copy_text(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }

    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    snprintf(dst, dst_size, "%s", src);
}

static int scooterdemo_bt_uart_configure(int fd)
{
    struct termios tty;

    if (tcgetattr(fd, &tty) != 0) {
        return -errno;
    }

    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag |= CREAD | CLOCAL;
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_oflag &= ~OPOST;
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    if (cfsetispeed(&tty, SCOOTERDEMO_BT_UART_BAUD) != 0 ||
        cfsetospeed(&tty, SCOOTERDEMO_BT_UART_BAUD) != 0) {
        return -errno;
    }

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        return -errno;
    }

    return 0;
}

static int scooterdemo_bt_uart_ensure_open_locked(void)
{
    int fd;
    int ret;
    const char *uart_path = rivotek_platform_config_get()->bt_uart_path;

    if (g_bt_uart_fd >= 0) {
        return 0;
    }

    if (uart_path == NULL || uart_path[0] == '\0') {
        return -EINVAL;
    }

    fd = open(uart_path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        int err = errno;
        if (g_bt_uart_last_open_errno != err) {
            syslog(LOG_ERR,"bt_music(ext): open %s failed: %d\n",
                   uart_path, err);
            g_bt_uart_last_open_errno = err;
        }
        return -err;
    }

    ret = scooterdemo_bt_uart_configure(fd);
    if (ret != 0) {
        syslog(LOG_ERR,"bt_music(ext): uart configure failed: %d\n", -ret);
        close(fd);
        return ret;
    }

    g_bt_uart_fd = fd;
    g_bt_uart_last_open_errno = 0;
    syslog(LOG_ERR,"bt_music(ext): uart ready on %s fd=%d\n",
            uart_path, g_bt_uart_fd);
    return 0;
}

static void scooterdemo_bt_uart_close_locked(void)
{
    if (g_bt_uart_fd >= 0) {
        syslog(LOG_ERR,"bt_music(ext): uart closed fd=%d\n", g_bt_uart_fd);
        close(g_bt_uart_fd);
        g_bt_uart_fd = -1;
    }
}

static const char *scooterdemo_bt_music_cmd_uart_text(scooterdemo_bt_cmd_t cmd)
{
    switch (cmd) {
    case SCOOTERDEMO_BT_CMD_CONNECT_TOGGLE:
        return "AT+BT=TOGGLE\\r\\n";
    case SCOOTERDEMO_BT_CMD_PLAY:
        return "AT+MUSIC=PLAY\\r\\n";
    case SCOOTERDEMO_BT_CMD_PAUSE:
        return "AT+MUSIC=PAUSE\\r\\n";
    case SCOOTERDEMO_BT_CMD_STOP:
        return "AT+MUSIC=STOP\\r\\n";
    case SCOOTERDEMO_BT_CMD_PREV:
        return "AT+MUSIC=PREV\\r\\n";
    case SCOOTERDEMO_BT_CMD_NEXT:
        return "AT+MUSIC=NEXT\\r\\n";
    case SCOOTERDEMO_BT_CMD_METADATA:
        return "AT+MUSIC=INFO\\r\\n";
    default:
        return NULL;
    }
}

static void scooterdemo_bt_music_handle_bt_response(uint8_t sub_cmd,
                                                    const uint8_t *data,
                                                    uint16_t packet_len)
{
    uint8_t response;

    if (data == NULL || packet_len < 1) {
        syslog(LOG_ERR, "bt_music(ext): short bt response sub=0x%02x len=%u\n",
               (unsigned int)sub_cmd,
               (unsigned int)packet_len);
        return;
    }

    response = data[0];

    pthread_mutex_lock(&g_bt_music_lock);
    switch (sub_cmd) {
    case VELA_BT_CMD_GET_STATUS:
        g_bt_music_state.bt_state = (vela_bt_state_t)response;
        g_bt_music_state.connected = (response == VELA_BT_STATE_CONNECTED);
        g_bt_music_state.valid = true;
        syslog(LOG_ERR, "bt_music(ext): bt rsp status=%u connected=%d\n",
               (unsigned int)response,
               g_bt_music_state.connected ? 1 : 0);
        break;
    case VELA_BT_CMD_GET_PLAY_STATUS:
        g_bt_music_state.play_state = (vela_bt_play_state_t)response;
        g_bt_music_state.playing = (response == VELA_BT_PLAY_STATE_PLAYING);
        if (response == VELA_BT_PLAY_STATE_PLAYING ||
            response == VELA_BT_PLAY_STATE_PAUSE) {
            g_bt_music_state.connected = true;
        }
        g_bt_music_state.valid = true;
        syslog(LOG_ERR, "bt_music(ext): bt rsp play_status=%u playing=%d\n",
               (unsigned int)response,
               g_bt_music_state.playing ? 1 : 0);
        break;
    case VELA_BT_CMD_PREV:
    case VELA_BT_CMD_PLAY:
    case VELA_BT_CMD_NEXT:
        syslog(LOG_ERR, "bt_music(ext): bt rsp cmd=0x%02x result=%s\n",
               (unsigned int)sub_cmd,
               response == VELA_CMD_EXECUTE_SUCCESS ? "success" : "fail");
        break;
    default:
        syslog(LOG_ERR, "bt_music(ext): bt rsp unknown sub=0x%02x value=0x%02x\n",
               (unsigned int)sub_cmd,
               (unsigned int)response);
        break;
    }
    pthread_mutex_unlock(&g_bt_music_lock);
}


// 发送二进制命令包
static int scooterdemo_bt_music_send_cmd(scooterdemo_bt_cmd_t cmd)
{
    uint8_t tx_buffer[6];
    size_t tx_len = 4;
    uint8_t sub_cmd = 0;
    switch (cmd) {
        case SCOOTERDEMO_BT_CMD_PLAY:
            sub_cmd = VELA_BT_CMD_PLAY; break;
        case SCOOTERDEMO_BT_CMD_PREV:
            sub_cmd = VELA_BT_CMD_PREV; break;
        case SCOOTERDEMO_BT_CMD_NEXT:
            sub_cmd = VELA_BT_CMD_NEXT; break;
        case SCOOTERDEMO_BT_CMD_METADATA:
            syslog(LOG_ERR, "bt_music(ext): metadata request ignored, waiting active report\n");
            return 0;
        case SCOOTERDEMO_BT_CMD_CONNECT_TOGGLE:
            sub_cmd = VELA_BT_CMD_GET_STATUS; break; // 可根据协议调整
        case SCOOTERDEMO_BT_CMD_STOP:
            // 可扩展
            sub_cmd = VELA_BT_CMD_PLAY; break;
        case SCOOTERDEMO_BT_CMD_PAUSE:
            sub_cmd = VELA_BT_CMD_PLAY; break;
        default:
            syslog(LOG_ERR,"bt_music(ext): unsupported cmd=%d\n", cmd);
            return -EINVAL;
    }
    tx_buffer[0] = VELA_CMD_BLUETOOTH;
    tx_buffer[1] = sub_cmd;
    tx_buffer[2] = 0x00; // LEN_LOW
    tx_buffer[3] = 0x00; // LEN_HIGH
    pthread_mutex_lock(&g_bt_music_lock);
    if (scooterdemo_bt_uart_ensure_open_locked() != 0) {
        pthread_mutex_unlock(&g_bt_music_lock);
        syslog(LOG_ERR,"bt_music(ext): send %s failed: uart not ready\n",
               scooterdemo_bt_music_cmd_name(cmd));
        return -ENODEV;
    }
    ssize_t n = write(g_bt_uart_fd, tx_buffer, tx_len);
    if (n == (ssize_t)tx_len) {
        (void)tcdrain(g_bt_uart_fd);
    }
    pthread_mutex_unlock(&g_bt_music_lock);
    if (n == (ssize_t)tx_len) {
        syslog(LOG_ERR,"bt_music(ext): tx cmd=%s sub=0x%02x len=%u\n",
               scooterdemo_bt_music_cmd_name(cmd),
               (unsigned int)sub_cmd,
               (unsigned int)tx_len);
        return 0;
    }

    syslog(LOG_ERR,"bt_music(ext): tx short write cmd=%s wrote=%d expect=%u err=%d\n",
           scooterdemo_bt_music_cmd_name(cmd),
           (int)n,
           (unsigned int)tx_len,
           errno);
    return -EIO;
}

static void *scooterdemo_bt_music_reader_thread(void *arg)
{
    uint8_t rx_buffer[BT_COMM_RX_BUFFER_SIZE];
    size_t rx_pos = 0;
    (void)arg;

    while (g_bt_music_reader_running) {
        int fd;
        scooterdemo_bt_music_sync_focus_owner_fallback();
        pthread_mutex_lock(&g_bt_music_lock);
        if (scooterdemo_bt_uart_ensure_open_locked() != 0) {
            pthread_mutex_unlock(&g_bt_music_lock);
            usleep(SCOOTERDEMO_BT_POLL_US);
            continue;
        }
        fd = g_bt_uart_fd;
        pthread_mutex_unlock(&g_bt_music_lock);

        ssize_t n = read(fd, rx_buffer + rx_pos, BT_COMM_RX_BUFFER_SIZE - rx_pos);
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                usleep(SCOOTERDEMO_BT_POLL_US);
                continue;
            }
            syslog(LOG_ERR,"bt_music(ext): uart read failed: %d\n", errno);
            pthread_mutex_lock(&g_bt_music_lock);
            scooterdemo_bt_uart_close_locked();
            g_bt_music_state.connected = false;
            pthread_mutex_unlock(&g_bt_music_lock);
            usleep(SCOOTERDEMO_BT_POLL_US);
            continue;
        }
        if (n == 0) {
            usleep(SCOOTERDEMO_BT_POLL_US);
            continue;
        }
        rx_pos += (size_t)n;

        // 解析协议包
        while (rx_pos >= 4) {
            uint8_t type = rx_buffer[0];
            uint8_t sub_type = rx_buffer[1];
            uint16_t packet_len = ((uint16_t)rx_buffer[3] << 8) | rx_buffer[2];
            uint16_t total_len = packet_len + 4;
                 syslog(LOG_ERR,"bt_music(ext): rx packet type=0x%02x(%s) sub=0x%02x payload=%u buffered=%u\n",
                     (unsigned int)type,
                     type == VELA_DATA_LOG ? "log" :
                     type == VELA_DATA_BLUETOOTH_MUSIC ? "music" :
                     type == VELA_DATA_STATUS ? "status" : "unknown",
                     (unsigned int)sub_type,
                     (unsigned int)packet_len,
                     (unsigned int)rx_pos);
            if (total_len > BT_COMM_RX_BUFFER_SIZE || packet_len > 500) {
                // 包过大，丢弃同步
                syslog(LOG_ERR,"bt_music(ext): drop packet oversize total=%u payload=%u\n",
                       (unsigned int)total_len,
                       (unsigned int)packet_len);
                rx_pos = 0;
                break;
            }
            if (rx_pos < total_len) {
                // 数据未收齐
                break;
            }
            // 处理包
            if (type >= 0x80) {
                // 主动上报
                if (type == VELA_DATA_BLUETOOTH_MUSIC) {
                    // 音乐信息
                    uint8_t attr = sub_type;
                    uint8_t *data = &rx_buffer[4];
                    switch (attr) {
                        case VELA_AVRC_ATTR_TITLE:
                            pthread_mutex_lock(&g_bt_music_lock);
                            snprintf(g_bt_music_state.title, sizeof(g_bt_music_state.title), "%.*s", packet_len, data);
                            g_bt_music_state.valid = true;
                            syslog(LOG_ERR,"bt_music(ext): metadata title=%s\n", g_bt_music_state.title);
                            pthread_mutex_unlock(&g_bt_music_lock);
                            break;
                        case VELA_AVRC_ATTR_ARTIST:
                            pthread_mutex_lock(&g_bt_music_lock);
                            snprintf(g_bt_music_state.artist, sizeof(g_bt_music_state.artist), "%.*s", packet_len, data);
                            g_bt_music_state.valid = true;
                            syslog(LOG_ERR,"bt_music(ext): metadata artist=%s\n", g_bt_music_state.artist);
                            pthread_mutex_unlock(&g_bt_music_lock);
                            break;
                        case VELA_AVRC_ATTR_ALBUM:
                            pthread_mutex_lock(&g_bt_music_lock);
                            snprintf(g_bt_music_state.album, sizeof(g_bt_music_state.album), "%.*s", packet_len, data);
                            g_bt_music_state.valid = true;
                            syslog(LOG_ERR,"bt_music(ext): metadata album=%s\n", g_bt_music_state.album);
                            pthread_mutex_unlock(&g_bt_music_lock);
                            break;
                        case VELA_AVRC_ATTR_PLAY_POS:
                            if (packet_len >= 4) {
                                uint32_t pos = ((uint32_t)data[0]) | ((uint32_t)data[1] << 8) |
                                               ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
                                pthread_mutex_lock(&g_bt_music_lock);
                                g_bt_music_state.play_pos = pos;
                                g_bt_music_state.valid = true;
                                syslog(LOG_ERR,"bt_music(ext): metadata play_pos=%u\n", (unsigned int)pos);
                                pthread_mutex_unlock(&g_bt_music_lock);
                            }
                            break;
                        case VELA_AVRC_ATTR_PLAYING_TIME:
                            if (packet_len >= 4) {
                                uint32_t t = ((uint32_t)data[0]) | ((uint32_t)data[1] << 8) |
                                             ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
                                pthread_mutex_lock(&g_bt_music_lock);
                                g_bt_music_state.playing_time = t;
                                g_bt_music_state.duration_ms = t;
                                g_bt_music_state.valid = true;
                                syslog(LOG_ERR,"bt_music(ext): metadata duration=%u\n", (unsigned int)t);
                                pthread_mutex_unlock(&g_bt_music_lock);
                            }
                            break;
                        default:
                            syslog(LOG_ERR,"bt_music(ext): unknown music attr=0x%02x len=%u\n",
                                   (unsigned int)attr,
                                   (unsigned int)packet_len);
                            break;
                    }
                } else if (type == VELA_DATA_STATUS) {
                    // 状态信息
                    uint8_t status_type = sub_type;
                    uint8_t *data = &rx_buffer[4];
                    if (packet_len >= 1) {
                        uint8_t value = data[0];
                        pthread_mutex_lock(&g_bt_music_lock);
                        switch (status_type) {
                            case VELA_STATUS_BLUETOOTH:
                                g_bt_music_state.bt_state = (vela_bt_state_t)value;
                                g_bt_music_state.connected = (value == VELA_BT_STATE_CONNECTED);
                                g_bt_music_state.valid = true;
                                syslog(LOG_ERR,"bt_music(ext): status bt=%u connected=%d\n",
                                       (unsigned int)value,
                                       g_bt_music_state.connected ? 1 : 0);
                                break;
                            case VELA_STATUS_PLAY_STATE:
                                g_bt_music_state.play_state = (vela_bt_play_state_t)value;
                                g_bt_music_state.playing = (value == VELA_BT_PLAY_STATE_PLAYING);
                                if (value == VELA_BT_PLAY_STATE_PLAYING ||
                                    value == VELA_BT_PLAY_STATE_PAUSE) {
                                    g_bt_music_state.connected = true;
                                }
                                g_bt_music_state.valid = true;
                                syslog(LOG_ERR,"bt_music(ext): status play=%u playing=%d\n",
                                       (unsigned int)value,
                                       g_bt_music_state.playing ? 1 : 0);
                                break;
                            case VELA_STATUS_AUDIO_SOURCE:
                                syslog(LOG_ERR,"bt_music(ext): status audio_source=%u\n",
                                       (unsigned int)value);
                                break;
                            default:
                                syslog(LOG_ERR,"bt_music(ext): unknown status type=0x%02x value=%u\n",
                                       (unsigned int)status_type,
                                       (unsigned int)value);
                                break;
                        }
                        pthread_mutex_unlock(&g_bt_music_lock);
                    }
                }
                // 其他类型可扩展
            } else {
                syslog(LOG_ERR,"bt_music(ext): rsp cmd=0x%02x sub=0x%02x len=%u\n",
                       (unsigned int)type,
                       (unsigned int)sub_type,
                       (unsigned int)packet_len);

                if (type == VELA_CMD_BLUETOOTH) {
                    scooterdemo_bt_music_handle_bt_response(sub_type,
                                                            &rx_buffer[4],
                                                            packet_len);
                }
            }
            // 移动缓冲区
            if (rx_pos > total_len) {
                memmove(rx_buffer, rx_buffer + total_len, rx_pos - total_len);
            }
            rx_pos -= total_len;
        }
        usleep(SCOOTERDEMO_BT_POLL_US);
    }
    pthread_mutex_lock(&g_bt_music_lock);
    scooterdemo_bt_uart_close_locked();
    pthread_mutex_unlock(&g_bt_music_lock);
    return NULL;
}

static void *scooterdemo_bt_music_cmd_thread(void *arg)
{
    scooterdemo_bt_cmd_t queued_cmd;

    (void)arg;

    while (g_bt_music_cmd_running) {
        pthread_mutex_lock(&g_bt_music_cmd_lock);
        while (g_bt_music_cmd_running && g_bt_music_cmd_count == 0) {
            pthread_cond_wait(&g_bt_music_cmd_cond, &g_bt_music_cmd_lock);
        }

        if (!g_bt_music_cmd_running) {
            pthread_mutex_unlock(&g_bt_music_cmd_lock);
            break;
        }

        queued_cmd = g_bt_music_cmd_queue[g_bt_music_cmd_head];
        g_bt_music_cmd_head = (g_bt_music_cmd_head + 1) % SCOOTERDEMO_BT_CMD_QUEUE_DEPTH;
        g_bt_music_cmd_count--;
        pthread_mutex_unlock(&g_bt_music_cmd_lock);

        if (scooterdemo_bt_music_send_cmd(queued_cmd) != 0) {
            syslog(LOG_ERR,"bt_music(ext): command failed: %d\\n", queued_cmd);
        } else {
            syslog(LOG_ERR,"bt_music(ext): command tx complete: %s\n",
                   scooterdemo_bt_music_cmd_name(queued_cmd));
        }
    }

    return NULL;
}

static void scooterdemo_bt_music_start_reader_locked(void)
{
    pthread_attr_t attr;
    int ret;
    syslog(LOG_ERR, "bt_music(ext): start reader locked\n");
    if (g_bt_music_reader_started) {
        return;
    }

    g_bt_music_reader_running = true;
    syslog(LOG_ERR,"bt_music(ext): starting reader thread\n");
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, SCOOTERDEMO_BT_THREAD_STACKSIZE);
    ret = pthread_create(&g_bt_music_reader_tid, &attr,
                         scooterdemo_bt_music_reader_thread, NULL);
    pthread_attr_destroy(&attr);
    if (ret != 0) {
        g_bt_music_reader_running = false;
        syslog(LOG_ERR,"bt_music(ext): reader thread create failed: %d\\n", ret);
        return;
    }

    pthread_detach(g_bt_music_reader_tid);
    g_bt_music_reader_started = true;
}

static void scooterdemo_bt_music_start_cmd_worker(void)
{
    pthread_attr_t attr;
    int ret;

    pthread_mutex_lock(&g_bt_music_cmd_lock);
    if (g_bt_music_cmd_started) {
        pthread_mutex_unlock(&g_bt_music_cmd_lock);
        return;
    }

    g_bt_music_cmd_running = true;
    syslog(LOG_ERR,"bt_music(ext): starting cmd worker\n");
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, SCOOTERDEMO_BT_THREAD_STACKSIZE);
    ret = pthread_create(&g_bt_music_cmd_tid, &attr,
                         scooterdemo_bt_music_cmd_thread, NULL);
    pthread_attr_destroy(&attr);
    if (ret != 0) {
        g_bt_music_cmd_running = false;
        pthread_mutex_unlock(&g_bt_music_cmd_lock);
        syslog(LOG_ERR,"bt_music(ext): command thread create failed: %d\\n", ret);
        return;
    }

    pthread_detach(g_bt_music_cmd_tid);
    g_bt_music_cmd_started = true;
    pthread_mutex_unlock(&g_bt_music_cmd_lock);
}

static int scooterdemo_bt_music_exec_cmd_async(scooterdemo_bt_cmd_t cmd)
{
    unsigned int tail;
    uint64_t now_ms;

    if (scooterdemo_bt_music_cmd_uart_text(cmd) == NULL) {
        return -EINVAL;
    }

    scooterdemo_bt_music_start_cmd_worker();

    pthread_mutex_lock(&g_bt_music_cmd_lock);
    if (!g_bt_music_cmd_started) {
        pthread_mutex_unlock(&g_bt_music_cmd_lock);
        return -EIO;
    }

    if (cmd == SCOOTERDEMO_BT_CMD_METADATA &&
        scooterdemo_bt_music_queue_contains_locked(SCOOTERDEMO_BT_CMD_METADATA)) {
        pthread_mutex_unlock(&g_bt_music_cmd_lock);
        return 0;
    }

    now_ms = scooterdemo_bt_music_now_ms();
    if (scooterdemo_bt_music_is_control_cmd(cmd) &&
        cmd == g_bt_music_last_cmd &&
        now_ms > 0 &&
        g_bt_music_last_cmd_ms > 0 &&
        now_ms - g_bt_music_last_cmd_ms < SCOOTERDEMO_BT_CMD_DEDUP_MS) {
        pthread_mutex_unlock(&g_bt_music_cmd_lock);
        return 0;
    }

    if (g_bt_music_cmd_count >= SCOOTERDEMO_BT_CMD_QUEUE_DEPTH) {
        syslog(LOG_ERR,"bt_music(ext): queue full cmd=%s\n",
               scooterdemo_bt_music_cmd_name(cmd));
        pthread_mutex_unlock(&g_bt_music_cmd_lock);
        return -EBUSY;
    }

    tail = g_bt_music_cmd_tail;
    g_bt_music_cmd_queue[tail] = cmd;
    g_bt_music_cmd_tail = (tail + 1) % SCOOTERDEMO_BT_CMD_QUEUE_DEPTH;
    g_bt_music_cmd_count++;
        syslog(LOG_ERR,"bt_music(ext): queue push cmd=%s depth=%u\n",
            scooterdemo_bt_music_cmd_name(cmd),
            g_bt_music_cmd_count);
    if (scooterdemo_bt_music_is_control_cmd(cmd)) {
        g_bt_music_last_cmd = cmd;
        g_bt_music_last_cmd_ms = now_ms;
    }

    pthread_cond_signal(&g_bt_music_cmd_cond);
    pthread_mutex_unlock(&g_bt_music_cmd_lock);
    return 0;
}

void rivotek_audio_component_init(void)
{
    int ret;

    pthread_mutex_lock(&g_bt_music_lock);
    g_bt_music_ops = NULL;
    g_bt_music_ops_ctx = NULL;
    scooterdemo_bt_music_state_reset_locked();
    g_bt_music_focus_cb_registered = false;
    g_bt_music_focus_requested = false;
    g_bt_music_resume_after_ai = false;
    scooterdemo_bt_music_start_reader_locked();
    pthread_mutex_unlock(&g_bt_music_lock);

    ret = audio_focus_register_change_cb(scooterdemo_bt_music_focus_change_cb);
    if (ret == 0) {
        pthread_mutex_lock(&g_bt_music_lock);
        g_bt_music_focus_cb_registered = true;
        pthread_mutex_unlock(&g_bt_music_lock);
        syslog(LOG_ERR, "bt_music(ext): focus change callback registered\n");
    } else {
        syslog(LOG_ERR, "bt_music(ext): register focus callback failed: %d\n", ret);
    }
    scooterdemo_bt_music_maybe_request_focus(true);
    g_bt_music_focus_owner_cached = audio_focus_get_owner();
    scooterdemo_bt_music_start_cmd_worker();
}

int rivotek_audio_component_register_ops(const rivotek_audio_component_ops_t *ops, void *ctx)
{
    pthread_mutex_lock(&g_bt_music_lock);
    g_bt_music_ops = ops;
    g_bt_music_ops_ctx = ctx;
    pthread_mutex_unlock(&g_bt_music_lock);
    return 0;
}

int rivotek_audio_component_ensure_ready(void)
{
    const scooterdemo_bt_music_ops_t *ops;
    void *ops_ctx;

    pthread_mutex_lock(&g_bt_music_lock);
    ops = g_bt_music_ops;
    ops_ctx = g_bt_music_ops_ctx;
    pthread_mutex_unlock(&g_bt_music_lock);

    if (ops != NULL && ops->ensure_ready != NULL) {
        return ops->ensure_ready(ops_ctx);
    }

    pthread_mutex_lock(&g_bt_music_lock);
    if (scooterdemo_bt_uart_ensure_open_locked() != 0) {
        pthread_mutex_unlock(&g_bt_music_lock);
        return -ENODEV;
    }
    pthread_mutex_unlock(&g_bt_music_lock);

    scooterdemo_bt_music_start_reader_locked();
    scooterdemo_bt_music_start_cmd_worker();
    return 0;
}

int rivotek_audio_component_connect_toggle(void)
{
    const scooterdemo_bt_music_ops_t *ops;
    void *ops_ctx;

    pthread_mutex_lock(&g_bt_music_lock);
    ops = g_bt_music_ops;
    ops_ctx = g_bt_music_ops_ctx;
    pthread_mutex_unlock(&g_bt_music_lock);

    if (ops != NULL && ops->connect_toggle != NULL) {
        return ops->connect_toggle(ops_ctx);
    }

    return scooterdemo_bt_music_exec_cmd_async(SCOOTERDEMO_BT_CMD_CONNECT_TOGGLE);
}

int rivotek_audio_component_toggle_play(void)
{
    bool playing;

    pthread_mutex_lock(&g_bt_music_lock);
    playing = g_bt_music_state.playing;
    pthread_mutex_unlock(&g_bt_music_lock);

    return scooterdemo_bt_music_exec_cmd_async(playing ?
                                               SCOOTERDEMO_BT_CMD_PAUSE :
                                               SCOOTERDEMO_BT_CMD_PLAY);
}

int rivotek_audio_component_stop(void)
{
    return scooterdemo_bt_music_exec_cmd_async(SCOOTERDEMO_BT_CMD_STOP);
}

int rivotek_audio_component_play_prev(void)
{
    return scooterdemo_bt_music_exec_cmd_async(SCOOTERDEMO_BT_CMD_PREV);
}

int rivotek_audio_component_play_next(void)
{
    return scooterdemo_bt_music_exec_cmd_async(SCOOTERDEMO_BT_CMD_NEXT);
}

int rivotek_audio_component_request_metadata(void)
{
    return scooterdemo_bt_music_exec_cmd_async(SCOOTERDEMO_BT_CMD_METADATA);
}

void rivotek_audio_component_get_title_text(char *buffer, uint32_t buffer_size)
{
    const char *title;

    if (buffer == NULL || buffer_size == 0) {
        return;
    }

    pthread_mutex_lock(&g_bt_music_lock);
    title = (g_bt_music_state.title[0] != '\0') ? g_bt_music_state.title : "等待蓝牙音频";
    snprintf(buffer, buffer_size, "%s", title);
    pthread_mutex_unlock(&g_bt_music_lock);
}

void rivotek_audio_component_get_artist_text(char *buffer, uint32_t buffer_size)
{
    const char *artist;

    if (buffer == NULL || buffer_size == 0) {
        return;
    }

    pthread_mutex_lock(&g_bt_music_lock);

    if (g_bt_music_state.artist[0] != '\0') {
        artist = g_bt_music_state.artist;
    } else if (g_bt_music_state.device_name[0] != '\0') {
        artist = g_bt_music_state.device_name;
    } else {
        artist = g_bt_music_state.connected ? "蓝牙已连接" : "未连接设备";
    }

    snprintf(buffer, buffer_size, "%s", artist);
    pthread_mutex_unlock(&g_bt_music_lock);
}

bool rivotek_audio_component_is_connected(void)
{
    bool connected;

    pthread_mutex_lock(&g_bt_music_lock);
    connected = g_bt_music_state.connected;
    pthread_mutex_unlock(&g_bt_music_lock);
    return connected;
}

bool rivotek_audio_component_is_playing(void)
{
    bool playing;

    pthread_mutex_lock(&g_bt_music_lock);
    playing = g_bt_music_state.playing;
    pthread_mutex_unlock(&g_bt_music_lock);
    return playing;
}

bool rivotek_audio_component_has_metadata(void)
{
    bool has_metadata;

    pthread_mutex_lock(&g_bt_music_lock);
    has_metadata = (g_bt_music_state.title[0] != '\0') ||
                   (g_bt_music_state.artist[0] != '\0') ||
                   (g_bt_music_state.album[0] != '\0') ||
                   (g_bt_music_state.duration_ms > 0);
    pthread_mutex_unlock(&g_bt_music_lock);
    return has_metadata;
}

uint32_t rivotek_audio_component_get_duration_ms(void)
{
    uint32_t duration_ms;

    pthread_mutex_lock(&g_bt_music_lock);
    duration_ms = g_bt_music_state.duration_ms;
    pthread_mutex_unlock(&g_bt_music_lock);
    return duration_ms;
}
