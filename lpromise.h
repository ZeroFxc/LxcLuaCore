/**
 * @file lpromise.h
 * @brief LXCLUA Promise/Future 异步原语 - 头文件
 * 实现 JavaScript 风格的 Promise，支持链式调用和组合操作
 */

#ifndef lpromise_h
#define lpromise_h

#include "lua.h"
#include "leventloop.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
** =====================================================================
** 常量定义
** =====================================================================
*/

/** @name Promise 状态 */
/**@{*/
#define PROMISE_PENDING    0   /**< 待定状态 */
#define PROMISE_FULFILLED  1   /**< 已完成（成功） */
#define PROMISE_REJECTED  2   /**< 已拒绝（失败） */
/**@}*/

/** @name Promise 元表名称（用于 Lua 绑定） */
/**@{*/
#define PROMISE_METATABLE "asyncio.promise"
/**@}*/

/*
** =====================================================================
** 数据结构声明
** =====================================================================
*/

struct promise;
struct promise_resolver;

/**
 * @brief Promise 回调函数类型（用于 then/catch）
 *
 * @param L Lua 状态
 * @param value 上一个 Promise 的结果值（在栈上）
 * @return 新的 Promise 对象（如果返回 NULL，则自动创建 fulfilled Promise）
 */
typedef struct promise *(*promise_then_cb)(lua_State *L, struct promise *value);

/**
 * @brief Promise 最终回调函数类型（用于 finally）
 *
 * @param L Lua 状态
 * @param promise 当前 Promise
 */
typedef void (*promise_finally_cb)(lua_State *L, struct promise *promise);

/**
 * @brief Promise settle 回调函数类型（用于 async/await）
 *
 * 当 Promise 从 pending 变为 fulfilled/rejected 时调用
 * 用于通知等待中的协程恢复执行
 *
 * @param p 当前 Promise
 */
typedef void (*promise_settled_cb)(struct promise *p);

/**
 * @brief Promise 执行器函数类型（传给 new Promise 的回调）
 *
 * @param L Lua 状态
 * @param resolve 解决函数引用（在注册表中）
 * @param reject 拒绝函数引用（在注册表中）
 * @param data 用户数据
 */
typedef void (*promise_executor)(lua_State *L, int resolve, int reject, void *data);

/**
 * @brief Promise 反应（Reaction）结构体
 *
 * 存储then/catch/finally 注册的回调信息
 */
typedef struct promise_reaction {
    int type;                    /**< 反应类型: 0=then, 1=catch, 2=finally */
    int handler_ref;             /**< 回调函数的注册表引用（LUA_NOREF 表示无） */
    struct promise *child;       /**< 子 Promise（链式调用产生的新 Promise） */
    struct promise_reaction *next;/**< 链表中的下一个反应 */
} promise_reaction;

/**
 * @brief Promise 结果联合体
 */
typedef union promise_result {
    lua_Integer integer;         /**< 整数结果 */
    lua_Number number;           /**< 浮点数结果 */
    int string_ref;              /**< 字符串结果（注册表引用） */
    int table_ref;               /**< 表结果（注册表引用） */
    void *userdata;              /**< 用户数据指针 */
} promise_result;

/**
 * @brief Promise 核心结构体
 *
 * 实现完整的 Promise/A+ 规范：
 * - 三种状态：pending → fulfilled / rejected
 * - 状态不可逆（一旦 settled 不能再改变）
 * - 支持 then/catch/finally 链式调用
 * - 支持组合操作：all/race/allSettled/any
 */
typedef struct promise {
    int state;                   /**< 当前状态 (PROMISE_*) */
    
    /* 结果值或拒绝原因 */
    int result_type;             /**< 结果类型: LUA_TNIL, LUA_TBOOLEAN, LUA_TNUMBER, LUA_TINTEGER(=100), LUA_TSTRING, ... */
    union {
        int boolean_val;         /**< 布尔值 */
        lua_Integer int_val;     /**< 整数值 */
        lua_Number num_val;      /**< 浮点数值 */
        int str_ref;             /**< 字符串（注册表引用） */
        int tbl_ref;             /**< 表（注册表引用） */
        int func_ref;            /**< 函数（注册表引用） */
        void *data;              /**< 轻量用户数据 */
    } result;
    
    /* 反应队列（then/catch/finally 回调列表） */
    promise_reaction *reactions; /**< 反应链表头 */
    int reaction_count;          /**< 反应数量 */
    
    /* 关联的事件循环（可选，用于异步调度） */
    event_loop *loop;            /**< 所属事件循环（可为 NULL） */
    
    /* 异步/await 支持：等待此 Promise 的协程上下文 */
    void *aco_ctx;               /**< 等待中的协程上下文 (coroutine_context*) */
    promise_settled_cb on_settled; /**< settle 回调（用于 async/await 协程恢复） */
    
    /* 元信息 */
    const char *tag;             /**< 调试标签（用于标识 Promise 来源） */
    int ref_count;               /**< 引用计数（用于内存管理） */
} promise;

/**
 * @brief Promise 组合操作的上下文
 *
 * 用于 all/race/allSettled 等静态方法
 */
typedef struct promise_compose_ctx {
    promise **promises;          /**< 子 Promise 数组 */
    int count;                   /**< 子 Promise 数量 */
    int resolved_count;          /**< 已解决的数量 */
    promise *parent;             /**< 父 Promise（组合后的结果） */
    int mode;                    /**< 组合模式: 0=all, 1=race, 2=all_settled, 3=any */
    int *results;                /**< 结果数组（注册表引用数组） */
    l_mutex_t lock;              /**< 互斥锁（线程安全） */
} promise_compose_ctx;

/*
** =====================================================================
** 核心 API 函数
** =====================================================================*/

/**
 * @brief 创建一个新的 pending Promise
 *
 * @param L Lua 状态
 * @param loop 可选的事件循环（NULL 则使用同步模式）
 * @return 新的 Promise 指针
 */
promise *promise_new(lua_State *L, event_loop *loop);

/**
 * @brief 使用执行器创建 Promise
 *
 * 类似 JavaScript 的 new Promise((resolve, reject) => { ... })
 *
 * @param L Lua 状态
 * @param loop 事件循环（可为 NULL）
 * @param executor 执行器函数
 * @param data 传递给执行器的用户数据
 * @return 新的 Promise
 */
promise *promise_new_with_executor(lua_State *L, event_loop *loop,
                                   promise_executor executor, void *data);

/**
 * @brief 增加 Promise 引用计数
 *
 * @param p Promise 指针
 * @return 同一个 Promise 指针
 */
promise *promise_retain(promise *p);

/**
 * @brief 减少 Promise 引用计数，必要时释放内存
 *
 * @param p 要释放的 Promise
 */
void promise_release(promise *p);

/**
 * @brief 解决 Promise（标记为 fulfilled）
 *
 * 一旦 Promise 被 settle，不能再改变状态。
 *
 * @param p 要解决的 Promise
 * @param L Lua 状态（结果值从栈顶获取）
 * @return 0 成功，-1 已经 settled 或错误
 */
int promise_resolve(promise *p, lua_State *L);

/**
 * @brief 拒绝 Promise（标记为 rejected）
 *
 * @param p 要拒绝的 Promise
 * @param L Lua 状态（拒绝原因从栈顶获取）
 * @return 0 成功，-1 已经 settled 或错误
 */
int promise_reject(promise *p, lua_State *L);

/**
 * @brief 获取 Promise 当前状态
 *
 * @param p Promise 指针
 * @return 状态 (PROMISE_*)
 */
int promise_get_state(const promise *p);

/**
 * @brief 将 Promise 结果压入 Lua 栈
 *
 * 如果 Promise 是 pending，压入 nil。
 * 如果是 fulfilled，压入结果值。
 * 如果是 rejected，抛出错误（或压入 nil + 错误消息）。
 *
 * @param p Promise 指针
 * @param L Lua 状态
 * @return 压入栈上的值的数量（通常为 1）
 */
int promise_get_result(promise *p, lua_State *L);

/*
** =====================================================================
** 链式调用 API
** =====================================================================*/

/**
 * @brief 注册 then 回调
 *
 * 当 Promise fulfilled 时调用。
 *
 * @param p 原 Promise
 * @param L Lua 状态（栈上有回调函数）
 * @return 新的子 Promise（支持链式调用）
 */
promise *promise_then(promise *p, lua_State *L);

/**
 * @brief 注册 catch 回调
 *
 * 当 Promise rejected 时调用。
 *
 * @param p 原 Promise
 * @param L Lua 状态（栈上有回调函数）
 * @return 新的子 Promise
 */
promise *promise_catch(promise *p, lua_State *L);

/**
 * @brief 注册 finally 回调
 *
 * 无论 Promise 是 fulfilled 还是 rejected 都会调用。
 *
 * @param p 原 Promise
 * @param L Lua 状态（栈上有回调函数）
 * @return 新的子 Promise（原样透传结果/拒绝原因）
 */
promise *promise_finally(promise *p, lua_State *L);

/*
** =====================================================================
** 静态工具函数 API
** =====================================================================*/

/**
 * @brief 创建已完成的 Promise（fulfilled）
 *
 * @param L Lua 状态（栈上有结果值）
 * @param loop 事件循环（可为 NULL）
 * @return fulfilled 的 Promise
 */
promise *promise_resolved(lua_State *L, event_loop *loop);

/**
 * @brief 创建已拒绝的 Promise（rejected）
 *
 * @param L Lua 状态（栈上有拒绝原因）
 * @param loop 事件循环（可为 NULL）
 * @return rejected 的 Promise
 */
promise *promise_rejected(lua_State *L, event_loop *loop);

/**
 * @brief Promise.all - 等待所有 Promise 完成
 *
 * 所有 Promise 都 fulfilled 时，父 Promise fulfilled，
 * 结果是所有结果的数组。任一 Promise rejected 则立即 reject。
 *
 * @param L Lua 状态（栈上是 Promise 数组/可变参数）
 * @param loop 事件循环
 * @return 组合后的新 Promise
 */
promise *promise_all(lua_State *L, event_loop *loop);

/**
 * @brief Promise.race - 返回第一个 settle 的 Promise 的结果
 *
 * @param L Lua 状态（栈上是 Promise 数组）
 * @param loop 事件循环
 * @return 竞争后的新 Promise
 */
promise *promise_race(lua_State *L, event_loop *loop);

/**
 * @brief Promise.allSettled - 等待所有 Promise settle
 *
 * 不管成功还是失败，等待所有 Promise 完成后返回状态数组。
 *
 * @param L Lua 状态
 * @param loop 事件循环
 * @return 组合后的新 Promise
 */
promise *promise_all_settled(lua_State *L, event_loop *loop);

/**
 * @brief Promise.any - 任一 Promise fulfilled 即完成
 *
 * 所有 Promise 都 rejected 时才 rejected。
 *
 * @param L Lua 状态
 * @param loop 事件循环
 * @return 组合后的新 Promise
 */
promise *promise_any(lua_State *L, event_loop *loop);

/**
 * @brief 将值包装为 Promise（类似 JavaScript 的 Promise.resolve()）
 *
 * 如果传入的是 Promise，直接返回；否则创建 fulfilled Promise。
 *
 * @param L Lua 状态（栈上有要包装的值）
 * @param loop 事件循环
 * @return Promise
 */
promise *promise_wrap_value(lua_State *L, event_loop *loop);

/*
** =====================================================================
** 高级 API
** =====================================================================*/

/**
 * @brief 设置 Promise 的调试标签
 *
 * @param p Promise
 * @param tag 标签字符串
 */
void promise_set_tag(promise *p, const char *tag);

/**
 * @brief 获取 Promise 的调试标签
 *
 * @param p Promise
 * @return 标签字符串（可能为 NULL）
 */
const char *promise_get_tag(const promise *p);

/**
 * @brief 检查 Promise 是否已经 settled
 *
 * @param p Promise
 * @return 1 如果已 settled（fulfilled 或 rejected），0 否则
 */
int promise_is_settled(const promise *p);

/**
 * @brief 同步等待 Promise 完成（阻塞当前线程）
 *
 * ⚠️ 注意：这会阻塞事件循环，仅在特定场景使用！
 * 正常情况下应使用 async/await 或 .then() 链式调用。
 *
 * @param p Promise
 * @param L Lua 状态
 * @param timeout_ms 超时时间（毫秒，-1 为无限等待）
 * @return 0 成功完成，-1 超时或错误
 */
int promise_await_sync(promise *p, lua_State *L, int timeout_ms);

/**
 * @brief 取消 Promise（尝试中断异步操作）
 *
 * 并非所有 Promise 都能被取消，取决于底层实现。
 *
 * @param p Promise
 * @return 0 成功发送取消请求，-1 不支持取消
 */
int promise_cancel(promise *p, lua_State *L);

#ifdef __cplusplus
}
#endif

#endif /* lpromise_h */
