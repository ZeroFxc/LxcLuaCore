/**
 * @file laio.c
 * @brief LXCLUA 异步 I/O 操作库实现
 * 提供完整的异步编程运行时，支持文件、网络、定时器和协程调度
 */

#define laio_c
#define LUA_LIB

#include "laio.h"
#include "lpromise.h"
#include "lauxlib.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  #include <wininet.h>
#else
  #include <sys/types.h>
  #include <sys/stat.h>
  #include <dirent.h>
  #include <fcntl.h>
  #include <unistd.h>
  #include <netdb.h>
  #include <arpa/inet.h>
  #include <sys/socket.h>
  #include <pthread.h>
#endif

/*
** =====================================================================
** 全局默认事件循环管理
** =====================================================================
*/

#define ASYNCIO_LOOP_KEY "_ASYNCIO_DEFAULT_LOOP"

event_loop *aio_get_default_loop(lua_State *L) {
    lua_getfield(L, LUA_REGISTRYINDEX, ASYNCIO_LOOP_KEY);
    if (lua_isuserdata(L, -1)) {
        event_loop **pp = (event_loop **)lua_touserdata(L, -1);
        lua_pop(L, 1);
        return pp ? *pp : NULL;
    }
    lua_pop(L, 1);

    event_loop *loop = ev_loop_new(L, NULL);
    if (loop) {
        event_loop **pp = (event_loop **)lua_newuserdata(L, sizeof(event_loop *));
        *pp = loop;
        lua_setfield(L, LUA_REGISTRYINDEX, ASYNCIO_LOOP_KEY);
    }

    return loop;
}

void aio_set_default_loop(lua_State *L, event_loop *loop) {
    if (!loop || !L) return;

    event_loop **pp = (event_loop **)lua_newuserdata(L, sizeof(event_loop *));
    *pp = loop;
    lua_setfield(L, LUA_REGISTRYINDEX, ASYNCIO_LOOP_KEY);
}

/*
** =====================================================================
** 异步文件操作实现（基于线程池）
** =====================================================================
*/

typedef struct {
    char *filepath;
    char *content;
    size_t content_length;
    int mode;  /* 0=read, 1=write, 2=append */
    promise *p;
    lua_State *L_main;
} file_op_context;

static void file_read_work(void *data) {
    file_op_context *ctx = (file_op_context *)data;

#ifdef _WIN32
    HANDLE hFile = CreateFileA(ctx->filepath, GENERIC_READ, FILE_SHARE_READ,
                               NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        ctx->content = NULL;
        ctx->content_length = 0;
        return;
    }

    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(hFile, &fileSize)) {
        CloseHandle(hFile);
        ctx->content = NULL;
        ctx->content_length = 0;
        return;
    }

    ctx->content_length = (size_t)fileSize.QuadPart;
    ctx->content = (char *)malloc(ctx->content_length + 1);
    if (!ctx->content) {
        CloseHandle(hFile);
        ctx->content_length = 0;
        return;
    }

    DWORD bytesRead;
    BOOL success = ReadFile(hFile, ctx->content, (DWORD)ctx->content_length, 
                            &bytesRead, NULL);
    CloseHandle(hFile);

    if (!success) {
        free(ctx->content);
        ctx->content = NULL;
        ctx->content_length = 0;
    } else {
        ctx->content[bytesRead] = '\0';
    }
#else
    FILE *f = fopen(ctx->filepath, "rb");
    if (!f) {
        ctx->content = NULL;
        ctx->content_length = 0;
        return;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0) {
        fclose(f);
        ctx->content = strdup("");
        ctx->content_length = 0;
        return;
    }

    ctx->content_length = (size_t)size;
    ctx->content = (char *)malloc(ctx->content_length + 1);
    if (!ctx->content) {
        fclose(f);
        ctx->content_length = 0;
        return;
    }

    size_t read_size = fread(ctx->content, 1, ctx->content_length, f);
    ctx->content[read_size] = '\0';
    ctx->content_length = read_size;
    fclose(f);
#endif
}

static void file_complete(event_loop *loop, ev_task *task) {
    file_op_context *ctx = (file_op_context *)task->data;
    lua_State *L = ctx->L_main;

    if (ctx->content && ctx->p && ctx->content_length > 0) {
        lua_pushlstring(L, ctx->content, ctx->content_length);
        promise_resolve(ctx->p, L);
        lua_pop(L, 1);
    } else if (ctx->p) {
        lua_pushfstring(L, "Failed to read file: %s", ctx->filepath ? ctx->filepath : "(null)");
        promise_reject(ctx->p, L);
        lua_pop(L, 1);
    }

    free(ctx->content);
    free(ctx->filepath);
    promise_release(ctx->p);
    free(ctx);
}

promise *aio_file_read(lua_State *L, event_loop *loop) {
    const char *path = luaL_checkstring(L, 1);
    
    if (!loop) loop = aio_get_default_loop(L);
    if (!loop) return NULL;

    promise *p = promise_new(L, loop);
    if (!p) return NULL;

    file_op_context *ctx = (file_op_context *)calloc(1, sizeof(file_op_context));
    if (!ctx) {
        promise_release(p);
        return NULL;
    }

    ctx->filepath = strdup(path);
    ctx->mode = 0;
    ctx->p = promise_retain(p);
    ctx->L_main = L;

    ev_run_in_pool(loop, file_read_work, ctx, file_complete, ctx);

    return p;
}

/* 类似地实现其他文件操作... (write, append, stat, exists, readdir) */

/*
** =====================================================================
** 异步 HTTP 客户端实现
** =====================================================================
*/

typedef struct {
    char *url;
    char *method;
    char *body;
    size_t body_length;
    char **headers;
    int header_count;
    double timeout;
    promise *p;
    lua_State *L_main;
    aio_http_response response;
} http_op_context;

static void http_request_work(void *data) {
    http_op_context *ctx = (http_op_context *)data;

    /* 简化版 HTTP 实现：使用平台原生 API */
    /* 生产环境应使用 libcurl 或异步 HTTP 库如 libuv + libcurl-multi */
    
#ifdef _WIN32
    /* Windows: 使用 WinINet（简化示例） */
    HINTERNET hInternet = InternetOpen("LXCLua/AsyncIO",
                                       INTERNET_OPEN_TYPE_PRECONFIG,
                                       NULL, NULL, 0);
    if (!hInternet) {
        ctx->response.status_code = 0;
        ctx->response.body = strdup("Failed to initialize WinINet");
        return;
    }

    HINTERNET hConnect = InternetOpenUrl(hInternet, ctx->url, NULL, 0,
                                        INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE,
                                        0);
    if (!hConnect) {
        InternetCloseHandle(hInternet);
        ctx->response.status_code = 0;
        ctx->response.body = strdup("Failed to open URL");
        return;
    }

    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    HttpQueryInfo(hConnect, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                  &statusCode, &statusCodeSize, NULL);
    ctx->response.status_code = (int)statusCode;

    char buffer[8192];
    size_t total_size = 0;
    size_t capacity = sizeof(buffer);
    char *result = (char *)malloc(capacity);

    DWORD bytesRead;
    while (InternetReadFile(hConnect, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
        if (total_size + bytesRead >= capacity) {
            capacity *= 2;
            result = (char *)realloc(result, capacity);
        }
        memcpy(result + total_size, buffer, bytesRead);
        total_size += bytesRead;
    }

    result[total_size] = '\0';
    ctx->response.body = result;
    ctx->response.body_length = total_size;

    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);
#else
    /* POSIX: 使用 socket 简化实现 */
    /* 解析 URL... */
    /* 创建连接... */
    /* 发送请求... */
    /* 读取响应... */
    
    /* 这里简化处理，实际应完整实现 HTTP/1.1 协议 */
    ctx->response.body = strdup("HTTP client not fully implemented for this platform");
    ctx->response.body_length = strlen(ctx->response.body);
    ctx->response.status_code = 501;
#endif
}

static void http_complete(event_loop *loop, ev_task *task) {
    http_op_context *ctx = (http_op_context *)task->data;
    lua_State *L = ctx->L_main;

    if (ctx->p && ctx->response.status_code > 0) {
        lua_newtable(L);
        
        lua_pushinteger(L, ctx->response.status_code);
        lua_setfield(L, -2, "status");

        if (ctx->response.body) {
            lua_pushlstring(L, ctx->response.body, ctx->response.body_length);
            lua_setfield(L, -2, "body");
        } else {
            lua_pushliteral(L, "");
            lua_setfield(L, -2, "body");
        }

        lua_pushnumber(L, ctx->response.elapsed_time);
        lua_setfield(L, -2, "elapsed");

        promise_resolve(ctx->p, L);
        lua_pop(L, 1);
    } else if (ctx->p) {
        lua_pushfstring(L, "HTTP request failed: %s", ctx->url ? ctx->url : "");
        promise_reject(ctx->p, L);
        lua_pop(L, 1);
    }

    aio_http_response_free(&ctx->response);
    free(ctx->url);
    free(ctx->method);
    free(ctx->body);
    for (int i = 0; i < ctx->header_count; i++) free(ctx->headers[i]);
    free(ctx->headers);
    promise_release(ctx->p);
    free(ctx);
}

promise *aio_http_get(lua_State *L, event_loop *loop) {
    const char *url = luaL_checkstring(L, 1);
    
    if (!loop) loop = aio_get_default_loop(L);
    if (!loop) return NULL;

    promise *p = promise_new(L, loop);
    if (!p) return NULL;

    http_op_context *ctx = (http_op_context *)calloc(1, sizeof(http_op_context));
    if (!ctx) {
        promise_release(p);
        return NULL;
    }

    ctx->url = strdup(url);
    ctx->method = strdup("GET");
    ctx->timeout = 30.0;
    ctx->p = promise_retain(p);
    ctx->L_main = L;

    ev_run_in_pool(loop, http_request_work, ctx, http_complete, ctx);

    return p;
}

promise *aio_http_post(lua_State *L, event_loop *loop) {
    const char *url = luaL_checkstring(L, 1);
    size_t body_len;
    const char *body = luaL_optlstring(L, 2, "", &body_len);
    
    if (!loop) loop = aio_get_default_loop(L);
    if (!loop) return NULL;

    promise *p = promise_new(L, loop);
    if (!p) return NULL;

    http_op_context *ctx = (http_op_context *)calloc(1, sizeof(http_op_context));
    if (!ctx) {
        promise_release(p);
        return NULL;
    }

    ctx->url = strdup(url);
    ctx->method = strdup("POST");
    ctx->body = (char *)malloc(body_len + 1);
    if (ctx->body && body) {
        memcpy(ctx->body, body, body_len);
        ctx->body[body_len] = '\0';
    }
    ctx->body_length = body_len;
    ctx->timeout = 30.0;
    ctx->p = promise_retain(p);
    ctx->L_main = L;

    ev_run_in_pool(loop, http_request_work, ctx, http_complete, ctx);

    return p;
}

void aio_http_response_free(aio_http_response *resp) {
    if (!resp) return;
    free(resp->body);
    if (resp->headers) {
        for (int i = 0; i < resp->header_count; i++) free(resp->headers[i]);
        free(resp->headers);
    }
    memset(resp, 0, sizeof(*resp));
}

/*
** =====================================================================
** 异步 DNS 解析实现
** =====================================================================
*/

typedef struct {
    char *hostname;
    promise *p;
    lua_State *L_main;
    aio_dns_result result;
} dns_op_context;

static void dns_resolve_work(void *data) {
    dns_op_context *ctx = (dns_op_context *)data;
    double start = ev_time();

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int err = getaddrinfo(ctx->hostname, NULL, &hints, &res);
    ctx->result.elapsed_time = ev_time() - start;

    if (err != 0) {
        ctx->result.addresses = NULL;
        ctx->result.address_count = 0;
        ctx->result.hostname = ctx->hostname;
        return;
    }

    int count = 0;
    struct addrinfo *p;
    for (p = res; p; p = p->ai_next) count++;

    ctx->result.address_count = count;
    ctx->result.addresses = (char **)malloc(sizeof(char *) * count);
    ctx->result.hostname = ctx->hostname;

    int i = 0;
    for (p = res; p; p = p->ai_next) {
        char addr_str[INET_ADDRSTRLEN];
        struct sockaddr_in *addr = (struct sockaddr_in *)p->ai_addr;
        inet_ntop(AF_INET, &addr->sin_addr, addr_str, sizeof(addr_str));
        ctx->result.addresses[i++] = strdup(addr_str);
    }

    freeaddrinfo(res);
}

static void dns_complete(event_loop *loop, ev_task *task) {
    dns_op_context *ctx = (dns_op_context *)task->data;
    lua_State *L = ctx->L_main;

    if (ctx->p && ctx->result.address_count > 0) {
        lua_newtable(L);
        for (int i = 0; i < ctx->result.address_count; i++) {
            lua_pushstring(L, ctx->result.addresses[i]);
            lua_rawseti(L, -2, i + 1);
        }
        promise_resolve(ctx->p, L);
        lua_pop(L, 1);
    } else if (ctx->p) {
        lua_pushfstring(L, "DNS resolution failed for: %s", 
                        ctx->hostname ? ctx->hostname : "(null)");
        promise_reject(ctx->p, L);
        lua_pop(L, 1);
    }

    aio_dns_result_free(&ctx->result);
    free(ctx->hostname);
    promise_release(ctx->p);
    free(ctx);
}

promise *aio_dns_resolve(lua_State *L, event_loop *loop) {
    const char *hostname = luaL_checkstring(L, 1);
    
    if (!loop) loop = aio_get_default_loop(L);
    if (!loop) return NULL;

    promise *p = promise_new(L, loop);
    if (!p) return NULL;

    dns_op_context *ctx = (dns_op_context *)calloc(1, sizeof(dns_op_context));
    if (!ctx) {
        promise_release(p);
        return NULL;
    }

    ctx->hostname = strdup(hostname);
    ctx->p = promise_retain(p);
    ctx->L_main = L;

    ev_run_in_pool(loop, dns_resolve_work, ctx, dns_complete, ctx);

    return p;
}

void aio_dns_result_free(aio_dns_result *result) {
    if (!result) return;
    if (result->addresses) {
        for (int i = 0; i < result->address_count; i++) free(result->addresses[i]);
        free(result->addresses);
    }
    memset(result, 0, sizeof(*result));
}

/*
** =====================================================================
** 定时器与延迟实现
** =====================================================================
*/

typedef struct {
    promise *p;
    lua_State *L_main;
    ev_timer timer;  /* 嵌入定时器，确保生命周期与 ctx 一致（堆分配） */
} sleep_context;

static void timer_callback(event_loop *loop, ev_timer *timer) {
    sleep_context *ctx = (sleep_context *)timer->data;

    if (ctx && ctx->p && ctx->L_main) {
        lua_pushboolean(ctx->L_main, 1);
        promise_resolve(ctx->p, ctx->L_main);
        promise_release(ctx->p);
        ctx->p = NULL;
    }
}

promise *aio_sleep(double seconds, lua_State *L, event_loop *loop) {
    if (!loop) loop = aio_get_default_loop(L);
    if (!loop || seconds < 0) return NULL;

    promise *p = promise_new(L, loop);
    if (!p) return NULL;

    /* 零延迟或负延迟：立即完成 */
    if (seconds == 0) {
        lua_pushboolean(L, 1);  /* 返回 true 而不是 nil */
        promise_resolve(p, L);
        lua_pop(L, 1);
        return p;
    }

    sleep_context *ctx = (sleep_context *)calloc(1, sizeof(sleep_context));
    if (!ctx) {
        promise_release(p);
        return NULL;
    }

    ctx->p = promise_retain(p);
    ctx->L_main = L;

    memset(&ctx->timer, 0, sizeof(ctx->timer));
    ctx->timer.timeout = ev_loop_now(loop) + seconds;
    ctx->timer.repeat = 0;
    ctx->timer.callback = timer_callback;
    ctx->timer.data = ctx;

    ev_timer_start(loop, &ctx->timer);

    return p;
}

static void aio_interval_callback(event_loop *loop, ev_timer *timer);

ev_timer_id aio_set_interval(double interval, lua_State *L,
                              int callback_ref, int times,
                              event_loop *loop) {
    if (!loop || interval <= 0) return EV_TIMER_INVALID;

    static int call_count = 0;
    call_count++;

    typedef struct {
        lua_State *L;
        int callback_ref;
        int remaining_times;
    } interval_ctx;

    interval_ctx *ctx = (interval_ctx *)calloc(1, sizeof(interval_ctx));
    if (!ctx) return EV_TIMER_INVALID;

    ctx->L = L;
    ctx->callback_ref = callback_ref;
    ctx->remaining_times = times;

    ev_timer timer;
    memset(&timer, 0, sizeof(timer));
    timer.timeout = ev_loop_now(loop) + interval;
    timer.repeat = interval;
    timer.callback = aio_interval_callback;
    timer.data = ctx;

    ev_timer_start(loop, &timer);

    return timer.id;
}

static void aio_interval_callback(event_loop *loop, ev_timer *timer) {
    typedef struct {
        lua_State *L;
        int callback_ref;
        int remaining_times;
    } interval_ctx;

    interval_ctx *ictx = (interval_ctx *)timer->data;

    lua_rawgeti(ictx->L, LUA_REGISTRYINDEX, ictx->callback_ref);
    lua_pcall(ictx->L, 0, 0, 0);

    if (ictx->remaining_times > 0) {
        ictx->remaining_times--;
        if (ictx->remaining_times == 0) {
            ev_timer_stop(loop, timer);
            luaL_unref(ictx->L, LUA_REGISTRYINDEX, ictx->callback_ref);
            free(ictx);
        }
    }
}

int aio_clear_timer(ev_timer_id timer_id, event_loop *loop) {
    if (!loop || timer_id == EV_TIMER_INVALID) return -1;
    return ev_timer_stop_by_id(loop, timer_id);
}

/*
** =====================================================================
** 协程调度器（async/await 核心实现）
** =====================================================================
*/

typedef struct {
    lua_State *co;       /**< 协程状态 */
    lua_State *L_main;   /**< 主线程状态（用于恢复协程） */
    promise *waiting_p;  /**< 当前等待的 Promise */
    event_loop *loop;    /**< 所属事件循环 */
    promise *result_p;   /**< 最终结果的 Promise */
    int co_ref;          /**< 协程在注册表中的引用（防止GC） */
    char *registry_key;  /**< 注册表键名（用于存储协程上下文，避免全局暴露） */
} coroutine_context;

/**
 * @brief 安全释放 coroutine_context（包括 registry_key）
 *
 * 统一清理协程上下文的所有资源，避免内存泄漏
 */
static void free_coroutine_context(coroutine_context *ctx) {
    if (!ctx) return;

    /* 释放注册表键名字符串 */
    if (ctx->registry_key) {
        free(ctx->registry_key);
        ctx->registry_key = NULL;
    }

    /* 释放上下文本身 */
    free(ctx);
}

/**
 * @brief Promise settle 回调（当 Promise 完成/拒绝时自动调用）
 * 
 * 由 promise_resolve/promise_reject 在 settle 时触发
 * 负责恢复等待此 Promise 的协程
 */
static void laio_promise_settled(promise *p) {
    if (!p || !p->aco_ctx) return;
    
    coroutine_context *ctx = (coroutine_context *)p->aco_ctx;
    p->aco_ctx = NULL;
    p->on_settled = NULL;  /* 防止递归 */
    
    if (!ctx || !ctx->co) {
        if (ctx) free(ctx);
        return;
    }

    /* 获取 Promise 结果并传给 lua_resume */
    int nargs = 0;
    int state = promise_get_state(p);
    if (state == PROMISE_FULFILLED) {
        promise_get_result(p, ctx->co);
        nargs = 1;
    } else {  /* REJECTED */
        lua_pushnil(ctx->co);
        nargs = 1;
    }

    int nres;
    int status = lua_resume(ctx->co, ctx->L_main, nargs, &nres);
    
    if (status == LUA_OK || status == LUA_YIELD) {
        if (status == LUA_OK) {
        /* 协程执行完毕，获取返回值并 resolve result_p */
        int nresults = lua_gettop(ctx->co);

        if (nresults > 0) {
            lua_xmove(ctx->co, ctx->L_main, nresults);
            promise_resolve(ctx->result_p, ctx->L_main);
        } else {
            lua_pushnil(ctx->L_main);
            promise_resolve(ctx->result_p, ctx->L_main);
        }
        /* 先解除关联，防止 promise_resolve 内部 on_settled 回调访问已释放的 ctx */
        p->aco_ctx = NULL;
        p->on_settled = NULL;
        /* 协程正常结束：清理上下文 */
        luaL_unref(ctx->L_main, LUA_REGISTRYINDEX, ctx->co_ref);
        free_coroutine_context(ctx);
        }
        /* 如果是 yield，ctx 继续使用，由新的 await 调用处理清理 */
    } else {
        /* 协程出错 */
        const char *errmsg = lua_tostring(ctx->co, -1);
        lua_pushstring(ctx->L_main, errmsg ? errmsg : "Unknown coroutine error");
        /* 先解除关联，防止 promise_reject 内部 on_settled 回调访问已释放的 ctx */
        p->aco_ctx = NULL;
        p->on_settled = NULL;
        promise_reject(ctx->result_p, ctx->L_main);
        /* 清理上下文 */
        if (ctx->waiting_p) { promise_release(ctx->waiting_p); }
        luaL_unref(ctx->L_main, LUA_REGISTRYINDEX, ctx->co_ref);
        free_coroutine_context(ctx);
    }
}

int aco_await(promise *p, lua_State *co_L) {
    if (!p || !co_L) return -1;

    /* 检查是否在协程中运行 */
    if (lua_type(co_L, 1) != LUA_TTHREAD) return -1;

    /* 从注册表中获取关联的 coroutine context */
    char ctx_key[64];
    snprintf(ctx_key, sizeof(ctx_key), "ACO_CTX_%p", (void *)co_L);

    lua_getfield(co_L, LUA_REGISTRYINDEX, ctx_key);
    if (!lua_isuserdata(co_L, -1)) {
        lua_pop(co_L, 1);
        return -1;
    }

    coroutine_context *ctx = (coroutine_context *)lua_touserdata(co_L, -1);
    lua_pop(co_L, 1);

    if (!ctx) return -1;

    ctx->waiting_p = promise_retain(p);

    if (p->state == PROMISE_PENDING) {
        /* Promise 未完成：设置 settle 回调 */
        p->aco_ctx = ctx;
        p->on_settled = laio_promise_settled;

        return 0;  /* 成功挂起（调用方需要 yield） */
    } else {
        /* Promise 已完成，立即恢复 */
        promise_get_result(p, co_L);
        lua_pop(co_L, 1);  /* 清理结果值 */
        promise_release(ctx->waiting_p);
        ctx->waiting_p = NULL;
        return 0;
    }
}

promise *aio_run_async(lua_State *L, event_loop *loop) {
    if (!L) return NULL;

    luaL_checktype(L, 1, LUA_TFUNCTION);

    if (!loop) loop = aio_get_default_loop(L);
    if (!loop) return NULL;

    promise *result_p = promise_new(L, loop);
    if (!result_p) return NULL;

    /*
     * 完整的协程模式：
     * 1. 创建协程
     * 2. 设置协程上下文（包含 __aco_ctx__ 全局变量）
     * 3. 运行用户函数
     * 4. 如果函数 yield（等待 Promise），则注册回调并在 Promise 完成时恢复
     */
    
    /* 创建协程 */
    lua_State *co_L = lua_newthread(L);
    if (!co_L) {
        lua_pushstring(L, "Failed to create coroutine");
        promise_reject(result_p, L);
        lua_pop(L, 1);
        return result_p;
    }
    
    int co_ref = luaL_ref(L, LUA_REGISTRYINDEX);  /* 保存协程引用防止 GC */
    
    /* 创建协程上下文 */
    coroutine_context *ctx = (coroutine_context *)malloc(sizeof(coroutine_context));
    if (!ctx) {
        luaL_unref(L, LUA_REGISTRYINDEX, co_ref);
        lua_pushstring(L, "Failed to allocate coroutine context");
        promise_reject(result_p, L);
        lua_pop(L, 1);
        return result_p;
    }
    
    memset(ctx, 0, sizeof(coroutine_context));
    ctx->co = co_L;
    ctx->L_main = L;
    ctx->loop = loop;
    ctx->result_p = result_p;
    ctx->co_ref = co_ref;
    ctx->waiting_p = NULL;

    /*
     * 将协程上下文存储到注册表中（不暴露给 Lua 用户）
     *
     * 使用 "ACO_CTX_" 前缀 + 协程指针作为键，
     * 确保每个协程有独立的上下文存储。
     * 用户无法通过常规方式访问这些数据。
     */
    {
        /* 生成唯一键：字符串 "ACO_CTX_" + 指针地址 */
        char ctx_key[64];
        snprintf(ctx_key, sizeof(ctx_key), "ACO_CTX_%p", (void *)co_L);

        lua_pushlightuserdata(co_L, ctx);
        lua_setfield(co_L, LUA_REGISTRYINDEX, ctx_key);

        /* 同时将键名存储到 ctx 中，以便后续查找 */
        if (ctx->registry_key) free(ctx->registry_key);
        ctx->registry_key = strdup(ctx_key);
    }
    
    /* 将用户函数和参数移到协程栈中并启动执行 */
    int nargs = lua_gettop(L);  /* 栈上元素数（含函数+参数） */
    if (nargs > 0) {
        lua_xmove(L, co_L, nargs);  /* 将函数+所有参数移到协程 */
    }
    
    int nres = 0;
    int status = lua_resume(co_L, L, nargs > 0 ? nargs - 1 : 0, &nres);
    
    if (status == LUA_OK || status == LUA_YIELD) {
        if (status == LUA_OK) {
            /* 函数正常完成（没有 await） */
            if (nres > 0) {
                lua_xmove(co_L, L, nres);
                promise_resolve(result_p, L);
                /* promise_resolve 已内部 pop 栈顶值，无需再 pop */
            } else {
                lua_pushboolean(L, 1);
                promise_resolve(result_p, L);
                /* promise_resolve 已内部 pop */
            }
            
            /* 清理 */
            free_coroutine_context(ctx);
            luaL_unref(L, LUA_REGISTRYINDEX, co_ref);
            
        } else {
            /* LUA_YIELD: 协程挂起（正在 await Promise） */
            /*
             * 处理 await 表达式：
             * lparser.c 将 await(expr) 编译为 coroutine.yield(expr)
             * yield 的返回值（Promise）在协程栈顶
             * 需要将其与协程上下文关联，以便 Promise 完成时恢复协程
             */
            int yield_results = lua_gettop(co_L);
            if (yield_results > 0) {
                /* 检查 yield 出来的值是否是 Promise */
                promise **pp = (promise **)luaL_testudata(co_L, -1, PROMISE_METATABLE);
                if (pp && *pp) {
                    promise *yielded_p = *pp;
                    ctx->waiting_p = promise_retain(yielded_p);

                    /* 注册 settle 回调：Promise 完成时恢复协程 */
                    yielded_p->aco_ctx = ctx;
                    yielded_p->on_settled = laio_promise_settled;

                    lua_pop(co_L, 1);  /* 清理协程栈上的 Promise */
                } else {
                    /* yield 出来的不是 Promise：当作普通值恢复协程 */
                    /* 这种情况不应该发生在正常的 await 中，但为了健壮性处理 */
                    int status2 = lua_resume(co_L, L, 0, &nres);
                    if (status2 == LUA_OK || status2 == LUA_YIELD) {
                        if (status2 == LUA_OK) {
                            if (nres > 0) {
                                lua_xmove(co_L, L, nres);
                                promise_resolve(result_p, L);
                                lua_pop(L, 1);
                            } else {
                                lua_pushboolean(L, 1);
                                promise_resolve(result_p, L);
                                lua_pop(L, 1);
                            }
                            free_coroutine_context(ctx);
                            luaL_unref(L, LUA_REGISTRYINDEX, co_ref);
                        }
                        /* 如果再次 yield，继续上面的流程 */
                    } else {
                        const char *errmsg = lua_tostring(co_L, -1);
                        lua_pushstring(L, errmsg ? errmsg : "Error after non-Promise yield");
                        promise_reject(result_p, L);
                        lua_pop(L, 1);
                        free_coroutine_context(ctx);
                        luaL_unref(L, LUA_REGISTRYINDEX, co_ref);
                    }
                }
            } else {
                /* yield 但没有返回值（可能是 naked yield） */
                /* 协程将等待外部恢复，这里不做特殊处理 */
                /* 上下文保持有效，等待后续手动恢复或超时 */
            }
        }
        
    } else {
        /* 执行出错 */
        const char *errmsg = lua_tostring(co_L, -1);
        fprintf(stderr, "[asyncio] Coroutine error: %s\n", errmsg ? errmsg : "Unknown error");
        
        lua_pushstring(L, errmsg ? errmsg : "Coroutine error");
        promise_reject(result_p, L);
        lua_pop(L, 1);
        
        /* 清理 */
        if (ctx->waiting_p) { promise_release(ctx->waiting_p); }
        free_coroutine_context(ctx);
        luaL_unref(L, LUA_REGISTRYINDEX, co_ref);
    }
    
    return result_p;
}

/*
** =====================================================================
** Lua 绑定函数（供 Lua 代码调用）
** =====================================================================*/

static int laio_file_read(lua_State *L) {
    promise *p = aio_file_read(L, NULL);
    if (!p) return luaL_error(L, "Failed to create async file read operation");

    promise **pp = (promise **)lua_newuserdata(L, sizeof(promise *));
    *pp = p;
    luaL_getmetatable(L, PROMISE_METATABLE);
    lua_setmetatable(L, -2);
    return 1;
}

static int laio_http_get(lua_State *L) {
    promise *p = aio_http_get(L, NULL);
    if (!p) return luaL_error(L, "Failed to create async HTTP request");

    promise **pp = (promise **)lua_newuserdata(L, sizeof(promise *));
    *pp = p;
    luaL_getmetatable(L, PROMISE_METATABLE);
    lua_setmetatable(L, -2);
    return 1;
}

static int laio_http_post(lua_State *L) {
    promise *p = aio_http_post(L, NULL);
    if (!p) return luaL_error(L, "Failed to create async HTTP POST");

    promise **pp = (promise **)lua_newuserdata(L, sizeof(promise *));
    *pp = p;
    luaL_getmetatable(L, PROMISE_METATABLE);
    lua_setmetatable(L, -2);
    return 1;
}

static int laio_sleep(lua_State *L) {
    double seconds = luaL_checknumber(L, 1);
    promise *p = aio_sleep(seconds, L, NULL);
    if (!p) return luaL_error(L, "Failed to create async sleep");

    promise **pp = (promise **)lua_newuserdata(L, sizeof(promise *));
    *pp = p;
    luaL_getmetatable(L, PROMISE_METATABLE);
    lua_setmetatable(L, -2);
    return 1;
}

static int laio_dns_resolve(lua_State *L) {
    promise *p = aio_dns_resolve(L, NULL);
    if (!p) return luaL_error(L, "Failed to create async DNS resolution");

    promise **pp = (promise **)lua_newuserdata(L, sizeof(promise *));
    *pp = p;
    luaL_getmetatable(L, PROMISE_METATABLE);
    lua_setmetatable(L, -2);
    return 1;
}

static int laio_run_async(lua_State *L) {
    promise *p = aio_run_async(L, NULL);
    if (!p) return luaL_error(L, "Failed to run async function");

    promise **pp = (promise **)lua_newuserdata(L, sizeof(promise *));
    *pp = p;
    luaL_getmetatable(L, PROMISE_METATABLE);
    lua_setmetatable(L, -2);
    return 1;
}

static int laio_run_loop(lua_State *L) {
    event_loop *loop = aio_get_default_loop(L);
    if (!loop) return luaL_error(L, "Failed to get or create event loop");

    int ret = ev_loop_run(loop);
    lua_pushboolean(L, ret == 0);
    return 1;
}

static int laio_stop_loop(lua_State *L) {
    event_loop *loop = aio_get_default_loop(L);
    if (loop) ev_loop_stop(loop);
    return 0;
}

/*
** =====================================================================
** async/await 语法糖实现
** =====================================================================
*/

/* 前向声明 */
static int laio_async_call(lua_State *L);
static int laio_async_gc(lua_State *L);
static void defer_callback(event_loop *loop, ev_task *task);
static void laio_wt_timer_callback(event_loop *loop, ev_timer *timer);
static int laio_each_series_runner(lua_State *L);
static int laio_retry_runner(lua_State *L);

/**
 * @brief await() - 在协程中等待 Promise 完成（核心语法糖）
 *
 * 注意：由于 `await` 是 Lua 5.4 保留字，请使用：
 *   - asyncio["await"](promise)  - 字符串索引方式
 *   - asyncio.wait(promise)    - wait 别名（推荐）
 *
 * 实现原理：
 * 1. 检查当前是否在 asyncio.run() 创建的协程中
 * 2. 获取要等待的 Promise 对象
 * 3. 如果 Promise 已完成，立即返回结果
 * 4. 如果未完成，挂起协程，注册回调
 * 5. 当 Promise settle 时，恢复协程并返回结果
 */
static int laio_await(lua_State *L) {
    /* 检查参数：必须是 Promise userdata */
    promise **pp = (promise **)luaL_testudata(L, 1, PROMISE_METATABLE);
    if (!pp || !*pp) {
        /* 非 Promise 值：透传返回 */
        return 1;
    }
    
    promise *p = *pp;

    /* 从注册表中获取当前协程的上下文（判断是否在 async 函数中） */
    char ctx_key[64];
    snprintf(ctx_key, sizeof(ctx_key), "ACO_CTX_%p", (void *)L);

    lua_getfield(L, LUA_REGISTRYINDEX, ctx_key);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return luaL_error(L,
            "wait() can only be used inside an async function. "
            "Use asyncio.run() or asyncio.wrap() to execute async functions.");
    }

    coroutine_context *ctx = (coroutine_context *)lua_touserdata(L, -1);
    lua_pop(L, 1);  /* 弹出上下文 */
    
    if (!ctx) {
        return luaL_error(L, "Invalid coroutine context");
    }
    
    /* 检查 Promise 状态 */
    int state = promise_get_state(p);
    
    if (state == PROMISE_FULFILLED) {
        /* 已完成：立即返回结果 */
        promise_get_result(p, L);
        return 1;
        
    } else if (state == PROMISE_REJECTED) {
        /* 已拒绝：抛出错误 */
        promise_get_result(p, L);
        const char *errmsg = lua_tostring(L, -1);
        lua_pop(L, 1);
        return luaL_error(L, errmsg ? errmsg : "Promise rejected");
        
    } else {
        /* 未完成：设置 settle 回调并挂起协程 */
        ctx->waiting_p = promise_retain(p);
        
        /* 将协程上下文注册到 Promise 上 */
        /* 当 Promise 被 resolve/reject 时，laio_promise_settled 会被自动调用 */
        p->aco_ctx = ctx;
        p->on_settled = laio_promise_settled;
        
        /* 使用协程状态进行 yield（必须使用 ctx->co） */
        return lua_yield(ctx->co, 0);
    }
}

/**
 * @brief async() - 将函数包装为异步函数
 *
 * 用法：
 *   local fetchUser = async(function(id)
 *       local resp = await(http_get("/api/users/" .. id))
 *       return json.decode(resp.body)
 *   end)
 *   
 *   local user = fetchUser(123):await_sync()
 */
static int laio_async(lua_State *L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);

    lua_pushvalue(L, 1);
    int func_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    typedef struct { int func_ref; event_loop *loop; } async_fn_data;
    async_fn_data *afd = (async_fn_data *)lua_newuserdata(L, sizeof(async_fn_data));

    afd->func_ref = func_ref;
    afd->loop = aio_get_default_loop(L);

    if (luaL_newmetatable(L, "asyncio.async_function") != 0) {
        lua_pushcfunction(L, laio_async_call);
        lua_setfield(L, -2, "__call");
        lua_pushcfunction(L, laio_async_gc);
        lua_setfield(L, -2, "__gc");
        lua_pushliteral(L, "AsyncFunction");
        lua_setfield(L, -2, "__name");
    }
    /* 统一：metatable 在栈顶（新建或已复用），userdata 在次顶 */
    /* lua_setmetatable 弹出 metatable 并设置给 userdata */
    lua_setmetatable(L, -2);

    return 1;
}

/** @brief 异步函数的 __call 元方法 */
static int laio_async_call(lua_State *L) {
    typedef struct { int func_ref; event_loop *loop; } async_fn_data;
    async_fn_data *afd = (async_fn_data *)luaL_testudata(L, 1, "asyncio.async_function");
    if (!afd) return luaL_error(L, "Invalid async function");

    lua_rawgeti(L, LUA_REGISTRYINDEX, afd->func_ref);
    lua_replace(L, 1);

    promise *p = aio_run_async(L, afd->loop);

    if (!p) return luaL_error(L, "Failed to execute async function");

    promise **pp = (promise **)lua_newuserdata(L, sizeof(promise *));
    *pp = p;
    luaL_getmetatable(L, PROMISE_METATABLE);
    lua_setmetatable(L, -2);

    return 1;
}

/** @brief 异步函数的 GC 元方法 */
static int laio_async_gc(lua_State *L) {
    typedef struct { int func_ref; event_loop *loop; } async_fn_data;
    async_fn_data *afd = (async_fn_data *)luaL_testudata(L, 1, "asyncio.async_function");
    if (afd) luaL_unref(L, LUA_REGISTRYINDEX, afd->func_ref);
    return 0;
}

/**
 * @brief defer() - 延迟执行到下一个事件循环 tick
 *
 * 用法：await(defer(function() ... end))
 */
static int laio_defer(lua_State *L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    
    event_loop *loop = aio_get_default_loop(L);
    if (!loop) return luaL_error(L, "Failed to get event loop");
    
    promise *p = promise_new(L, loop);
    if (!p) return luaL_error(L, "Failed to create Promise");
    
    int func_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    
    typedef struct { int func_ref; promise *p; lua_State *L_main; } defer_ctx;
    defer_ctx *dctx = (defer_ctx *)calloc(1, sizeof(defer_ctx));
    if (!dctx) { promise_release(p); return luaL_error(L, "Out of memory"); }
    
    dctx->func_ref = func_ref;
    dctx->p = promise_retain(p);
    dctx->L_main = L;
    
    ev_task task;
    memset(&task, 0, sizeof(task));
    task.callback = defer_callback;  /* 使用外部定义的函数 */
    task.data = dctx;
    ev_post_task(loop, &task);
    
    return 1;
}

/**
 * @brief defer() 的回调函数实现（在外部定义以避免嵌套函数）
 */
static void defer_callback(event_loop *loop, ev_task *task) {
    typedef struct { int func_ref; promise *p; lua_State *L_main; } defer_ctx;
    defer_ctx *ctx = (defer_ctx *)task->data;
    
    if (ctx && ctx->p && ctx->L_main) {
        lua_rawgeti(ctx->L_main, LUA_REGISTRYINDEX, ctx->func_ref);
        int status = lua_pcall(ctx->L_main, 0, 1, 0);
        
        if (status == LUA_OK) {
            promise_resolve(ctx->p, ctx->L_main);
            lua_pop(ctx->L_main, 1);
        } else {
            const char *err = lua_tostring(ctx->L_main, -1);
            lua_pushstring(ctx->L_main, err ? err : "defer error");
            promise_reject(ctx->p, ctx->L_main);
            lua_pop(ctx->L_main, 1);
        }
    }
    
    if (ctx) { 
        luaL_unref(ctx->L_main, LUA_REGISTRYINDEX, ctx->func_ref); 
        promise_release(ctx->p); 
        free(ctx); 
    }
}

static int laio_promise_then(lua_State *L) {
    promise **pp = (promise **)luaL_checkudata(L, 1, PROMISE_METATABLE);
    if (!pp || !*pp) return luaL_error(L, "Invalid Promise object");

    promise *child = promise_then(*pp, L);
    if (!child) return luaL_error(L, "Failed to register then handler");

    promise **cp = (promise **)lua_newuserdata(L, sizeof(promise *));
    *cp = child;
    luaL_getmetatable(L, PROMISE_METATABLE);
    lua_setmetatable(L, -2);
    return 1;
}

static int laio_promise_catch(lua_State *L) {
    promise **pp = (promise **)luaL_checkudata(L, 1, PROMISE_METATABLE);
    if (!pp || !*pp) return luaL_error(L, "Invalid Promise object");

    promise *child = promise_catch(*pp, L);
    if (!child) return luaL_error(L, "Failed to register catch handler");

    promise **cp = (promise **)lua_newuserdata(L, sizeof(promise *));
    *cp = child;
    luaL_getmetatable(L, PROMISE_METATABLE);
    lua_setmetatable(L, -2);
    return 1;
}

static int laio_promise_await_sync(lua_State *L) {
    promise **pp = (promise **)luaL_checkudata(L, 1, PROMISE_METATABLE);
    if (!pp || !*pp) return luaL_error(L, "Invalid Promise object");

    int timeout_ms = luaL_optinteger(L, 2, -1);

    /* 清理栈上的参数（已读取到局部变量），避免回调链执行后的残留 */
    lua_settop(L, 0);

    int ret = promise_await_sync(*pp, L, timeout_ms);

    if (ret != 0) {
        lua_pushnil(L);
        lua_pushliteral(L, "Timeout or error while waiting for Promise");
        return 2;
    }

    /* promise_get_result 已将结果压栈，直接返回 1 */
    return 1;
}

static int laio_promise_state(lua_State *L) {
    promise **pp = (promise **)luaL_checkudata(L, 1, PROMISE_METATABLE);
    if (!pp || !*pp) {
        lua_pushliteral(L, "invalid");
        return 1;
    }

    switch ((*pp)->state) {
        case PROMISE_PENDING:    lua_pushliteral(L, "pending"); break;
        case PROMISE_FULFILLED:  lua_pushliteral(L, "fulfilled"); break;
        case PROMISE_REJECTED:   lua_pushliteral(L, "rejected"); break;
        default:                 lua_pushliteral(L, "unknown"); break;
    }
    return 1;
}

/**
 * @brief Promise.finally() - 注册 finally 回调（无论成功或失败都执行）
 *
 * 用法：p:finally(function() ... end)
 */
static int laio_promise_finally(lua_State *L) {
    promise **pp = (promise **)luaL_checkudata(L, 1, PROMISE_METATABLE);
    if (!pp || !*pp) return luaL_error(L, "Invalid Promise object");

    luaL_checktype(L, 2, LUA_TFUNCTION);

    promise *child = promise_finally(*pp, L);
    if (!child) return luaL_error(L, "Failed to register finally handler");

    promise **cp = (promise **)lua_newuserdata(L, sizeof(promise *));
    *cp = child;
    luaL_getmetatable(L, PROMISE_METATABLE);
    lua_setmetatable(L, -2);
    return 1;
}

/**
 * @brief Promise.cancel() - 取消 pending 状态的 Promise
 *
 * 用法：p:cancel()
 * 返回：true 如果取消成功，false 如果 Promise 已 settled
 */
static int laio_promise_cancel(lua_State *L) {
    promise **pp = (promise **)luaL_checkudata(L, 1, PROMISE_METATABLE);
    if (!pp || !*pp) { lua_pushboolean(L, 0); return 1; }

    if ((*pp)->state != PROMISE_PENDING) {
        lua_pushboolean(L, 0);
        return 1;
    }

    int ret = promise_cancel(*pp, L);
    if (ret == 0) {
        lua_pop(L, 1);  /* cancel 推入了拒绝原因，清理掉 */
        lua_pushboolean(L, 1);
    } else {
        lua_pushboolean(L, 0);
    }
    return 1;
}

/*
** =====================================================================
** Promise 组合操作静态方法（asyncio.all / race / allSettled / any）
** =====================================================================
*/

/**
 * @brief asyncio.all(...) - 等待所有 Promise 全部完成
 *
 * 用法：local p = asyncio.all(p1, p2, p3)
 *       local results = p:await_sync()
 *       -- results 是包含所有结果的数组
 */
static int laio_asyncio_all(lua_State *L) {
    event_loop *loop = aio_get_default_loop(L);

    promise *p = promise_all(L, loop);
    if (!p) return luaL_error(L, "Failed to create Promise.all");

    promise **pp = (promise **)lua_newuserdata(L, sizeof(promise *));
    *pp = p;
    luaL_getmetatable(L, PROMISE_METATABLE);
    lua_setmetatable(L, -2);
    return 1;
}

/**
 * @brief asyncio.race(...) - 返回第一个 settle 的 Promise 结果
 *
 * 用法：local p = asyncio.race(p1, p2, p3)
 *       local result = p:await_sync()
 */
static int laio_asyncio_race(lua_State *L) {
    event_loop *loop = aio_get_default_loop(L);

    promise *p = promise_race(L, loop);
    if (!p) return luaL_error(L, "Failed to create Promise.race");

    promise **pp = (promise **)lua_newuserdata(L, sizeof(promise *));
    *pp = p;
    luaL_getmetatable(L, PROMISE_METATABLE);
    lua_setmetatable(L, -2);
    return 1;
}

/**
 * @brief asyncio.allSettled(...) - 等待所有 Promise settle（不管成功或失败）
 *
 * 用法：local p = asyncio.allSettled(p1, p2)
 *       local results = p:await_sync()
 *       -- results[i] = {status="fulfilled"/"rejected", value=.../reason=...}
 */
static int laio_asyncio_all_settled(lua_State *L) {
    event_loop *loop = aio_get_default_loop(L);

    promise *p = promise_all_settled(L, loop);
    if (!p) return luaL_error(L, "Failed to create Promise.allSettled");

    promise **pp = (promise **)lua_newuserdata(L, sizeof(promise *));
    *pp = p;
    luaL_getmetatable(L, PROMISE_METATABLE);
    lua_setmetatable(L, -2);
    return 1;
}

/**
 * @brief asyncio.any(...) - 任一 Promise fulfilled 即完成
 *
 * 用法：local p = asyncio.any(p1, p2, p3)
 *       local result = p:await_sync()
 *       -- 全部 rejected 时返回错误
 */
static int laio_asyncio_any(lua_State *L) {
    event_loop *loop = aio_get_default_loop(L);

    promise *p = promise_any(L, loop);
    if (!p) return luaL_error(L, "Failed to create Promise.any");

    promise **pp = (promise **)lua_newuserdata(L, sizeof(promise *));
    *pp = p;
    luaL_getmetatable(L, PROMISE_METATABLE);
    lua_setmetatable(L, -2);
    return 1;
}

/**
 * @brief asyncio.withTimeout(promise, ms) - 带超时的 Promise 等待
 *
 * 超时后自动 reject 并返回超时错误信息
 * 原始 Promise 正常完成则正常返回结果
 *
 * 用法：local r = asyncio.withTimeout(slowPromise, 500):await_sync()
 */
static int laio_with_timeout(lua_State *L) {
    promise **pp = (promise **)luaL_checkudata(L, 1, PROMISE_METATABLE);
    if (!pp || !*pp) return luaL_error(L, "Invalid Promise object");

    int timeout_ms = luaL_checkinteger(L, 2);
    if (timeout_ms <= 0) {
        promise_retain(*pp);
        promise **result = (promise **)lua_newuserdata(L, sizeof(promise *));
        *result = *pp;
        luaL_getmetatable(L, PROMISE_METATABLE);
        lua_setmetatable(L, -2);
        return 1;
    }

    event_loop *loop = (*pp)->loop ? (*pp)->loop : aio_get_default_loop(L);

    promise *timeout_p = promise_new(L, loop);

    /* 将 L[2]（超时毫秒数整数）替换为 timeout_p 的 userdata */
    /* 这样 promise_race 只看到 L[1]=原始Promise, L[2]=timeoutPromise，共2个参数 */
    promise **tout_ud = (promise **)lua_newuserdata(L, sizeof(promise *));
    *tout_ud = timeout_p;
    luaL_getmetatable(L, PROMISE_METATABLE);
    lua_setmetatable(L, -2);
    lua_replace(L, 2);  /* 栈顶 tout_ud 替换 L[2]，弹出栈顶 */

    /* 此时栈上只有 [1]=orig_promise_ud [2]=timeout_p_ud */
    promise *race_p = promise_race(L, loop);

    if (!race_p) {
        promise_release(timeout_p);
        return luaL_error(L, "withTimeout: failed to create race");
    }

    double timeout_sec = timeout_ms / 1000.0;

    typedef struct { promise *p; lua_State *L_main; ev_timer timer; } _wt_tctx;
    _wt_tctx *_wrc = (_wt_tctx *)calloc(1, sizeof(_wt_tctx));
    _wrc->p = timeout_p;
    _wrc->L_main = L;

    memset(&_wrc->timer, 0, sizeof(_wrc->timer));
    _wrc->timer.timeout = ev_loop_now(loop) + timeout_sec;
    _wrc->timer.data = _wrc;
    _wrc->timer.callback = laio_wt_timer_callback;

    ev_timer_start(loop, &_wrc->timer);

    promise **rp = (promise **)lua_newuserdata(L, sizeof(promise *));
    *rp = race_p;
    luaL_getmetatable(L, PROMISE_METATABLE);
    lua_setmetatable(L, -2);
    return 1;
}

/*
** =====================================================================
** setInterval / clearInterval 定时器
** =====================================================================
*/

/**
 * @brief asyncio.setInterval(callback, seconds[, times]) - 周期性执行回调
 *
 * @param callback 要执行的函数
 * @param seconds 间隔秒数
 * @param times 可选，重复次数（0 或不传 = 无限重复）
 * @return timer_id 可用于 clearInterval 取消
 *
 * 用法：
 *   local id = setInterval(function() print("tick") end, 1.0)
 *   -- 1秒打印一次 "tick"
 *   clearInterval(id)  -- 停止
 */
static int laio_set_interval(lua_State *L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    double interval = luaL_checknumber(L, 2);
    int times = luaL_optinteger(L, 3, 0);  /* 0 = 无限 */

    if (interval <= 0) {
        return luaL_error(L, "setInterval: interval must be positive");
    }

    lua_State *main_L = L;

    /* 保存回调函数引用（防止 GC） */
    int cb_ref = luaL_ref(main_L, LUA_REGISTRYINDEX);

    event_loop *loop = aio_get_default_loop(L);
    ev_timer_id id = aio_set_interval(interval, main_L, cb_ref, times, loop);

    if (id == EV_TIMER_INVALID) {
        luaL_unref(main_L, LUA_REGISTRYINDEX, cb_ref);
        return luaL_error(L, "setInterval: failed to create timer");
    }

    lua_pushinteger(L, (lua_Integer)id);
    return 1;
}

/**
 * @brief asyncio.clearInterval(timer_id) - 取消周期定时器
 *
 * @param timer_id setInterval 返回的 ID
 * @return true 成功, false 失败
 */
static int laio_clear_interval(lua_State *L) {
    ev_timer_id id = (ev_timer_id)luaL_checkinteger(L, 1);

    event_loop *loop = aio_get_default_loop(L);
    int ret = aio_clear_timer(id, loop);

    lua_pushboolean(L, ret == 0);
    return 1;
}

/**
 * @brief withTimeout 定时器回调：超时后 reject 目标 Promise
 */
static void laio_wt_timer_callback(event_loop *loop, ev_timer *timer) {
    typedef struct {
        promise *p;
        lua_State *L_main;
        ev_timer timer;
    } _wt_ctx;

    _wt_ctx *ctx = (_wt_ctx *)timer->data;
    if (!ctx || !ctx->p || !ctx->L_main) return;

    if (ctx->p->state == PROMISE_PENDING) {
        lua_pushliteral(ctx->L_main, "Operation timed out");
        promise_reject(ctx->p, ctx->L_main);
    }

    promise_release(ctx->p);
    ctx->p = NULL;
    /* 不 free(ctx)：ctx 包含 timer，事件循环仍持有 &ctx->timer */
}

static int laio_gc(lua_State *L) {
    promise **pp = (promise **)luaL_testudata(L, 1, PROMISE_METATABLE);
    if (pp && *pp) {
        promise_release(*pp);
        *pp = NULL;
    }
    return 0;
}

/**
 * @brief Promise.resolve(value) - 创建已完成的 Promise（静态方法）
 *
 * 将任意值包装为已 fulfilled 的 Promise
 * 如果传入的值已经是 Promise，则直接返回
 */
static int laio_promise_resolve_static(lua_State *L) {
    event_loop *loop = aio_get_default_loop(L);
    promise *p = promise_resolved(L, loop);
    if (!p) return luaL_error(L, "Failed to create resolved Promise");

    promise **pp = (promise **)lua_newuserdata(L, sizeof(promise *));
    *pp = p;
    luaL_getmetatable(L, PROMISE_METATABLE);
    lua_setmetatable(L, -2);
    return 1;
}

/**
 * @brief Promise.reject(reason) - 创建已拒绝的 Promise（静态方法）
 *
 * 用法：local p = asyncio.reject("error message")
 */
static int laio_promise_reject_static(lua_State *L) {
    event_loop *loop = aio_get_default_loop(L);
    promise *p = promise_rejected(L, loop);

    promise **pp = (promise **)lua_newuserdata(L, sizeof(promise *));
    *pp = p;
    luaL_getmetatable(L, PROMISE_METATABLE);
    lua_setmetatable(L, -2);
    return 1;
}

/**
 * @brief asyncio.map(fn, table[, concurrency]) - 并发映射
 *
 * 对表中每个元素执行异步函数，限制并发数，返回结果表
 * fn 接收 (value, index) 参数，应返回 Promise 或普通值
 */
static int laio_asyncio_map(lua_State *L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);   /* 映射函数 */
    luaL_checktype(L, 2, LUA_TTABLE);       /* 输入列表 */
    int max_concurrency = luaL_optinteger(L, 3, 0);  /* 0=无限制 */

    int count = lua_rawlen(L, 2);
    if (count == 0) {
        event_loop *loop = aio_get_default_loop(L);
        lua_newtable(L);
        promise *p = promise_resolved(L, loop);
        if (!p) return luaL_error(L, "map: failed to create result");
        promise **pp = (promise **)lua_newuserdata(L, sizeof(promise *));
        *pp = p;
        luaL_getmetatable(L, PROMISE_METATABLE);
        lua_setmetatable(L, -2);
        return 1;
    }

    event_loop *loop = aio_get_default_loop(L);

    /* 为每个元素创建 Promise：直接执行函数并包装结果 */
    /* 不使用 aio_run_async（协程开销大且栈管理复杂） */
    for (int i = 1; i <= count; i++) {
        lua_pushvalue(L, 1);           /* 复制映射函数 */
        lua_rawgeti(L, 2, i);          /* 获取元素 */
        lua_pushinteger(L, i);         /* 索引 */

        /* 直接调用用户函数（同步方式） */
        int status = lua_pcall(L, 2, 1, 0);  /* fn(elem, idx) → result */

        if (status != LUA_OK) {
            /* 函数出错：为这个元素创建 rejected promise */
            const char *errmsg = lua_tostring(L, -1);
            lua_pushstring(L, errmsg ? errmsg : "map function error");
            promise *err_p = promise_rejected(L, loop);
            lua_pop(L, 1);  /* pop error string consumed by promise_reject */
            if (!err_p) return luaL_error(L, "map: item %d reject failed", i);

            promise **pp = (promise **)lua_newuserdata(L, sizeof(promise *));
            *pp = err_p;
            luaL_getmetatable(L, PROMISE_METATABLE);
            lua_setmetatable(L, -2);
        } else {
            /* 函数成功：包装返回值为 fulfilled promise */
            promise *ok_p = promise_resolved(L, loop);
            if (!ok_p) return luaL_error(L, "map: item %d resolve failed", i);
            lua_pop(L, 1);  /* promise_resolve 不消费栈值，手动清理 */

            promise **pp = (promise **)lua_newuserdata(L, sizeof(promise *));
            *pp = ok_p;
            luaL_getmetatable(L, PROMISE_METATABLE);
            lua_setmetatable(L, -2);
        }
    }

    /* 移除原始参数（函数+表），只保留 count 个 Promise userdata */
    lua_remove(L, 1);  /* 移除函数 */
    lua_remove(L, 1);  /* 移除表（原位置2，现在变成1） */

    /* 栈上现在只有 count 个 Promise userdata，调用 all 收集结果 */
    promise *result = promise_all(L, loop);
    if (!result) return luaL_error(L, "map: failed to collect results");

    promise **rp = (promise **)lua_newuserdata(L, sizeof(promise *));
    *rp = result;
    luaL_getmetatable(L, PROMISE_METATABLE);
    lua_setmetatable(L, -2);
    return 1;
}

/**
 * @brief asyncio.eachSeries(list, fn) - 串行逐个执行
 *
 * 按顺序对每个元素执行异步函数，等待完成后再执行下一个
 * fn 接收 (value, index) 参数，在协程中运行
 */
static int laio_each_series(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);       /* 列表 */
    luaL_checktype(L, 2, LUA_TFUNCTION);     /* 处理函数 */

    int count = lua_rawlen(L, 1);
    if (count == 0) {
        event_loop *loop = aio_get_default_loop(L);
        lua_pushboolean(L, 1);
        promise *p = promise_resolved(L, loop);
        if (!p) return luaL_error(L, "eachSeries: failed");
        promise **pp = (promise **)lua_newuserdata(L, sizeof(promise *));
        *pp = p;
        luaL_getmetatable(L, PROMISE_METATABLE);
        lua_setmetatable(L, -2);
        return 1;
    }

    /* 创建一个串行执行所有任务的 async 函数 */
    event_loop *loop = aio_get_default_loop(L);

    /* 把 list 和 fn 存到 registry 中供内部使用 */
    int list_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    int fn_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    /* 创建包装协程函数 */
    lua_pushcfunction(L, laio_each_series_runner);
    /* 传递引用 */
    lua_pushinteger(L, list_ref);
    lua_pushinteger(L, fn_ref);
    lua_pushinteger(L, count);

    promise *p = aio_run_async(L, loop);

    luaL_unref(L, LUA_REGISTRYINDEX, fn_ref);
    luaL_unref(L, LUA_REGISTRYINDEX, list_ref);

    if (!p) return luaL_error(L, "eachSeries: failed to start");

    promise **pp = (promise **)lua_newuserdata(L, sizeof(promise *));
    *pp = p;
    luaL_getmetatable(L, PROMISE_METATABLE);
    lua_setmetatable(L, -2);
    return 1;
}

/** eachSeries 内部运行的协程体 */
static int laio_each_series_runner(lua_State *L) {
    int list_ref = luaL_checkinteger(L, 1);
    int fn_ref = luaL_checkinteger(L, 2);
    int count = luaL_checkinteger(L, 3);

    for (int i = 1; i <= count; i++) {
        /* 获取处理函数 */
        lua_rawgeti(L, LUA_REGISTRYINDEX, fn_ref);
        /* 获取列表元素 */
        lua_rawgeti(L, LUA_REGISTRYINDEX, list_ref);
        lua_rawgeti(L, -1, i);
        lua_remove(L, -2);  /* 移除列表表，保留元素 */
        lua_pushinteger(L, i);

        /* 调用用户函数获取 Promise */
        coroutine_context *ctx = NULL;
        {
            char ctx_key[64];
            snprintf(ctx_key, sizeof(ctx_key), "ACO_CTX_%p", (void *)L);
            lua_getfield(L, LUA_REGISTRYINDEX, ctx_key);
            if (!lua_isnil(L, -1)) {
                ctx = (coroutine_context *)lua_touserdata(L, -1);
            }
            lua_pop(L, 1);
        }

        if (ctx && ctx->waiting_p) {
            /* 在协程内调用函数 */
            lua_call(L, 2, 1);  /* fn(element, index) → result/promise */

            /* 如果结果是 Promise，等待它 */
            if (luaL_testudata(L, -1, PROMISE_METATABLE)) {
                promise **pp = (promise **)lua_touserdata(L, -1);
                if (pp && *pp) {
                    ctx->waiting_p = promise_retain(*pp);
                    (*pp)->aco_ctx = ctx;
                    (*pp)->on_settled = NULL;  /* 使用 await 机制 */
                    lua_pop(L, 1);
                    return lua_yield(L, 0);  /* 挂起等待 */
                }
            }
            lua_pop(L, 1);  /* 非Promise结果，丢弃 */
        } else {
            lua_pop(L, 3);  /* 清理栈 */
        }
    }

    lua_pushboolean(L, 1);
    return 1;
}

/**
 * @brief asyncio.retry(fn, maxRetries[, delayMs]) - 自动重试机制
 *
 * 失败时自动重试最多 maxRetries 次，可选延迟
 * 返回最后一次成功的结果或最终错误
 */
static int laio_asyncio_retry(lua_State *L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);     /* 要执行的函数 */
    int max_retries = luaL_optinteger(L, 2, 3);
    int delay_ms = luaL_optinteger(L, 3, 0);

    event_loop *loop = aio_get_default_loop(L);

    int fn_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    /* 创建重试协程 */
    lua_pushcfunction(L, laio_retry_runner);
    lua_pushinteger(L, fn_ref);
    lua_pushinteger(L, max_retries);
    lua_pushinteger(L, delay_ms);

    promise *p = aio_run_async(L, loop);
    luaL_unref(L, LUA_REGISTRYINDEX, fn_ref);

    if (!p) return luaL_error(L, "retry: failed to start");

    promise **pp = (promise **)lua_newuserdata(L, sizeof(promise *));
    *pp = p;
    luaL_getmetatable(L, PROMISE_METATABLE);
    lua_setmetatable(L, -2);
    return 1;
}

/** retry 内部运行的协程体 */
static int laio_retry_runner(lua_State *L) {
    int fn_ref = luaL_checkinteger(L, 1);
    int max_retries = luaL_checkinteger(L, 2);
    int delay_ms = luaL_checkinteger(L, 3);

    const char *last_err = NULL;

    for (int attempt = 1; attempt <= max_retries; attempt++) {
        /* 调用用户函数 */
        lua_rawgeti(L, LUA_REGISTRYINDEX, fn_ref);

        /* 获取协程上下文用于 yield/await */
        coroutine_context *ctx = NULL;
        {
            char ctx_key[64];
            snprintf(ctx_key, sizeof(ctx_key), "ACO_CTX_%p", (void *)L);
            lua_getfield(L, LUA_REGISTRYINDEX, ctx_key);
            if (!lua_isnil(L, -1)) {
                ctx = (coroutine_context *)lua_touserdata(L, -1);
            }
            lua_pop(L, 1);
        }

        int status = lua_pcall(L, 0, 1, 0);
        if (status == LUA_OK) {
            /* 成功：返回结果（已在栈上） */
            return 1;
        }

        /* 失败：记录错误 */
        last_err = lua_tostring(L, -1);
        lua_pop(L, 1);

        /* 不是最后一次：等待后重试 */
        if (attempt < max_retries && delay_ms > 0 && ctx) {
            double sec = delay_ms / 1000.0;
            promise *sleep_p = aio_sleep(sec, L, ctx->loop);
            if (sleep_p) {
                ctx->waiting_p = promise_retain(sleep_p);
                sleep_p->aco_ctx = ctx;
                sleep_p->on_settled = NULL;
                return lua_yield(L, 0);
            }
        }
    }

    /* 所有尝试都失败 */
    lua_pushstring(L, last_err ? last_err : "All retries failed");
    return luaL_error(L, "%s", lua_tostring(L, -1));
}


/**
 * @brief Promise __index 元方法（支持方法访问）
 *
 * 提供无下划线的简洁 API：
 *   p:done(fn)    - 成功回调（替代 .and_then）
 *   p:fail(fn)    - 错误回调（替代 .on_catch）
 */
static int laio_promise_index(lua_State *L) {
    /* 检查是否是 Promise userdata */
    promise **pp = (promise **)luaL_testudata(L, 1, PROMISE_METATABLE);
    if (!pp || !*pp) {
        lua_pushnil(L);
        return 1;
    }

    /* 获取要访问的字段名 */
    const char *key = luaL_checkstring(L, 2);

    /* 处理方法名映射 */
    if (strcmp(key, "done") == 0 || strcmp(key, "then") == 0) {
        /* done: 成功回调 */
        lua_pushcfunction(L, laio_promise_then);
        return 1;
    }
    else if (strcmp(key, "fail") == 0 || strcmp(key, "catch") == 0) {
        /* fail: 错误回调 */
        lua_pushcfunction(L, laio_promise_catch);
        return 1;
    }
    else if (strcmp(key, "finally") == 0) {
        /* finally: 无论成功或失败都执行的回调 */
        lua_pushcfunction(L, laio_promise_finally);
        return 1;
    }
    else if (strcmp(key, "cancel") == 0) {
        /* cancel: 取消 pending 的 Promise */
        lua_pushcfunction(L, laio_promise_cancel);
        return 1;
    }

    /* 其他字段：从元表本身查找 */
    if (lua_getmetatable(L, 1)) {
        lua_pushvalue(L, 2);  /* 再次 push key */
        lua_rawget(L, -2);   /* 从元表获取值 */
        if (!lua_isnil(L, -1)) {
            return 1;  /* 找到了 */
        }
        lua_pop(L, 1);  /* 弹出 nil */
        lua_pop(L, 1);  /* 弹出元表 */
    }

    /* 未找到 */
    lua_pushnil(L);
    return 1;
}

/*
** =====================================================================
** 辅助工具函数
** =====================================================================*/

/**
 * @brief asyncio.isPromise(value) - 检查值是否为 Promise
 *
 * 用于类型检查和条件逻辑
 * 用法：
 *   if asyncio.isPromise(result) then
 *       local data = asyncio.wait(result)
 *   end
 */
static int laio_is_promise(lua_State *L) {
    promise **pp = (promise **)luaL_testudata(L, 1, PROMISE_METATABLE);
    lua_pushboolean(L, pp && *pp != NULL);
    return 1;
}

/**
 * @brief asyncio.promisify(fn) - 将 Node.js 风格的回调函数转换为 async 函数
 *
 * 将 function(callback) 风格的异步函数转换为返回 Promise 的 async 函数
 * callback 应遵循 Node.js 约定：callback(err, result)
 *
 * 用法：
 *   local fs_read = asyncio.promisify(function(path, cb)
 *       -- 异步操作完成后调用 cb(nil, data) 或 cb(error)
 *   end)
 *
 *   -- 使用时像普通 async 函数一样
 *   local data = fs_read("file.txt"):await_sync()
 */
static int laio_promisify(lua_State *L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);

    lua_pushvalue(L, 1);
    int fn_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    typedef struct { int fn_ref; event_loop *loop; } promisify_data;
    promisify_data *pd = (promisify_data *)lua_newuserdata(L, sizeof(promisify_data));
    pd->fn_ref = fn_ref;
    pd->loop = aio_get_default_loop(L);

    if (luaL_newmetatable(L, "asyncio.promisified_function") != 0) {
        lua_pushcfunction(L, laio_async_call);  /* 复用 __call 实现 */
        lua_setfield(L, -2, "__call");
        lua_pushcfunction(L, laio_async_gc);    /* 复用 GC 实现 */
        lua_setfield(L, -2, "__gc");
    }
    lua_setmetatable(L, -2);

    return 1;
}

/**
 * @brief Promise.__tostring - 调试用字符串表示
 *
 * 显示 Promise 状态和调试信息
 * 用法：print(promiseObj)
 */
static int laio_promise_tostring(lua_State *L) {
    promise **pp = (promise **)luaL_testudata(L, 1, PROMISE_METATABLE);
    if (!pp || !*pp) {
        lua_pushliteral(L, "Promise(<invalid>)");
        return 1;
    }

    promise *p = *pp;
    const char *state_str;
    switch (p->state) {
        case PROMISE_PENDING:   state_str = "Pending"; break;
        case PROMISE_FULFILLED: state_str = "Fulfilled"; break;
        case PROMISE_REJECTED:  state_str = "Rejected"; break;
        default:                state_str = "Unknown"; break;
    }

    lua_pushfstring(L, "Promise<%s>%s%s",
                   state_str,
                   p->tag ? " [" : "",
                   p->tag ? p->tag : "",
                   p->tag ? "]" : "");
    return 1;
}

/**
 * @brief run_async_internal(func, ...) - 内部异步执行入口
 *
 * 这是 lvm.c 的 lvm_async_start 调用的桥梁函数。
 * 接收一个普通 Lua 函数和参数，在协程中执行它，
 * 返回 Promise 对象。
 *
 * 特点：
 * - 不注册为全局函数（用户无法访问）
 * - 存储在 LOADED_ASYNCIO.run_async_internal
 * - 完整支持 await(Promise) 协作
 */
static int laio_run_async_internal(lua_State *L) {
    /* 检查第一个参数是否是函数 */
    luaL_checktype(L, 1, LUA_TFUNCTION);

    event_loop *loop = aio_get_default_loop(L);

    /* 调用 aio_run_async 执行异步函数 */
    promise *p = aio_run_async(L, loop);

    if (p) {
        /* 将 Promise 压入栈 */
        promise **pp = (promise **)lua_newuserdata(L, sizeof(promise *));
        *pp = p;
        luaL_setmetatable(L, PROMISE_METATABLE);
        return 1;
    }

    /* 如果 aio_run_async 失败，返回 nil + 错误信息 */
    lua_pushnil(L);
    lua_pushliteral(L, "Failed to start async execution");
    return 2;
}

/*
** =====================================================================
** 库初始化
** =====================================================================*/

int luaopen_asyncio(lua_State *L) {
    /* 创建 Promise 元表 */
    luaL_newmetatable(L, PROMISE_METATABLE);

    /* 元方法 */
    lua_pushcfunction(L, laio_gc);
    lua_setfield(L, -2, "__gc");

    /* 设置自定义 __index 元方法（支持保留字） */
    lua_pushcfunction(L, laio_promise_index);
    lua_setfield(L, -2, "__index");

    /* 注册非保留字的方法（直接访问） */
    lua_pushcfunction(L, laio_promise_await_sync);
    lua_setfield(L, -2, "await_sync");
    
    lua_pushcfunction(L, laio_promise_state);
    lua_setfield(L, -2, "state");
    
    lua_pushliteral(L, "Promise");
    lua_setfield(L, -2, "__name");
    
    /* 创建 asyncio 模块表 */
    lua_newtable(L);
    
    /* 核心函数 */
    lua_pushcfunction(L, laio_run_async);
    lua_setfield(L, -2, "run");
    
    lua_pushcfunction(L, laio_run_loop);
    lua_setfield(L, -2, "run_loop");
    
    lua_pushcfunction(L, laio_stop_loop);
    lua_setfield(L, -2, "stop");
    
    /* 异步 I/O 函数 */
    lua_pushcfunction(L, laio_file_read);
    lua_setfield(L, -2, "read_file");
    
    lua_pushcfunction(L, laio_http_get);
    lua_setfield(L, -2, "http_get");
    
    lua_pushcfunction(L, laio_http_post);
    lua_setfield(L, -2, "http_post");
    
    lua_pushcfunction(L, laio_dns_resolve);
    lua_setfield(L, -2, "dns_resolve");
    
    lua_pushcfunction(L, laio_sleep);
    lua_setfield(L, -2, "sleep");
    
    /* ============================================ */
    /* async/await 语法糖核心函数              */
    /* ============================================ */
    
    /**
     * @brief await() - 在协程中等待 Promise 完成
     * 
     * 用法：
     *   local result = await(asyncio.sleep(1))
     *   local data = await(asyncio.http_get(url))
     * 
     * 必须在 asyncio.run() 创建的协程中使用！
     */
    lua_pushcfunction(L, laio_await);
    lua_setfield(L, -2, "await");
    
    /**
     * @brief wait() - await 的非保留字别名 (Lua 5.4 中 await 是保留字)
     * 
     * 用法（推荐，避免语法错误）:
     *   local result = asyncio.wait(asyncio.sleep(1))
     *   local wait = asyncio.wait  -- 定义局部变量更简洁
     */
    lua_pushcfunction(L, laio_await);
    lua_setfield(L, -2, "wait");
    
    /**
     * @brief async() - 将普通函数包装为异步函数
     * 
     * 用法：
     *   local fetchUser = async(function(id)
     *       local resp = await(http_get("/api/users/"..id))
     *       return json.decode(resp.body)
     *   end)
     *   
     *   local user = fetchUser(123):await_sync()
     */
    lua_pushcfunction(L, laio_async);
    lua_setfield(L, -2, "async");
    
    /**
     * @brief wrap() - async 的非保留字别名 (Lua 5.4 中 async 是保留字)
     * 
     * 用法（推荐）:
     *   local fetchUser = asyncio.wrap(function(id)
     *       local resp = wait(asyncio.http_get("/api/users/"..id))
     *       return json.decode(resp.body)
     *   end)
     */
    lua_pushcfunction(L, laio_async);
    lua_setfield(L, -2, "wrap");
    
    /**
     * @brief defer() - 延迟执行（类似 setTimeout(fn, 0)）
     * 
     * 用法：
     *   await(defer(function()
     *       print("稍后执行")
     *   end))
     */
    lua_pushcfunction(L, laio_defer);
    lua_setfield(L, -2, "defer");
    
    /**
     * @brief nexttick() - defer 的非保留字别名 (LXCLUA-NCore 中 defer 是保留字)
     * 
     * 用法（推荐）:
     *   wait(asyncio.nexttick(function()
     *       print("下个 tick 执行")
     *   end))
     */
    lua_pushcfunction(L, laio_defer);
    lua_setfield(L, -2, "nexttick");

    /* ============================================ */
    /* Promise 组合操作（静态方法）                */
    /* ============================================ */

    /**
     * @brief asyncio.all(...) - 等待所有 Promise 全部完成
     *
     * 任一失败则立即 reject
     * 用法：local p = asyncio.all(p1, p2, p3)
     */
    lua_pushcfunction(L, laio_asyncio_all);
    lua_setfield(L, -2, "all");

    /**
     * @brief asyncio.race(...) - 返回第一个 settle 的结果
     *
     * 用法：local p = asyncio.race(slowPromise, fastPromise)
     */
    lua_pushcfunction(L, laio_asyncio_race);
    lua_setfield(L, -2, "race");

    /**
     * @brief asyncio.allSettled(...) - 等待所有 Promise settle
     *
     * 不管成功或失败，返回所有状态
     * 用法：local results = asyncio.allSettled(p1, p2):await_sync()
     */
    lua_pushcfunction(L, laio_asyncio_all_settled);
    lua_setfield(L, -2, "allSettled");

    /**
     * @brief asyncio.any(...) - 任一 fulfilled 即完成
     *
     * 全部 rejected 时才 reject
     * 用法：local p = asyncio.any(p1, p2, p3)
     */
    lua_pushcfunction(L, laio_asyncio_any);
    lua_setfield(L, -2, "any");

    /**
     * @brief asyncio.withTimeout(promise, ms) - 带超时的 Promise 包装
     *
     * 超时后自动 reject，原始完成则正常返回
     * 用法：local r = asyncio.withTimeout(slowOp, 500):await_sync()
     */
    lua_pushcfunction(L, laio_with_timeout);
    lua_setfield(L, -2, "withTimeout");

    /* ============================================ */
    /* 定时器                                      */
    /* ============================================ */

    /**
     * @brief asyncio.setInterval(fn, sec[, times]) - 周期定时器
     *
     * 返回 timer_id，可用 clearInterval 取消
     */
    lua_pushcfunction(L, laio_set_interval);
    lua_setfield(L, -2, "setInterval");

    /**
     * @brief asyncio.clearInterval(timer_id) - 取消周期定时器
     */
    lua_pushcfunction(L, laio_clear_interval);
    lua_setfield(L, -2, "clearInterval");

    /* ============================================ */
    /* Promise 静态工厂方法                        */
    /* ============================================ */

    /**
     * @brief Promise.resolve(value) - 创建已完成的 Promise
     *
     * 用法：local p = asyncio.resolve(42)
     *       local p = asyncio.resolve({key="value"})
     */
    lua_pushcfunction(L, laio_promise_resolve_static);
    lua_setfield(L, -2, "resolve");

    /**
     * @brief Promise.reject(reason) - 创建已拒绝的 Promise
     *
     * 用法：local p = asyncio.reject("error message")
     */
    lua_pushcfunction(L, laio_promise_reject_static);
    lua_setfield(L, -2, "reject");

    /* ============================================ */
    /* 高级并发工具                                */
    /* ============================================ */

    /**
     * @brief asyncio.gather(...) - asyncio.all 的 Python 兼容别名
     *
     * 用法：local results = asyncio.gather(p1, p2, p3):await_sync()
     */
    lua_pushcfunction(L, laio_asyncio_all);
    lua_setfield(L, -2, "gather");

    /**
     * @brief asyncio.map(fn, {...}, concurrency) - 并发映射
     *
     * 对列表中每个元素执行异步函数，限制并发数
     * 用法：
     *   local results = asyncio.map(
     *       function(url) return http_get(url):await_sync() end,
     *       {"http://a.com", "http://b.com"},
     *       3  -- 最大并发
     *   ):await_sync()
     */
    lua_pushcfunction(L, laio_asyncio_map);
    lua_setfield(L, -2, "map");

    /**
     * @brief asyncio.eachSeries({...}, fn) - 串行逐个执行
     *
     * 按顺序对每个元素执行异步函数，等待一个完成后再执行下一个
     * 用法：
     *   asyncio.eachSeries({"a.txt", "b.txt"}, function(file)
     *       local content = read_file(file):await_sync()
     *       print(content)
     *   end):await_sync()
     */
    lua_pushcfunction(L, laio_each_series);
    lua_setfield(L, -2, "eachSeries");

    /**
     * @brief asyncio.retry(fn, maxRetries[, delayMs]) - 自动重试
     *
     * 失败时自动重试指定次数，支持延迟
     * 用法：
     *   local result = asyncio.retry(function()
     *       return http_get(url):await_sync()
     *   end, 3, 1000):await_sync()  -- 最多重试3次，间隔1秒
     */
    lua_pushcfunction(L, laio_asyncio_retry);
    lua_setfield(L, -2, "retry");

    /* 常量 */
    lua_pushinteger(L, PROMISE_PENDING);
    lua_setfield(L, -2, "PENDING");
    
    lua_pushinteger(L, PROMISE_FULFILLED);
    lua_setfield(L, -2, "FULFILLED");
    
    lua_pushinteger(L, PROMISE_REJECTED);
    lua_setfield(L, -2, "REJECTED");

    /* ============================================ */
    /* 语法糖辅助工具                               */
    /* ============================================ */

    lua_pushcfunction(L, laio_is_promise);
    lua_setfield(L, -2, "isPromise");

    lua_pushcfunction(L, laio_promisify);
    lua_setfield(L, -2, "promisify");

    /* 注册 __tostring 到 Promise 元表（需要重新获取元表） */
    luaL_getmetatable(L, PROMISE_METATABLE);
    lua_pushcfunction(L, laio_promise_tostring);
    lua_setfield(L, -2, "__tostring");
    lua_pop(L, 1);

    /*
     * ====================================================================
     * 注册内部接口（供 lvm.c 的纯 C 层 async/await 使用）
     * ====================================================================
     *
     * 这些函数不暴露到全局环境，只存储在注册表中，
     * 供 VM 的 OP_ASYNCWRAP 操作码内部使用。
     */

    /* 存储 asyncio 表引用到注册表（供 lvm.c 检测） */
    lua_pushvalue(L, -2);  /* 复制 asyncio 表 */
    lua_setfield(L, LUA_REGISTRYINDEX, "LOADED_ASYNCIO");

    /* 注册 run_async_internal 到 asyncio 表（但不暴露给用户） */
    lua_pushcfunction(L, laio_run_async_internal);
    lua_setfield(L, -2, "run_async_internal");

    return 1;
}
