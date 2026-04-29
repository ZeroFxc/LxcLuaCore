/**
 * @file leventloop.c
 * @brief LXCLUA 异步事件循环核心实现
 * 提供跨平台的事件驱动架构，支持任务队列、定时器和 I/O 多路复用
 */

#define leventloop_c
#define LUA_LIB

#include "leventloop.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#ifdef _WIN32
  #include <winsock2.h>
  #include <windows.h>
#else
  #include <sys/time.h>
  #include <unistd.h>
  #include <fcntl.h>
  #if defined(__linux__)
    #include <sys/epoll.h>
  #elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)
    #include <sys/event.h>
  #endif
#endif

/*
** =====================================================================
** 默认配置常量
** =====================================================================
*/

#define DEFAULT_MAX_TIMERS      4096
#define DEFAULT_MAX_WATCHERS    1024
#define DEFAULT_TASK_QUEUE_SIZE 65536
#define DEFAULT_THREAD_POOL_SIZE 4

/*
** =====================================================================
** 时间工具函数实现
** =====================================================================
*/

double ev_time(void) {
#ifdef _WIN32
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    uint64_t ticks = (((uint64_t)ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    return (double)(ticks - 116444736000000000ULL) / 10000000.0;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
#endif
}

double ev_monotonic_time(void) {
#ifdef _WIN32
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)freq.QuadPart;
#elif defined(__APPLE__)
    clock_serv_t cclock;
    mach_timespec_t mts;
    host_get_clock_service(mach_host_self(), SYSTEM_CLOCK, &cclock);
    clock_get_time(cclock, &mts);
    mach_port_deallocate(mach_task_self(), cclock);
    return (double)mts.tv_sec + (double)mts.tv_nsec / 1e9;
#elif defined(CLOCK_MONOTONIC)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
#endif
}

/*
** =====================================================================
** 定时器最小堆操作（内部函数）
** =====================================================================
*/

static void timer_heap_up(event_loop *loop, int index) {
    while (index > 0) {
        int parent = (index - 1) / 2;
        if (loop->timer_heap[index].timeout < loop->timer_heap[parent].timeout) {
            ev_timer temp = loop->timer_heap[index];
            loop->timer_heap[index] = loop->timer_heap[parent];
            loop->timer_heap[parent] = temp;
            index = parent;
        } else {
            break;
        }
    }
}

static void timer_heap_down(event_loop *loop, int index) {
    int count = loop->timer_count;
    while (1) {
        int smallest = index;
        int left = 2 * index + 1;
        int right = 2 * index + 2;

        if (left < count && loop->timer_heap[left].timeout < loop->timer_heap[smallest].timeout)
            smallest = left;
        if (right < count && loop->timer_heap[right].timeout < loop->timer_heap[smallest].timeout)
            smallest = right;

        if (smallest != index) {
            ev_timer temp = loop->timer_heap[index];
            loop->timer_heap[index] = loop->timer_heap[smallest];
            loop->timer_heap[smallest] = temp;
            index = smallest;
        } else {
            break;
        }
    }
}

static int find_timer_by_id(event_loop *loop, ev_timer_id id) {
    for (int i = 0; i < loop->timer_count; i++) {
        if (loop->timer_heap[i].id == id) return i;
    }
    return -1;
}

/*
** =====================================================================
** 任务队列操作（内部函数）
** =====================================================================
*/

static int task_queue_is_full(const event_loop *loop) {
    return (loop->task_count >= loop->task_capacity);
}

static void task_queue_push(event_loop *loop, const ev_task *task) {
    if (task_queue_is_full(loop)) return;

    int pos = loop->task_rear;
    loop->task_queue[pos] = *task;
    loop->task_rear = (pos + 1) % loop->task_capacity;
    loop->task_count++;
}

static int task_queue_pop(event_loop *loop, ev_task *out_task) {
    if (loop->task_count == 0) return -1;

    int pos = loop->task_front;
    *out_task = loop->task_queue[pos];
    loop->task_front = (pos + 1) % loop->task_capacity;
    loop->task_count--;
    return 0;
}

/*
** =====================================================================
** 线程池工作线程函数（内部）
** =====================================================================
*/

typedef struct {
    event_loop *loop;
    void (*work_func)(void *);
    void *work_data;
    ev_task_cb complete_func;
    void *complete_data;
} pool_work_item;

static void *pool_worker_thread(void *arg) {
    event_loop *loop = (event_loop *)arg;

    while (1) {
        l_mutex_lock(&loop->pool_lock);

        while (loop->pool_running) {
            l_cond_wait(&loop->pool_cond, &loop->pool_lock);
        }

        if (!loop->pool_running) {
            l_mutex_unlock(&loop->pool_lock);
            break;
        }

        /* 获取工作项 */
        /* TODO: 实现工作队列 */
        
        l_mutex_unlock(&loop->pool_lock);

        /* 执行工作 */
        /* TODO: 执行 work_func */
        
        /* 发布完成回调到主线程 */
        /* TODO: ev_post_callback */
    }

    return NULL;
}

/*
** =====================================================================
** 平台相关 I/O 多路复用初始化/销毁
** =====================================================================*/

static int platform_io_init(event_loop *loop) {
#if defined(_WIN32)
    loop->iocp_handle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (!loop->iocp_handle) return -1;
    return 0;
#elif defined(__linux__)
    loop->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (loop->epoll_fd < 0) return -1;
    return 0;
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)
    loop->kq_fd = kqueue();
    if (loop->kq_fd < 0) return -1;
    return 0;
#else
    FD_ZERO(&loop->read_fds);
    FD_ZERO(&loop->write_fds);
    loop->max_fd = 0;
    return 0;
#endif
}

static void platform_io_destroy(event_loop *loop) {
#if defined(_WIN32)
    if (loop->iocp_handle) {
        CloseHandle(loop->iocp_handle);
        loop->iocp_handle = NULL;
    }
#elif defined(__linux__)
    if (loop->epoll_fd >= 0) {
        close(loop->epoll_fd);
        loop->epoll_fd = -1;
    }
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)
    if (loop->kq_fd >= 0) {
        close(loop->kq_fd);
        loop->kq_fd = -1;
    }
#endif
}

static int platform_io_add(event_loop *loop, ev_io_watcher *watcher) {
#if defined(__linux__)
    struct epoll_event ee;
    memset(&ee, 0, sizeof(ee));
    ee.events = 0;
    if (watcher->events & EV_READ) ee.events |= EPOLLIN;
    if (watcher->events & EV_WRITE) ee.events |= EPOLLOUT;
    if (watcher->events & EV_ONESHOT) ee.events |= EPOLLET;
    ee.data.ptr = watcher;

    int op = watcher->active ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
    if (epoll_ctl(loop->epoll_fd, op, watcher->fd, &ee) < 0) return -1;
    return 0;

#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)
    struct kevent kev;
    EV_SET(&kev, watcher->fd, EVFILT_READ, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, watcher);
    
    if (watcher->events & EV_WRITE) {
        struct kevent kev_write;
        EV_SET(&kev_write, watcher->fd, EVFILT_WRITE, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, watcher);
        if (kevent(loop->kq_fd, &kev_write, 1, NULL, 0, NULL) < 0) return -1;
    }

    if (kevent(loop->kq_fd, &kev, 1, NULL, 0, NULL) < 0) return -1;
    return 0;

#elif defined(_WIN32)
    /* IOCP 需要使用 overlapped I/O，这里简化处理 */
    /* 实际生产环境应使用 CreateFile + ReadFile/WriteFile with OVERLAPPED */
    watcher->active = 1;
    return 0;

#else
    /* Fallback: select */
    if (watcher->fd > loop->max_fd) loop->max_fd = watcher->fd;
    watcher->active = 1;
    return 0;
#endif
}

static int platform_io_remove(event_loop *loop, ev_io_watcher *watcher) {
#if defined(__linux__)
    struct epoll_event ee;
    if (epoll_ctl(loop->epoll_fd, EPOLL_CTL_DEL, watcher->fd, &ee) < 0) return -1;
    return 0;

#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)
    struct kevent kev_read, kev_write;
    EV_SET(&kev_read, watcher->fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
    EV_SET(&kev_write, watcher->fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    kevent(loop->kq_fd, &kev_read, 1, NULL, 0, NULL);
    kevent(loop->kq_fd, &kev_write, 1, NULL, 0, NULL);
    return 0;

#else
    watcher->active = 0;
    return 0;
#endif
}

static int platform_io_poll(event_loop *loop, double timeout) {
    int events_processed = 0;

#if defined(__linux__)
    int max_events = loop->watcher_count;
    if (max_events <= 0) max_events = 64;
    
    struct epoll_event *events = (struct epoll_event *)malloc(sizeof(struct epoll_event) * max_events);
    if (!events) return -1;

    int ms_timeout = (timeout < 0) ? -1 : (int)(timeout * 1000);
    int n = epoll_wait(loop->epoll_fd, events, max_events, ms_timeout);

    if (n > 0) {
        for (int i = 0; i < n; i++) {
            ev_io_watcher *w = (ev_io_watcher *)events[i].data.ptr;
            int revents = 0;
            if (events[i].events & (EPOLLIN | EPOLLERR | EPOLLHUP)) revents |= EV_READ | EV_ERROR;
            if (events[i].events & (EPOLLOUT | EPOLLERR | EPOLLHUP)) revents |= EV_WRITE | EV_ERROR;

            if (w && w->callback) {
                w->callback(loop, w, revents);
                events_processed++;
            }
        }
    }

    free(events);
    return events_processed;

#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)
    int max_events = loop->watcher_count;
    if (max_events <= 0) max_events = 64;
    
    struct kevent *events = (struct kevent *)malloc(sizeof(struct kevent) * max_events);
    if (!events) return -1;

    struct timespec ts;
    if (timeout >= 0) {
        ts.tv_sec = (time_t)timeout;
        ts.tv_nsec = (long)((timeout - ts.tv_sec) * 1e9);
    }

    int n = kevent(loop->kq_fd, NULL, 0, events, max_events, (timeout >= 0) ? &ts : NULL);

    if (n > 0) {
        for (int i = 0; i < n; i++) {
            ev_io_watcher *w = (ev_io_watcher *)events[i].udata;
            int revents = 0;
            if (events[i].filter == EVFILT_READ) revents |= EV_READ;
            if (events[i].filter == EVFILT_WRITE) revents |= EV_WRITE;
            if (events[i].flags & EV_ERROR) revents |= EV_ERROR;

            if (w && w->callback) {
                w->callback(loop, w, revents);
                events_processed++;
            }
        }
    }

    free(events);
    return events_processed;

#elif defined(_WIN32)
    /* IOCP 实现 */
    DWORD bytes_transferred;
    ULONG_PTR completion_key;
    LPOVERLAPPED overlapped;
    DWORD ms_timeout = (timeout < 0) ? INFINITE : (DWORD)(timeout * 1000);

    while (1) {
        BOOL success = GetQueuedCompletionStatus(
            loop->iocp_handle,
            &bytes_transferred,
            &completion_key,
            &overlapped,
            ms_timeout
        );

        if (!success && !overlapped) {
            if (GetLastError() == WAIT_TIMEOUT) break;
            continue;
        }

        if (overlapped) {
            /* 处理完成的 I/O 操作 */
            /* TODO: 从 overlapped 中提取 watcher 并调用回调 */
            events_processed++;
        } else if (completion_key != 0) {
            /* 用户定义的完成消息 */
            events_processed++;
        }
    }

    return events_processed;

#else
    /* Fallback: select */
    fd_set read_fds, write_fds;
    struct timeval tv, *tvp;

    memcpy(&read_fds, &loop->read_fds, sizeof(fd_set));
    memcpy(&write_fds, &loop->write_fds, sizeof(fd_set));

    if (timeout >= 0) {
        tv.tv_sec = (long)timeout;
        tv.tv_usec = (long)((timeout - tv.tv_sec) * 1e6);
        tvp = &tv;
    } else {
        tvp = NULL;
    }

    int ret = select(loop->max_fd + 1, &read_fds, &write_fds, NULL, tvp);
    if (ret > 0) {
        for (int i = 0; i < loop->watcher_count; i++) {
            ev_io_watcher *w = &loop->io_watchers[i];
            if (!w->active) continue;

            int revents = 0;
            if ((w->events & EV_READ) && FD_ISSET(w->fd, &read_fds)) revents |= EV_READ;
            if ((w->events & EV_WRITE) && FD_ISSET(w->fd, &write_fds)) revents |= EV_WRITE;

            if (revents && w->callback) {
                w->callback(loop, w, revents);
                events_processed++;
            }
        }
    }

    return events_processed;
#endif
}

/*
** =====================================================================
** 核心公共 API 实现
** =====================================================================*/

event_loop *ev_loop_new(lua_State *L, const ev_loop_config *config) {
    event_loop *loop = (event_loop *)calloc(1, sizeof(event_loop));
    if (!loop) return NULL;

    loop->L = L;
    loop->state = EV_LOOP_STOPPED;
    loop->start_time = ev_monotonic_time();

    /* 应用配置或使用默认值 */
    if (config) {
        loop->config = *config;
    } else {
        loop->config.max_timers = DEFAULT_MAX_TIMERS;
        loop->config.max_watchers = DEFAULT_MAX_WATCHERS;
        loop->config.task_queue_size = DEFAULT_TASK_QUEUE_SIZE;
        loop->config.thread_pool_size = DEFAULT_THREAD_POOL_SIZE;
    }

    /* 初始化任务队列 */
    loop->task_capacity = loop->config.task_queue_size;
    loop->task_queue = (ev_task *)malloc(sizeof(ev_task) * loop->task_capacity);
    if (!loop->task_queue) goto fail;
    loop->task_front = 0;
    loop->task_rear = 0;
    loop->task_count = 0;
    l_mutex_init(&loop->task_lock);

    /* 初始化定时器堆 */
    loop->timer_capacity = loop->config.max_timers;
    loop->timer_heap = (ev_timer *)malloc(sizeof(ev_timer) * loop->timer_capacity);
    if (!loop->timer_heap) goto fail;
    loop->timer_count = 0;
    loop->next_timer_id = 1;
    l_mutex_init(&loop->timer_lock);

    /* 初始化 I/O 观察者数组 */
    loop->watcher_capacity = loop->config.max_watchers;
    loop->io_watchers = (ev_io_watcher *)malloc(sizeof(ev_io_watcher) * loop->watcher_capacity);
    if (!loop->io_watchers) goto fail;
    loop->watcher_count = 0;
    l_mutex_init(&loop->io_lock);

    /* 初始化 I/O 多路复用 */
    if (platform_io_init(loop) != 0) goto fail;

    /* 初始化统计信息互斥锁 */
    l_mutex_init(&loop->stats_lock);

    /* 初始化线程池（如果启用） */
    if (!(loop->config.flags & EV_CONFIG_NO_THREADS) && loop->config.thread_pool_size > 0) {
        loop->pool_size = loop->config.thread_pool_size;
        loop->pool_threads = (l_thread_t *)malloc(sizeof(l_thread_t) * loop->pool_size);
        if (loop->pool_threads) {
            l_mutex_init(&loop->pool_lock);
            l_cond_init(&loop->pool_cond);
            loop->pool_running = 1;

            for (int i = 0; i < loop->pool_size; i++) {
                l_thread_create(&loop->pool_threads[i], pool_worker_thread, loop);
            }
        }
    }

    return loop;

fail:
    ev_loop_destroy(loop);
    return NULL;
}

void ev_loop_destroy(event_loop *loop) {
    if (!loop) return;

    /* 停止事件循环 */
    if (loop->state == EV_LOOP_RUNNING) {
        ev_loop_stop(loop);
    }

    /* 停止线程池 */
    if (loop->pool_threads) {
        l_mutex_lock(&loop->pool_lock);
        loop->pool_running = 0;
        l_cond_broadcast(&loop->pool_cond);
        l_mutex_unlock(&loop->pool_lock);

        for (int i = 0; i < loop->pool_size; i++) {
            void *retval;
            l_thread_join(loop->pool_threads[i], &retval);
        }

        l_cond_destroy(&loop->pool_cond);
        l_mutex_destroy(&loop->pool_lock);
        free(loop->pool_threads);
    }

    /* 销毁同步原语 */
    l_mutex_destroy(&loop->task_lock);
    l_mutex_destroy(&loop->timer_lock);
    l_mutex_destroy(&loop->io_lock);
    l_mutex_destroy(&loop->stats_lock);

    /* 释放内存 */
    free(loop->task_queue);
    free(loop->timer_heap);
    free(loop->io_watchers);

    /* 销毁 I/O 多路复用 */
    platform_io_destroy(loop);

    free(loop);
}

int ev_loop_run(event_loop *loop) {
    if (!loop || loop->state == EV_LOOP_RUNNING) return -1;

    loop->state = EV_LOOP_RUNNING;
    loop->start_time = ev_monotonic_time();

    while (loop->state == EV_LOOP_RUNNING) {
        int ret = ev_loop_iterate(loop, -1);
        if (ret < 0 && errno != EINTR) {
            loop->state = EV_LOOP_ERROR;
            break;
        }
    }

    return (loop->state == EV_LOOP_STOPPED) ? 0 : -1;
}

void ev_loop_stop(event_loop *loop) {
    if (loop) {
        loop->state = EV_LOOP_STOPPED;
    }
}

int ev_loop_iterate(event_loop *loop, double timeout) {
    if (!loop) return 0;

    /* 注意：允许在 EV_LOOP_STOPPED 状态下执行单次迭代 */
    /* 这样 promise_await_sync 可以驱动事件循环推进异步操作 */
    
    double now = ev_monotonic_time() - loop->start_time;

    /* 1. 处理就绪的任务队列 */
    l_mutex_lock(&loop->task_lock);
    while (loop->task_count > 0) {
        ev_task task;
        if (task_queue_pop(loop, &task) == 0) {
            l_mutex_unlock(&loop->task_lock);

            if (task.callback) {
                task.callback(loop, &task);
                
                l_mutex_lock(&loop->stats_lock);
                loop->stats.total_tasks++;
                l_mutex_unlock(&loop->stats_lock);
            }

            l_mutex_lock(&loop->task_lock);
        } else {
            break;
        }
    }
    l_mutex_unlock(&loop->task_lock);

    /* 2. 处理到期的定时器 */
    l_mutex_lock(&loop->timer_lock);
    while (loop->timer_count > 0) {
        ev_timer *next = &loop->timer_heap[0];
        if (next->timeout <= now) {
            ev_timer timer_copy = *next;

            /* 移除堆顶元素 */
            loop->timer_heap[0] = loop->timer_heap[--loop->timer_count];
            if (loop->timer_count > 0) {
                timer_heap_down(loop, 0);
            }
            timer_copy.active = 0;

            l_mutex_unlock(&loop->timer_lock);

            if (timer_copy.callback) {
                timer_copy.callback(loop, &timer_copy);
                
                l_mutex_lock(&loop->stats_lock);
                loop->stats.total_timers++;
                l_mutex_unlock(&loop->stats_lock);
            }

            /* 如果是周期性定时器，重新插入 */
            if (timer_copy.repeat > 0) {
                timer_copy.timeout = now + timer_copy.repeat;
                timer_copy.active = 1;
                ev_timer_start(loop, &timer_copy);
            }

            l_mutex_lock(&loop->timer_lock);
        } else {
            break;
        }
    }

    /* 计算下一个定时器的超时时间 */
    double next_timer_timeout = -1;
    if (loop->timer_count > 0) {
        next_timer_timeout = loop->timer_heap[0].timeout - now;
        if (next_timer_timeout < 0) next_timer_timeout = 0;
    }
    l_mutex_unlock(&loop->timer_lock);

    /* 3. I/O 多路复用等待 */
    double io_timeout = timeout;
    if (timeout < 0) {
        io_timeout = next_timer_timeout;
    } else if (next_timer_timeout >= 0) {
        io_timeout = (timeout < next_timer_timeout) ? timeout : next_timer_timeout;
    }

    int io_events = platform_io_poll(loop, io_timeout);
    
    if (io_events > 0) {
        l_mutex_lock(&loop->stats_lock);
        loop->stats.total_io_events += io_events;
        l_mutex_unlock(&loop->stats_lock);
    }

    /* 更新运行时间 */
    l_mutex_lock(&loop->stats_lock);
    loop->stats.uptime = now;
    loop->stats.pending_tasks = loop->task_count;
    loop->stats.active_timers = loop->timer_count;
    loop->stats.active_watchers = loop->watcher_count;
    l_mutex_unlock(&loop->stats_lock);

    return (loop->task_count > 0) ? 1 : io_events;
}

int ev_loop_get_state(const event_loop *loop) {
    return loop ? loop->state : EV_LOOP_STOPPED;
}

void ev_loop_get_stats(const event_loop *loop, ev_loop_stats *stats) {
    if (loop && stats) {
        l_mutex_lock((l_mutex_t *)&loop->stats_lock);
        *stats = loop->stats;
        l_mutex_unlock((l_mutex_t *)&loop->stats_lock);
    }
}

double ev_loop_now(const event_loop *loop) {
    if (!loop) return 0;
    return ev_monotonic_time() - loop->start_time;
}

/*
** =====================================================================
** 任务队列 API 实现
** =====================================================================*/

int ev_post_task(event_loop *loop, const ev_task *task) {
    if (!loop || !task) return -1;

    l_mutex_lock(&loop->task_lock);
    if (task_queue_is_full(loop)) {
        l_mutex_unlock(&loop->task_lock);
        return -1;
    }

    ev_task local_task = *task;
    if (local_task.schedule_time <= 0) {
        local_task.schedule_time = ev_loop_now(loop);
    }
    task_queue_push(loop, &local_task);
    l_mutex_unlock(&loop->task_lock);

    return 0;
}

int ev_post_callback(event_loop *loop, ev_task_cb callback, void *data) {
    if (!loop || !callback) return -1;

    ev_task task;
    memset(&task, 0, sizeof(task));
    task.callback = callback;
    task.data = data;
    task.priority = 0;

    return ev_post_task(loop, &task);
}

int ev_post_delayed(event_loop *loop, double delay_seconds, ev_task_cb callback, void *data) {
    if (!loop || !callback || delay_seconds < 0) return -1;

    ev_task task;
    memset(&task, 0, sizeof(task));
    task.callback = callback;
    task.data = data;
    task.schedule_time = ev_loop_now(loop) + delay_seconds;
    task.priority = 0;

    return ev_post_task(loop, &task);
}

/*
** =====================================================================
** 定时器 API 实现
** =====================================================================*/

int ev_timer_start(event_loop *loop, ev_timer *timer) {
    if (!loop || !timer) return -1;

    l_mutex_lock(&loop->timer_lock);

    if (loop->timer_count >= loop->timer_capacity) {
        l_mutex_unlock(&loop->timer_lock);
        return -1;
    }

    if (timer->id == EV_TIMER_INVALID) {
        timer->id = loop->next_timer_id++;
    }

    if (timer->timeout <= 0) {
        timer->timeout = ev_loop_now(loop);
    }

    timer->active = 1;

    /* 添加到最小堆 */
    loop->timer_heap[loop->timer_count] = *timer;
    timer_heap_up(loop, loop->timer_count);
    loop->timer_count++;

    l_mutex_unlock(&loop->timer_lock);
    return 0;
}

int ev_timer_stop(event_loop *loop, ev_timer *timer) {
    if (!loop || !timer) return -1;

    return ev_timer_stop_by_id(loop, timer->id);
}

int ev_timer_stop_by_id(event_loop *loop, ev_timer_id id) {
    if (!loop || id == EV_TIMER_INVALID) return -1;

    l_mutex_lock(&loop->timer_lock);

    int idx = find_timer_by_id(loop, id);
    if (idx < 0) {
        l_mutex_unlock(&loop->timer_lock);
        return -1;
    }

    /* 用最后一个元素替换要删除的元素 */
    loop->timer_heap[idx] = loop->timer_heap[--loop->timer_count];
    if (idx < loop->timer_count) {
        timer_heap_down(loop, idx);
        timer_heap_up(loop, idx);
    }

    l_mutex_unlock(&loop->timer_lock);
    return 0;
}

int ev_timer_again(event_loop *loop, ev_timer *timer, double new_timeout) {
    if (!loop || !timer) return -1;

    l_mutex_lock(&loop->timer_lock);

    int idx = find_timer_by_id(loop, timer->id);
    if (idx >= 0) {
        loop->timer_heap[idx].timeout = ev_loop_now(loop) + new_timeout;
        if (idx > 0) timer_heap_up(loop, idx);
        timer_heap_down(loop, idx);
    }

    l_mutex_unlock(&loop->timer_lock);
    return 0;
}

/*
** =====================================================================
** I/O 观察者 API 实现
** =====================================================================*/

int ev_io_start(event_loop *loop, ev_io_watcher *watcher) {
    if (!loop || !watcher || watcher->fd < 0) return -1;

    l_mutex_lock(&loop->io_lock);

    if (watcher->active) {
        l_mutex_unlock(&loop->io_lock);
        return 0;
    }

    if (platform_io_add(loop, watcher) != 0) {
        l_mutex_unlock(&loop->io_lock);
        return -1;
    }

    watcher->active = 1;

    if (loop->watcher_count < loop->watcher_capacity) {
        loop->io_watchers[loop->watcher_count++] = *watcher;
    }

    l_mutex_unlock(&loop->io_lock);
    return 0;
}

int ev_io_stop(event_loop *loop, ev_io_watcher *watcher) {
    if (!loop || !watcher) return -1;

    l_mutex_lock(&loop->io_lock);

    if (!watcher->active) {
        l_mutex_unlock(&loop->io_lock);
        return 0;
    }

    if (platform_io_remove(loop, watcher) != 0) {
        l_mutex_unlock(&loop->io_lock);
        return -1;
    }

    watcher->active = 0;

    for (int i = 0; i < loop->watcher_count; i++) {
        if (&loop->io_watchers[i] == watcher) {
            loop->io_watchers[i] = loop->io_watchers[--loop->watcher_count];
            break;
        }
    }

    l_mutex_unlock(&loop->io_lock);
    return 0;
}

int ev_io_modify(event_loop *loop, ev_io_watcher *watcher, int new_events) {
    if (!loop || !watcher) return -1;

    l_mutex_lock(&loop->io_lock);
    watcher->events = new_events;

    if (watcher->active) {
        platform_io_remove(loop, watcher);
        platform_io_add(loop, watcher);
    }

    l_mutex_unlock(&loop->io_lock);
    return 0;
}

/*
** =====================================================================
** 线程池 API 实现
** =====================================================================*/

int ev_run_in_pool(event_loop *loop,
                   void (*work_func)(void *work_data),
                   void *work_data,
                   ev_task_cb complete_func,
                   void *complete_data) {
    if (!loop || !work_func) return -1;

    /*
     * 当前实现：统一使用同步模式
     * 工作函数和完成回调都在当前线程立即执行
     * 这确保了 Promise 能被正确 resolve/reject
     *
     * TODO: 未来可优化为真正的异步线程池模式
     */

    /* 执行工作函数 */
    work_func(work_data);

    /* 立即执行完成回调 */
    if (complete_func) {
        ev_task task;
        memset(&task, 0, sizeof(task));
        task.callback = complete_func;
        task.data = complete_data;
        complete_func(loop, &task);
    }

    return 0;
}
