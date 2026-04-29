/**
 * @file leventloop.h
 * @brief LXCLUA 异步事件循环核心 - 头文件
 * 提供跨平台的事件循环、任务队列、定时器和 I/O 多路复用支持
 * 
 * 平台支持：
 * - Windows: IOCP (I/O Completion Ports)
 * - Linux: epoll
 * - macOS/BSD: kqueue
 * - Fallback: select (通用方案)
 */

#ifndef leventloop_h
#define leventloop_h

#include "lua.h"
#include "lthread.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
** =====================================================================
** 常量定义
** =====================================================================
*/

/** @name 事件类型 */
/**@{*/
#define EV_READ     0x001   /**< 可读事件 */
#define EV_WRITE    0x002   /**< 可写事件 */
#define EV_ERROR    0x004   /**< 错误事件 */
#define EV_ONESHOT  0x008   /**< 一次性事件（触发后自动移除） */
#define EV_PERSIST  0x010   /**< 持久事件（需要手动删除） */
/**@}*/

/** @name 定时器标识符 */
/**@{*/
typedef int64_t ev_timer_id;  /**< 定时器 ID（-1 表示无效） */
#define EV_TIMER_INVALID (-1)
/**@}*/

/** @name 事件循环状态 */
/**@{*/
#define EV_LOOP_RUNNING  0   /**< 运行中 */
#define EV_LOOP_STOPPED  1   /**< 已停止 */
#define EV_LOOP_ERROR    2   /**< 出错 */
/**@}*/

/*
** =====================================================================
** 数据结构声明
** =====================================================================
*/

/** @brief 前向声明 */
struct event_loop;
struct ev_io_watcher;
struct ev_timer;
struct ev_task;

/**
 * @brief I/O 回调函数类型
 * @param loop 事件循环指针
 * @param watcher 观察者对象
 * @param events 触发的事件类型
 */
typedef void (*ev_io_cb)(struct event_loop *loop, struct ev_io_watcher *watcher, int events);

/**
 * @brief 定时器回调函数类型
 * @param loop 事件循环指针
 * @param timer 定时器对象
 */
typedef void (*ev_timer_cb)(struct event_loop *loop, struct ev_timer *timer);

/**
 * @brief 任务回调函数类型（用于任务队列）
 * @param loop 事件循环指针
 * @param task 任务对象
 */
typedef void (*ev_task_cb)(struct event_loop *loop, struct ev_task *task);

/**
 * @brief I/O 观察者结构体
 *
 * 用于监听文件描述符的 I/O 事件（可读/可写/错误）
 */
typedef struct ev_io_watcher {
    int fd;                    /**< 文件描述符 */
    int events;                /**< 监听的事件类型 (EV_READ | EV_WRITE | ...) */
    ev_io_cb callback;         /**< 事件回调函数 */
    void *data;                /**< 用户数据指针 */
    int active;                /**< 是否已激活（已添加到事件循环） */
} ev_io_watcher;

/**
 * @brief 定时器结构体
 *
 * 支持单次触发和周期性定时器，使用最小堆管理
 */
typedef struct ev_timer {
    ev_timer_id id;            /**< 定时器唯一 ID */
    double timeout;            /**< 超时时间（秒，相对于事件循环启动时间） */
    double repeat;             /**< 重复间隔（0 = 单次定时器） */
    ev_timer_cb callback;      /**< 超时回调函数 */
    void *data;                /**< 用户数据指针 */
    int active;                /**< 是否已激活 */
} ev_timer;

/**
 * @brief 任务结构体（用于任务队列）
 *
 * 支持延迟执行和优先级调度
 */
typedef struct ev_task {
    ev_task_cb callback;       /**< 任务回调函数 */
    void *data;                /**< 用户数据指针 */
    double schedule_time;      /**< 计划执行时间（0 = 立即执行） */
    int priority;              /**< 优先级（数值越小优先级越高） */
} ev_task;

/**
 * @brief 事件循环统计信息
 */
typedef struct ev_loop_stats {
    uint64_t total_tasks;      /**< 已处理的总任务数 */
    uint64_t total_timers;     /**< 已触发的总定时器数 */
    uint64_t total_io_events;  /**< 已处理的 I/O 事件数 */
    double uptime;             /**< 运行时间（秒） */
    int pending_tasks;         /**< 待处理任务数 */
    int active_timers;         /**< 活跃定时器数 */
    int active_watchers;       /**< 活跃 I/O 观察者数 */
} ev_loop_stats;

/**
 * @brief 事件循环配置选项
 */
typedef struct ev_loop_config {
    int max_timers;            /**< 最大定时器数量（默认 4096） */
    int max_watchers;          /**< 最大 I/O 观察者数量（默认 1024） */
    int task_queue_size;       /**< 任务队列大小（默认 65536） */
    int thread_pool_size;      /**< 线程池大小（默认 4） */
    int flags;                 /**< 配置标志位 */
} ev_loop_config;

/** @name 配置标志位 */
/**@{*/
#define EV_CONFIG_NO_THREADS  0x01   /**< 禁用线程池（纯单线程模式） */
#define EV_CONFIG_PROFILE     0x02   /**< 启用性能剖析 */
/**@}*/

/**
 * @brief 事件循环主结构体
 *
 * 核心组件：
 * - 任务队列（FIFO + 优先级）
 * - 定时器最小堆
 * - I/O 多路复用（平台相关）
 * - 线程池（可选）
 */
typedef struct event_loop {
    /* 基本信息 */
    int state;                 /**< 循环状态 (EV_LOOP_*) */
    double start_time;         /**< 启动时间戳（用于计算相对时间） */
    
    /* 任务队列 */
    ev_task *task_queue;       /**< 任务队列数组 */
    int task_front;            /**< 队列头部索引 */
    int task_rear;             /**< 队列尾部索引 */
    int task_count;            /**< 当前任务数量 */
    int task_capacity;         /**< 队列容量 */
    l_mutex_t task_lock;       /**< 任务队列互斥锁 */
    
    /* 定时器最小堆 */
    ev_timer *timer_heap;      /**< 定时器堆数组 */
    int timer_count;           /**< 当前定时器数量 */
    int timer_capacity;        /**< 堆容量 */
    l_mutex_t timer_lock;      /**< 定时器互斥锁 */
    ev_timer_id next_timer_id; /**< 下一个定时器 ID */
    
    /* I/O 多路复用（平台相关） */
#if defined(_WIN32)
    void *iocp_handle;         /**< IOCP 句柄 */
#elif defined(__linux__)
    int epoll_fd;              /**< epoll 文件描述符 */
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)
    int kq_fd;                 /**< kqueue 文件描述符 */
#else
    fd_set read_fds;           /**< select 读集合 */
    fd_set write_fds;          /**< select 写集合 */
    int max_fd;                /**< 最大文件描述符 */
#endif
    ev_io_watcher *io_watchers;/**< I/O 观察者数组 */
    int watcher_count;         /**< 当前观察者数量 */
    int watcher_capacity;      /**< 观察者数组容量 */
    l_mutex_t io_lock;         /**< I/O 互斥锁 */
    
    /* 线程池（用于阻塞操作的异步化） */
    l_thread_t *pool_threads;  /**< 线程数组 */
    int pool_size;             /**< 线程池大小 */
    l_cond_t pool_cond;        /**< 线程池条件变量 */
    l_mutex_t pool_lock;       /**< 线程池互斥锁 */
    int pool_running;          /**< 线程池运行标志 */
    
    /* 统计信息 */
    ev_loop_stats stats;       /**< 性能统计 */
    l_mutex_t stats_lock;      /**< 统计互斥锁 */
    
    /* 配置 */
    ev_loop_config config;     /**< 配置选项 */
    
    /* Lua 状态引用（用于回调中操作 Lua 栈） */
    lua_State *L;              /**< 关联的 Lua 状态 */
} event_loop;

/*
** =====================================================================
** 核心 API 函数
** =====================================================================*/

/**
 * @brief 创建新的事件循环
 *
 * @param L 关联的 Lua 状态（可为 NULL）
 * @param config 配置选项（NULL 使用默认值）
 * @return 新的事件循环指针，失败返回 NULL
 */
event_loop *ev_loop_new(lua_State *L, const ev_loop_config *config);

/**
 * @brief 销毁事件循环
 *
 * 释放所有资源，包括定时器、观察者、线程池等。
 *
 * @param loop 要销毁的事件循环
 */
void ev_loop_destroy(event_loop *loop);

/**
 * @brief 运行事件循环（阻塞直到停止）
 *
 * @param loop 事件循环
 * @return 0 成功，非零错误码
 */
int ev_loop_run(event_loop *loop);

/**
 * @brief 停止事件循环
 *
 * 会在当前迭代完成后停止，不会立即终止。
 *
 * @param loop 事件循环
 */
void ev_loop_stop(event_loop *loop);

/**
 * @brief 执行一次事件循环迭代（非阻塞）
 *
 * 处理所有就绪的任务、定时器和 I/O 事件后立即返回。
 *
 * @param loop 事件循环
 * @param timeout 超时时间（秒，0 为立即返回，-1 为无限等待）
 * @return 处理的事件数量，-1 表示错误
 */
int ev_loop_iterate(event_loop *loop, double timeout);

/**
 * @brief 获取事件循环状态
 *
 * @param loop 事件循环
 * @return 当前状态 (EV_LOOP_*)
 */
int ev_loop_get_state(const event_loop *loop);

/**
 * @brief 获取统计信息
 *
 * @param loop 事件循环
 * @param stats 输出统计信息的结构体
 */
void ev_loop_get_stats(const event_loop *loop, ev_loop_stats *stats);

/**
 * @brief 获取当前时间（相对于事件循环启动的时间，单位：秒）
 *
 * @param loop 事件循环
 * @return 相对时间
 */
double ev_loop_now(const event_loop *loop);

/*
** =====================================================================
** 任务队列 API
** =====================================================================*/

/**
 * @brief 向任务队列添加一个任务
 *
 * @param loop 事件循环
 * @param task 要添加的任务（会被复制到内部队列）
 * @return 0 成功，-1 队列满
 */
int ev_post_task(event_loop *loop, const ev_task *task);

/**
 * @brief 添加立即执行的任务（便捷函数）
 *
 * @param loop 事件循环
 * @param callback 回调函数
 * @param data 用户数据
 * @return 0 成功，-1 失败
 */
int ev_post_callback(event_loop *loop, ev_task_cb callback, void *data);

/**
 * @brief 添加延迟任务
 *
 * @param loop 事件循环
 * @param delay_seconds 延迟时间（秒）
 * @param callback 回调函数
 * @param data 用户数据
 * @return 0 成功，-1 失败
 */
int ev_post_delayed(event_loop *loop, double delay_seconds, ev_task_cb callback, void *data);

/*
** =====================================================================
** 定时器 API
** =====================================================================*/

/**
 * @brief 添加定时器
 *
 * @param loop 事件循环
 * @param timer 定时器结构体（id 和 active 字段会被设置）
 * @return 0 成功，-1 失败
 */
int ev_timer_start(event_loop *loop, ev_timer *timer);

/**
 * @brief 停止并移除定时器
 *
 * @param loop 事件循环
 * @param timer 要停止的定时器
 * @return 0 成功，-1 未找到
 */
int ev_timer_stop(event_loop *loop, ev_timer *timer);

/**
 * @brief 通过 ID 停止定时器
 *
 * @param loop 事件循环
 * @param id 定时器 ID
 * @return 0 成功，-1 未找到
 */
int ev_timer_stop_by_id(event_loop *loop, ev_timer_id id);

/**
 * @brief 更新定时器的超时时间
 *
 * @param loop 事件循环
 * @param timer 定时器
 * @param new_timeout 新的超时时间（秒）
 * @return 0 成功，-1 失败
 */
int ev_timer_again(event_loop *loop, ev_timer *timer, double new_timeout);

/*
** =====================================================================
** I/O 观察者 API
** =====================================================================*/

/**
 * @brief 添加 I/O 观察者
 *
 * 开始监听指定文件描述符的 I/O 事件。
 *
 * @param loop 事件循环
 * @param watcher 观察者结构体（active 字段会被设置）
 * @return 0 成功，-1 失败
 */
int ev_io_start(event_loop *loop, ev_io_watcher *watcher);

/**
 * @brief 移除 I/O 观察者
 *
 * @param loop 事件循环
 * @param watcher 要移除的观察者
 * @return 0 成功，-1 未找到
 */
int ev_io_stop(event_loop *loop, ev_io_watcher *watcher);

/**
 * @brief 修改观察者监听的事件类型
 *
 * @param loop 事件循环
 * @param watcher 观察者
 * @param new_events 新的事件类型
 * @return 0 成功，-1 失败
 */
int ev_io_modify(event_loop *loop, ev_io_watcher *watcher, int new_events);

/*
** =====================================================================
** 线程池 API（用于异步化阻塞操作）
** =====================================================================*/

/**
 * @brief 在线程池中执行任务
 *
 * 将阻塞操作放到工作线程中执行，完成后通过回调通知主线程。
 *
 * @param loop 事件循环
 * @param work_func 工作函数（在工作线程中执行）
 * @param work_data 工作数据
 * @param complete_func 完成回调（在主线程事件循环中执行）
 * @param complete_data 完成回调的数据
 * @return 0 成功，-1 线程池未启用或队列满
 */
int ev_run_in_pool(event_loop *loop,
                   void (*work_func)(void *work_data),
                   void *work_data,
                   ev_task_cb complete_func,
                   void *complete_data);

/*
** =====================================================================
** 工具函数
** =====================================================================*/

/**
 * @brief 获取高精度时间戳（秒）
 *
 * @return 当前时间（Unix 时间戳，精确到微秒）
 */
double ev_time(void);

/**
 * @brief 获取单调时钟时间（秒）
 *
 * 不受系统时间调整影响，适合用于计算时间差。
 *
 * @return 单调时钟时间
 */
double ev_monotonic_time(void);

#ifdef __cplusplus
}
#endif

#endif /* leventloop_h */
