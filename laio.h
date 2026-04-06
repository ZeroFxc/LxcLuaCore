/**
 * @file laio.h
 * @brief LXCLUA 异步 I/O 操作库 - 头文件
 * 提供异步文件操作、HTTP请求、DNS解析和定时器功能
 */

#ifndef laio_h
#define laio_h

#include "lua.h"
#include "leventloop.h"
#include "lpromise.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
** =====================================================================
** 异步文件操作 API
** =====================================================================
*/

/**
 * @brief 异步读取文件内容
 *
 * 将阻塞的文件读操作放到工作线程中执行，完成后通过 Promise 返回结果。
 *
 * @param L Lua 状态（栈上有文件路径）
 * @param loop 事件循环
 * @return Promise（完成后 resolve 为文件内容字符串）
 */
promise *aio_file_read(lua_State *L, event_loop *loop);

/**
 * @brief 异步写入文件
 *
 * @param L Lua 状态（栈: path, content, [mode="w"]）
 * @param loop 事件循环
 * @return Promise（完成后 resolve 为写入的字节数）
 */
promise *aio_file_write(lua_State *L, event_loop *loop);

/**
 * @brief 异步追加到文件
 *
 * @param L Lua 状态（栈: path, content）
 * @param loop 事件循环
 * @return Promise
 */
promise *aio_file_append(lua_State *L, event_loop *loop);

/**
 * @brief 异步获取文件信息 (stat)
 *
 * @param L Lua 状态（栈上有文件路径）
 * @param loop 事件循环
 * @return Promise（resolve 为包含 size, mtime, mode 等信息的表）
 */
promise *aio_file_stat(lua_State *L, event_loop *loop);

/**
 * @brief 异步检查文件是否存在
 *
 * @param L Lua 状态（栈上有文件路径）
 * @param loop 事件循环
 * @return Promise（resolve 为布尔值）
 */
promise *aio_file_exists(lua_State *L, event_loop *loop);

/**
 * @brief 异步列出目录内容
 *
 * @param L Lua 状态（栈上有目录路径）
 * @param loop 事件循环
 * @return Promise（resolve 为文件名数组）
 */
promise *aio_readdir(lua_State *L, event_loop *loop);

/*
** =====================================================================
** 异步 HTTP 客户端 API
** =====================================================================
*/

/** @name HTTP 方法常量 */
/**@{*/
#define AIO_HTTP_GET     0
#define AIO_HTTP_POST    1
#define AIO_HTTP_PUT     2
#define AIO_HTTP_DELETE  3
#define AIO_HTTP_HEAD    4
#define AIO_HTTP_PATCH   5
/**@}*/

/**
 * @brief HTTP 响应结构体
 */
typedef struct aio_http_response {
    int status_code;             /**< HTTP 状态码 (200, 404, etc.) */
    char *body;                  /**< 响应体 */
    size_t body_length;          /**< 响应体长度 */
    char **headers;              /**< 响应头数组 */
    int header_count;            /**< 响应头数量 */
    double elapsed_time;         /**< 请求耗时（秒） */
} aio_http_response;

/**
 * @brief 异步 HTTP GET 请求
 *
 * @param L Lua 状态（栈: url [, options]）
 *              options 可包含:
 *                - headers: 表（请求头）
 *                - timeout: 超时时间（秒，默认30）
 *                - follow_redirects: 是否跟随重定向（默认true）
 * @param loop 事件循环
 * @return Promise（resolve 为响应表 {status, body, headers, elapsed}）
 */
promise *aio_http_get(lua_State *L, event_loop *loop);

/**
 * @brief 异步 HTTP POST 请求
 *
 * @param L Lua 状态（栈: url, body [, options]）
 * @param loop 事件循环
 * @return Promise
 */
promise *aio_http_post(lua_State *L, event_loop *loop);

/**
 * @brief 通用异步 HTTP 请求方法
 *
 * @param method HTTP 方法 (AIO_HTTP_*)
 * @param L Lua 状态
 * @param loop 事件循环
 * @return Promise
 */
promise *aio_http_request(int method, lua_State *L, event_loop *loop);

/**
 * @brief 释放 HTTP 响应资源
 *
 * @param resp 要释放的响应指针
 */
void aio_http_response_free(aio_http_response *resp);

/*
** =====================================================================
** 异步 DNS 解析 API
** =====================================================================*/

/**
 * @brief DNS 解析结果结构体
 */
typedef struct aio_dns_result {
    char **addresses;            /**< IP 地址数组 */
    int address_count;           /**< IP 地址数量 */
    char *hostname;              /**< 主机名（原始输入） */
    double elapsed_time;         /**< 解析耗时（秒） */
} aio_dns_result;

/**
 * @brief 异步 DNS 解析（主机名 → IP 地址）
 *
 * @param L Lua 状态（栈上有主机名）
 * @param loop 事件循环
 * @return Promise（resolve 为 IP 地址字符串数组）
 */
promise *aio_dns_resolve(lua_State *L, event_loop *loop);

/**
 * @brief 反向 DNS 解析（IP → 主机名）
 *
 * @param L Lua 状态（栈上有 IP 地址）
 * @param loop 事件循环
 * @return Promise
 */
promise *aio_dns_reverse(lua_State *L, event_loop *loop);

/**
 * @brief 释放 DNS 结果资源
 *
 * @param result 要释放的结果指针
 */
void aio_dns_result_free(aio_dns_result *result);

/*
** =====================================================================
** 定时器与延迟 API
** =====================================================================*/

/**
 * @brief 异步等待指定时间（非阻塞 sleep）
 *
 * 类似 JavaScript 的 setTimeout 或 Python 的 asyncio.sleep()
 *
 * @param seconds 等待时间（秒，支持小数如 0.5 表示500ms）
 * @param L Lua 状态
 * @param loop 事件循环
 * @return Promise（指定时间后 resolve，无返回值）
 */
promise *aio_sleep(double seconds, lua_State *L, event_loop *loop);

/**
 * @brief 创建周期性定时器
 *
 * 每隔 interval 秒调用一次回调。
 *
 * @param interval 间隔时间（秒）
 * @param callback 回调函数（Lua 函数）
 * @param times 执行次数（0 = 无限次，-1 = 执行一次后停止）
 * @param L Lua 状态
 * @param loop 事件循环
 * @return 定时器 ID（用于取消），-1 表示失败
 */
ev_timer_id aio_set_interval(double interval, lua_State *L,
                              int callback_ref, int times,
                              event_loop *loop);

/**
 * @brief 取消定时器
 *
 * @param timer_id 要取消的定时器 ID
 * @param loop 事件循环
 * @return 0 成功，-1 未找到或已过期
 */
int aio_clear_timer(ev_timer_id timer_id, event_loop *loop);

/*
** =====================================================================
** 协程调度与 async/await 集成 API
** =====================================================================*/

/**
 * @brief 将协程包装为异步任务并运行
 *
 * 这是 async/await 语法的核心运行时支持：
 * 1. 创建一个新的 coroutine 来运行 async 函数
 * 2. 当遇到 await 时挂起协程，注册回调
 * 3. 当异步操作完成时恢复协程
 *
 * @param L Lua 状态（栈上有要运行的函数）
 * @param loop 事件循环
 * @return Promise（协程完成后的最终结果）
 */
promise *aio_run_async(lua_State *L, event_loop *loop);

/**
 * @brief 在协程中等待 Promise 完成（await 实现）
 *
 * 必须在由 aio_run_async 创建的协程中调用。
 * 会挂起当前协程直到 Promise settle。
 *
 * @param p 要等待的 Promise
 * @param co_L 协程的 Lua 状态
 * @return 0 成功挂起/恢复，-1 错误
 */
int aco_await(promise *p, lua_State *co_L);

/**
 * @brief 并发运行多个异步函数（类似 Promise.all 的语法糖）
 *
 * @param L Lua 状态（栈上是多个函数）
 * @param loop 事件循环
 * @return Promise（所有函数完成后的结果数组）
 */
promise *aio_parallel(lua_State *L, event_loop *loop);

/**
 * @brief 竞争运行多个异步函数（第一个完成的胜出）
 *
 * @param L Lua 状态
 * @param loop 事件循环
 * @return Promise
 */
promise *aio_race(lua_State *L, event_loop *loop);

/*
** =====================================================================
** 工具与辅助函数
** =====================================================================
*/

/**
 * @brief 获取全局默认事件循环
 *
 * 如果不存在则创建一个。
 *
 * @param L Lua 状态（用于存储引用）
 * @return 全局事件循环指针
 */
event_loop *aio_get_default_loop(lua_State *L);

/**
 * @brief 设置全局默认事件循环
 *
 * @param L Lua 状态
 * @param loop 要设置为默认的事件循环
 */
void aio_set_default_loop(lua_State *L, event_loop *loop);

/**
 * @brief 初始化异步 I/O 库（注册所有函数到 Lua）
 *
 * @param L Lua 状态
 * @return 1（表：asyncio 模块）
 */
int luaopen_asyncio(lua_State *L);

#ifdef __cplusplus
}
#endif

#endif /* laio_h */
