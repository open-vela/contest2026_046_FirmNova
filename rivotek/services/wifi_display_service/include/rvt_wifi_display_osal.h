#ifndef __RVT_WIFI_DISPLAY_OSAL_H__
#define __RVT_WIFI_DISPLAY_OSAL_H__

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

typedef void *rvt_thread_t;
typedef void *rvt_mutex_t;
typedef int rvt_bool_t;
typedef uint32_t rvt_tick_t;

#define RVT_NULL  ((void *)0)
#define RVT_TRUE  1
#define RVT_FALSE 0
#define RVT_WAIT_FOREVER (-1)

#ifndef RVT_TCP_ACCEPT_NONBLOCK
#define RVT_TCP_ACCEPT_NONBLOCK 0
#endif

#ifndef RVT_TCP_ROLE_CLIENT
#define RVT_TCP_ROLE_CLIENT 0
#endif

#ifndef RVT_SOCKET_AVOID_POLL
#define RVT_SOCKET_AVOID_POLL RVT_TCP_ACCEPT_NONBLOCK
#endif

#ifndef RVT_TCP_THREAD_STACK
#define RVT_TCP_THREAD_STACK 4096
#endif

#ifndef RVT_TCP_CLIENT_NONBLOCK_CONNECT
#define RVT_TCP_CLIENT_NONBLOCK_CONNECT 0
#endif

#ifndef RVT_TCP_CLIENT_CONNECT_TIMEOUT_MS
#define RVT_TCP_CLIENT_CONNECT_TIMEOUT_MS 5000
#endif

#ifndef RVT_TCP_CLIENT_CONNECT_POLL_MS
#define RVT_TCP_CLIENT_CONNECT_POLL_MS 100
#endif

typedef void (*rvt_thread_entry_t)(void *arg);
typedef void (*rvt_timer_entry_t)(void *arg);

/*
 * 函数名: rvt_thread_create
 * 入参: name 线程名，entry 入口函数，arg 参数，stack_size 栈大小，priority 优先级，tick 时间片
 * 返回值: 非空表示线程创建成功，空表示失败
 */
rvt_thread_t rvt_thread_create(const char *name, rvt_thread_entry_t entry,
        void *arg, int stack_size, int priority, int tick);

/*
 * 函数名: rvt_thread_startup
 * 入参: thread 线程句柄
 * 返回值: 0 表示启动成功，负值表示失败
 */
int rvt_thread_startup(rvt_thread_t thread);

/*
 * 函数名: rvt_thread_mdelay
 * 入参: ms 延时毫秒数
 * 返回值: 无
 */
void rvt_thread_mdelay(int ms);

/*
 * 函数名: rvt_tick_get
 * 入参: 无
 * 返回值: 当前系统 tick
 */
rvt_tick_t rvt_tick_get(void);

/*
 * 函数名: rvt_tick_from_millisecond
 * 入参: ms 毫秒数
 * 返回值: 对应 tick 数
 */
rvt_tick_t rvt_tick_from_millisecond(int ms);

/*
 * 函数名: rvt_mutex_create
 * 入参: name 互斥锁名称
 * 返回值: 非空表示创建成功，空表示失败
 */
rvt_mutex_t rvt_mutex_create(const char *name);

/*
 * 函数名: rvt_mutex_take
 * 入参: mutex 互斥锁句柄，timeout_ms 等待超时毫秒数，RVT_WAIT_FOREVER 表示一直等待
 * 返回值: 0 表示加锁成功，负值表示失败
 */
int rvt_mutex_take(rvt_mutex_t mutex, int timeout_ms);

/*
 * 函数名: rvt_mutex_release
 * 入参: mutex 互斥锁句柄
 * 返回值: 0 表示释放成功，负值表示失败
 */
int rvt_mutex_release(rvt_mutex_t mutex);

/*
 * 函数名: rvt_timer_create
 * 入参: name 定时器名称，entry 回调函数，arg 参数，period_ms 周期毫秒数
 * 返回值: 非空表示创建成功，空表示失败
 */
void *rvt_timer_create(const char *name, rvt_timer_entry_t entry,
        void *arg, int period_ms);

/*
 * 函数名: rvt_timer_start
 * 入参: timer 定时器句柄
 * 返回值: 0 表示启动成功，负值表示失败
 */
int rvt_timer_start(void *timer);

/*
 * 函数名: rvt_timer_stop
 * 入参: timer 定时器句柄
 * 返回值: 0 表示停止成功，负值表示失败
 */
int rvt_timer_stop(void *timer);

/*
 * 函数名: rvt_timer_delete
 * 入参: timer 定时器句柄
 * 返回值: 0 表示删除成功，负值表示失败
 */
int rvt_timer_delete(void *timer);

/*
 * 函数名: rvt_socket_close
 * 入参: fd socket 描述符
 * 返回值: 0 表示关闭成功，负值表示失败
 */
int rvt_socket_close(int fd);

/*
 * 函数名: rvt_socket_set_nonblocking
 * 入参: fd socket 描述符
 * 返回值: 0 表示设置成功，负值表示失败
 */
int rvt_socket_set_nonblocking(int fd);

/*
 * 函数名: rvt_socket_set_blocking
 * 入参: fd socket 描述符
 * 返回值: 0 表示设置成功，负值表示失败
 */
int rvt_socket_set_blocking(int fd);

static inline void *rvt_malloc(size_t size)
{
    return malloc(size);
}

static inline void rvt_free(void *ptr)
{
    free(ptr);
}

#endif /* __RVT_WIFI_DISPLAY_OSAL_H__ */
