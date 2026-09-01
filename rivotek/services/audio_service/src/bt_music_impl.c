#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "bt_music_impl.h"
#include "bt_music_state_sync.h"

#define SCOOTERDEMO_BT_CMD_MAX 64
#define SCOOTERDEMO_BT_MQ_POLL_US (100 * 1000)
#define SCOOTERDEMO_BT_MQ_THREAD_STACKSIZE 8192
#define SCOOTERDEMO_BT_CMD_QUEUE_DEPTH 8

typedef enum {
    SCOOTERDEMO_BT_CMD_NONE = 0,
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
static scooterdemo_bt_cmd_t g_bt_music_cmd_queue[SCOOTERDEMO_BT_CMD_QUEUE_DEPTH];
static unsigned int g_bt_music_cmd_head;
static unsigned int g_bt_music_cmd_tail;
static unsigned int g_bt_music_cmd_count;
static bt_music_shared_state_t g_bt_music_state;
static scooterdemo_bt_cmd_t g_bt_music_last_cmd = SCOOTERDEMO_BT_CMD_NONE;
static uint64_t g_bt_music_last_cmd_ms;

#define SCOOTERDEMO_BT_CMD_DEDUP_MS 350U

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

static const char *scooterdemo_bt_music_cmd_name(scooterdemo_bt_cmd_t cmd)
{
    switch (cmd) {
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
        return NULL;
    }
}

static const scooterdemo_bt_music_ops_t *bt_music_get_ops(void)
{
    return g_bt_music_ops;
}

static void bt_music_get_ops_snapshot(const scooterdemo_bt_music_ops_t **ops,
                                      void **ctx)
{
    if (ops != NULL) {
        *ops = g_bt_music_ops;
    }

    if (ctx != NULL) {
        *ctx = g_bt_music_ops_ctx;
    }
}

static int bt_music_call_op(int (*op)(void *ctx), void *ctx)
{
    if (op == NULL) {
        return -ENOSYS;
    }

    return op(ctx);
}

static void scooterdemo_bt_music_clear_locked(void)
{
    bt_music_state_reset(&g_bt_music_state);
}

static void scooterdemo_bt_music_apply_shared_state_locked(const bt_music_shared_state_t *shared_state)
{
    if (shared_state == NULL) {
        scooterdemo_bt_music_clear_locked();
        return;
    }

    g_bt_music_state = *shared_state;
}

static int scooterdemo_bt_music_refresh_cache_locked(void)
{
    bt_music_shared_state_t shared_state;

    if (bt_music_state_read(&shared_state) != 0) {
        return g_bt_music_state.connected ? 0 : -ENODATA;
    }

    scooterdemo_bt_music_apply_shared_state_locked(&shared_state);

    return shared_state.connected ? 0 : -ENODATA;
}

static void *scooterdemo_bt_music_reader_thread(void *arg)
{
    struct mq_attr attr;
    bt_music_shared_state_t shared_state;
    mqd_t mq;

    (void)arg;

    memset(&attr, 0, sizeof(attr));
    attr.mq_maxmsg = BT_MUSIC_STATE_MQ_MAXMSG;
    attr.mq_msgsize = sizeof(shared_state);

    mq = mq_open(BT_MUSIC_STATE_MQ_NAME,
                 O_RDONLY | O_CREAT | O_NONBLOCK,
                 0666,
                 &attr);
    if (mq == (mqd_t)-1) {
        printf("bt_music: mq_open(%s) failed: %d\n", BT_MUSIC_STATE_MQ_NAME, errno);
        return NULL;
    }

    while (g_bt_music_reader_running) {
        ssize_t received = mq_receive(mq, (char *)&shared_state, sizeof(shared_state), NULL);

        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }

            if (errno == EAGAIN) {
                usleep(SCOOTERDEMO_BT_MQ_POLL_US);
                continue;
            }

            printf("bt_music: mq_receive failed: %d\n", errno);
            usleep(SCOOTERDEMO_BT_MQ_POLL_US);
            continue;
        }

        if ((size_t)received != sizeof(shared_state)) {
            continue;
        }

        pthread_mutex_lock(&g_bt_music_lock);
        scooterdemo_bt_music_apply_shared_state_locked(&shared_state);
        pthread_mutex_unlock(&g_bt_music_lock);
    }

    mq_close(mq);
    return NULL;
}

static void scooterdemo_bt_music_start_reader_locked(void)
{
    pthread_attr_t attr;
    int ret;

    if (g_bt_music_reader_started) {
        return;
    }

    g_bt_music_reader_running = true;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, SCOOTERDEMO_BT_MQ_THREAD_STACKSIZE);
    ret = pthread_create(&g_bt_music_reader_tid, &attr,
                         scooterdemo_bt_music_reader_thread, NULL);
    pthread_attr_destroy(&attr);
    if (ret != 0) {
        g_bt_music_reader_running = false;
        printf("bt_music: pthread_create failed: %d\n", ret);
        return;
    }

    pthread_detach(g_bt_music_reader_tid);
    g_bt_music_reader_started = true;
}

static int scooterdemo_bt_music_send_cmd(scooterdemo_bt_cmd_t cmd)
{
    const char *cmd_name;
    int ret;

    cmd_name = scooterdemo_bt_music_cmd_name(cmd);
    if (cmd_name == NULL) {
        return -EINVAL;
    }

    ret = bt_music_ctrl_send_cmd(cmd_name);
    if (ret != 0 && ret != -EAGAIN) {
        printf("bt_music: mq cmd=%s ret=%d\n", cmd_name, ret);
    }

    return ret;
}

static void *scooterdemo_bt_music_cmd_thread(void *arg)
{
    scooterdemo_bt_cmd_t queued_cmd;
    const char *cmd_name;

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
            cmd_name = scooterdemo_bt_music_cmd_name(queued_cmd);
            printf("bt_music: command failed: %s\n",
                   cmd_name != NULL ? cmd_name : "unknown");
        }
    }

    return NULL;
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
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, SCOOTERDEMO_BT_MQ_THREAD_STACKSIZE);
    ret = pthread_create(&g_bt_music_cmd_tid, &attr,
                         scooterdemo_bt_music_cmd_thread, NULL);
    pthread_attr_destroy(&attr);
    if (ret != 0) {
        g_bt_music_cmd_running = false;
        pthread_mutex_unlock(&g_bt_music_cmd_lock);
        printf("bt_music: command thread create failed: %d\n", ret);
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

    if (scooterdemo_bt_music_cmd_name(cmd) == NULL) {
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
        pthread_mutex_unlock(&g_bt_music_cmd_lock);
        return -EBUSY;
    }

    tail = g_bt_music_cmd_tail;
    g_bt_music_cmd_queue[tail] = cmd;
    g_bt_music_cmd_tail = (tail + 1) % SCOOTERDEMO_BT_CMD_QUEUE_DEPTH;
    g_bt_music_cmd_count++;
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
    pthread_mutex_lock(&g_bt_music_lock);
    g_bt_music_ops = NULL;
    g_bt_music_ops_ctx = NULL;
    scooterdemo_bt_music_clear_locked();
    (void)scooterdemo_bt_music_refresh_cache_locked();
    scooterdemo_bt_music_start_reader_locked();
    pthread_mutex_unlock(&g_bt_music_lock);

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
    int ret;
    const scooterdemo_bt_music_ops_t *ops;
    void *ops_ctx;

    pthread_mutex_lock(&g_bt_music_lock);
    bt_music_get_ops_snapshot(&ops, &ops_ctx);

    if (!(ops && ops->ensure_ready)) {
        ret = scooterdemo_bt_music_refresh_cache_locked();
        pthread_mutex_unlock(&g_bt_music_lock);
        return ret;
    }

    pthread_mutex_unlock(&g_bt_music_lock);

    return bt_music_call_op(ops->ensure_ready, ops_ctx);
}

int rivotek_audio_component_connect_toggle(void)
{
    int ret;
    const scooterdemo_bt_music_ops_t *ops;
    void *ops_ctx;

    pthread_mutex_lock(&g_bt_music_lock);
    bt_music_get_ops_snapshot(&ops, &ops_ctx);

    if (!(ops && ops->connect_toggle)) {
        ret = scooterdemo_bt_music_refresh_cache_locked();
        pthread_mutex_unlock(&g_bt_music_lock);
        return ret;
    }

    pthread_mutex_unlock(&g_bt_music_lock);

    return bt_music_call_op(ops->connect_toggle, ops_ctx);
}

int rivotek_audio_component_toggle_play(void)
{
    bool playing;

    pthread_mutex_lock(&g_bt_music_lock);
    if (!g_bt_music_state.valid) {
        (void)scooterdemo_bt_music_refresh_cache_locked();
    }
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
