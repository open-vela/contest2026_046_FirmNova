#include "rvt_wifi_display_osal.h"
#include <fcntl.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct rvt_thread_ctx {
    pthread_t tid;
    rvt_thread_entry_t entry;
    void *arg;
    int stack_size;
    int started;
};

struct rvt_mutex_ctx {
    pthread_mutex_t mutex;
};

struct rvt_timer_ctx {
    pthread_t tid;
    rvt_timer_entry_t entry;
    void *arg;
    int period_ms;
    volatile int running;
    int started;
};

static void *rvt_thread_trampoline(void *arg)
{
    struct rvt_thread_ctx *ctx = (struct rvt_thread_ctx *)arg;

    if (ctx && ctx->entry)
        ctx->entry(ctx->arg);

    if (ctx)
        rvt_free(ctx);

    return NULL;
}

rvt_thread_t rvt_thread_create(const char *name, rvt_thread_entry_t entry,
        void *arg, int stack_size, int priority, int tick)
{
    struct rvt_thread_ctx *ctx;

    (void)name;
    (void)priority;
    (void)tick;

    if (!entry)
        return RVT_NULL;

    ctx = (struct rvt_thread_ctx *)rvt_malloc(sizeof(*ctx));
    if (!ctx)
        return RVT_NULL;

    memset(ctx, 0, sizeof(*ctx));
    ctx->entry = entry;
    ctx->arg = arg;
    ctx->stack_size = stack_size;
    return (rvt_thread_t)ctx;
}

int rvt_thread_startup(rvt_thread_t thread)
{
    struct rvt_thread_ctx *ctx = (struct rvt_thread_ctx *)thread;
    pthread_attr_t attr;
    size_t stack_size;
    int ret;

    if (!ctx || ctx->started)
        return -1;

    pthread_attr_init(&attr);
    if (ctx->stack_size > 0) {
        stack_size = (size_t)ctx->stack_size;
#ifdef PTHREAD_STACK_MIN
        if (stack_size < PTHREAD_STACK_MIN)
            stack_size = PTHREAD_STACK_MIN;
#endif
        pthread_attr_setstacksize(&attr, stack_size);
    }

    ctx->started = 1;
    ret = pthread_create(&ctx->tid, &attr, rvt_thread_trampoline, ctx);
    pthread_attr_destroy(&attr);
    if (ret != 0) {
        ctx->started = 0;
        return -1;
    }

    pthread_detach(ctx->tid);
    return 0;
}

void rvt_thread_mdelay(int ms)
{
    if (ms > 0)
        usleep((useconds_t)ms * 1000);
}

rvt_tick_t rvt_tick_get(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (rvt_tick_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

rvt_tick_t rvt_tick_from_millisecond(int ms)
{
    return (rvt_tick_t)ms;
}

rvt_mutex_t rvt_mutex_create(const char *name)
{
    struct rvt_mutex_ctx *ctx;

    (void)name;

    ctx = (struct rvt_mutex_ctx *)rvt_malloc(sizeof(*ctx));
    if (!ctx)
        return RVT_NULL;

    if (pthread_mutex_init(&ctx->mutex, NULL) != 0) {
        rvt_free(ctx);
        return RVT_NULL;
    }

    return (rvt_mutex_t)ctx;
}

int rvt_mutex_take(rvt_mutex_t mutex, int timeout_ms)
{
    struct rvt_mutex_ctx *ctx = (struct rvt_mutex_ctx *)mutex;

    (void)timeout_ms;

    if (!ctx)
        return -1;

    return pthread_mutex_lock(&ctx->mutex) == 0 ? 0 : -1;
}

int rvt_mutex_release(rvt_mutex_t mutex)
{
    struct rvt_mutex_ctx *ctx = (struct rvt_mutex_ctx *)mutex;

    if (!ctx)
        return -1;

    return pthread_mutex_unlock(&ctx->mutex) == 0 ? 0 : -1;
}

static void *rvt_timer_thread(void *arg)
{
    struct rvt_timer_ctx *ctx = (struct rvt_timer_ctx *)arg;

    while (ctx && ctx->running) {
        rvt_thread_mdelay(ctx->period_ms);
        if (ctx->running && ctx->entry)
            ctx->entry(ctx->arg);
    }

    return NULL;
}

void *rvt_timer_create(const char *name, rvt_timer_entry_t entry,
        void *arg, int period_ms)
{
    struct rvt_timer_ctx *ctx;

    (void)name;

    if (!entry || period_ms <= 0)
        return RVT_NULL;

    ctx = (struct rvt_timer_ctx *)rvt_malloc(sizeof(*ctx));
    if (!ctx)
        return RVT_NULL;

    memset(ctx, 0, sizeof(*ctx));
    ctx->entry = entry;
    ctx->arg = arg;
    ctx->period_ms = period_ms;
    return ctx;
}

int rvt_timer_start(void *timer)
{
    struct rvt_timer_ctx *ctx = (struct rvt_timer_ctx *)timer;

    if (!ctx || ctx->running)
        return -1;

    ctx->running = 1;
    ctx->started = 1;
    if (pthread_create(&ctx->tid, NULL, rvt_timer_thread, ctx) != 0) {
        ctx->running = 0;
        ctx->started = 0;
        return -1;
    }

    return 0;
}

int rvt_timer_stop(void *timer)
{
    struct rvt_timer_ctx *ctx = (struct rvt_timer_ctx *)timer;

    if (!ctx)
        return -1;

    ctx->running = 0;
    if (ctx->started) {
        pthread_join(ctx->tid, NULL);
        ctx->started = 0;
    }
    return 0;
}

int rvt_timer_delete(void *timer)
{
    struct rvt_timer_ctx *ctx = (struct rvt_timer_ctx *)timer;

    if (!ctx)
        return -1;

    ctx->running = 0;
    if (ctx->started)
        pthread_join(ctx->tid, NULL);

    rvt_free(ctx);
    return 0;
}

int rvt_socket_close(int fd)
{
    return close(fd);
}

int rvt_socket_set_nonblocking(int fd)
{
    int nonblock = 1;
    int flags = fcntl(fd, F_GETFL, 0);

    /*
     * NuttX socket 的非阻塞语义最终落到 FIONBIO，对 TCP accept 是否走
     * backlog 轮询路径有直接影响。先走 ioctl，fcntl 只作为文件标志兜底。
     */
    if (ioctl(fd, FIONBIO, (unsigned long)&nonblock) != 0)
        return -1;

    if (flags < 0)
        return -1;

    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0 ? 0 : -1;
}

int rvt_socket_set_blocking(int fd)
{
    int nonblock = 0;
    int flags = fcntl(fd, F_GETFL, 0);

    if (ioctl(fd, FIONBIO, (unsigned long)&nonblock) != 0)
        return -1;

    if (flags < 0)
        return -1;

    return fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) == 0 ? 0 : -1;
}
