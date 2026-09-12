/**
 * @file wmt_async.c
 * @brief wasmtime 绑定 - Async 支持（func:callAsync + future）
 *
 * 对应 wasmtime v48 C API 的异步执行机制（wasmtime_func_call_async /
 * wasmtime_call_future_poll）。wasmtime 的 async 本质是协作式栈切换：
 * wasm 代码可在燃料耗尽 / epoch 到期 / async host function 让出时暂停，
 * 宿主通过 poll 推进其执行。
 *
 * Lua 侧暴露：
 *   func:callAsync(...)      -> future | nil, trap, msg
 *   future:poll()            -> bool（true = 已完成）
 *   future:done()            -> bool（是否已完成，不推进执行）
 *   future:result()          -> 结果值（已完成时）
 *   future:wait([timeoutMs])-> 结果值（同步 poll 循环直至完成；可选超时）
 *   future:delete()          -> 显式释放（__gc 自动调用）
 *
 * 约束（来自 async.h）：
 *   - 同一 store 同时只能有一个存活 future（须先 delete 再发起下一次调用）。
 *   - args/results 缓冲区须存活到 future 被删除。
 *   - 同步 await 对"等待外部事件的 async host function"会阻塞；此类场景
 *     应结合事件循环分步 poll（future:poll()）。
 */

#include "lwasmtime.h"
#include "lpromise.h"
#include "laio.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ============================================================
 * future 释放
 * ============================================================ */

static void wmt_future_release(lua_State *L, wmt_Future *fu) {
    if (fu->future) {
        wasmtime_call_future_delete(fu->future);
        fu->future = NULL;
    }
    if (fu->results) {
        free(fu->results);
        fu->results = NULL;
    }
    if (fu->trap) {
        wasm_trap_delete(fu->trap);
        fu->trap = NULL;
    }
    if (fu->error) {
        wasmtime_error_delete(fu->error);
        fu->error = NULL;
    }
    if (fu->store_ref != LUA_NOREF && fu->store_ref != 0) {
        luaL_unref(L, LUA_REGISTRYINDEX, fu->store_ref);
        fu->store_ref = LUA_NOREF;
    }
}

/* ============================================================
 * func:callAsync(...)
 * ============================================================ */

/**
 * @brief func:callAsync(...) → future | nil, trap, msg | nil, nil, msg
 *
 * 异步调用 wasm 函数，返回 wasmtime.future 对象。
 * 与 func:call 参数规则一致（Lua 值 → wasm 参数）。
 * 注意：同一 store 同时只能有一个存活 future。
 */
int l_func_call_async(lua_State *L) {
    wmt_Function *wf = (wmt_Function*)luaL_checkudata(L, 1, WMT_FUNC);
    int nargs = lua_gettop(L) - 1;

    wasmtime_context_t *ctx = wasmtime_store_context(wf->store);

    wasm_functype_t *ftype = wasmtime_func_type(ctx, &wf->func);
    if (!ftype) {
        return luaL_error(L, "Failed to get function type");
    }

    const wasm_valtype_vec_t *params = wasm_functype_params(ftype);
    const wasm_valtype_vec_t *results = wasm_functype_results(ftype);
    size_t expected_nargs = params->size;
    size_t nresults = results->size;
    int actual_nresults = (int)nresults;

    wasmtime_val_t *args = NULL;
    if (expected_nargs > 0) {
        args = (wasmtime_val_t*)malloc(expected_nargs * sizeof(wasmtime_val_t));
        if (!args) {
            wasm_functype_delete(ftype);
            return luaL_error(L, "out of memory");
        }
        for (size_t i = 0; i < expected_nargs; i++) {
            if ((int)i < nargs) {
                lua_to_wasmtime_val(L, i + 2, &args[i]);
            } else {
                args[i].kind = WASMTIME_I32;
                args[i].of.i32 = 0;
            }
        }
    }

    /* future userdata 持有 results 存储（wasmtime 写入） */
    wmt_Future *fu = (wmt_Future*)lua_newuserdata(L, sizeof(wmt_Future));
    memset(fu, 0, sizeof(wmt_Future));
    fu->store_ref = LUA_NOREF;

    if (nresults == 0) nresults = 1; /* 至少一个槽 */
    fu->results = (wasmtime_val_t*)calloc(nresults, sizeof(wasmtime_val_t));
    if (!fu->results) {
        free(args);
        wasm_functype_delete(ftype);
        return luaL_error(L, "out of memory");
    }
    fu->nresults = (size_t)actual_nresults;

    /* 调用 async：返回 NULL 表示立即完成 */
    fu->future = wasmtime_func_call_async(
        ctx, &wf->func,
        args, expected_nargs,
        fu->results, nresults,
        &fu->trap, &fu->error);

    wasm_functype_delete(ftype);
    free(args);

    if (fu->future == NULL) {
        /* 同步完成：trap/error 已写入 fu */
        fu->done = 1;
    } else {
        /* 先 poll 一次推进执行（避免未让出也返回 future 的情况） */
        if (wasmtime_call_future_poll(fu->future)) {
            wasmtime_call_future_delete(fu->future);
            fu->future = NULL;
            fu->done = 1;
        }
    }

    /* 若调用即刻失败（同步完成且有 trap/error），转为 nil, trap, msg */
    if (fu->done && (fu->trap || fu->error)) {
        /* 释放 future 占用的存储，取出 trap/error 返回 */
        if (fu->trap) {
            lua_pushnil(L);
            wmt_Trap *wt = (wmt_Trap*)lua_newuserdata(L, sizeof(wmt_Trap));
            wt->trap = fu->trap;
            fu->trap = NULL;
            luaL_getmetatable(L, WMT_TRAP);
            lua_setmetatable(L, -2);

            wasm_name_t msg = {0};
            wasm_trap_message(wt->trap, &msg);
            size_t msz = msg.size;
            while (msz > 0 && msg.data[msz - 1] == '\0') msz--;
            lua_pushlstring(L, msg.data, msz);
            wasm_byte_vec_delete(&msg);
        } else {
            wasm_name_t msg = {0};
            wasmtime_error_message(fu->error, &msg);
            lua_pushnil(L);
            lua_pushnil(L);
            lua_pushlstring(L, msg.data, msg.size);
            wasm_byte_vec_delete(&msg);
        }
        wmt_future_release(L, fu);
        lua_pop(L, 1); /* 弹出 future userdata */
        return 3;
    }

    /* 保护 store 生命周期：从 func 的 store_ref 复制一份引用 */
    fu->store_ref = LUA_NOREF;
    if (wf->store_ref != LUA_NOREF) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, wf->store_ref);
        fu->store_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }

    luaL_getmetatable(L, WMT_FUTURE);
    lua_setmetatable(L, -2);
    return 1;
}

/* ============================================================
 * lpromise 集成：func:callAsyncP(...) → Promise
 * 配合项目原生 async/await（await(promise) → coroutine.yield）
 * ============================================================ */

/**
 * @brief func:callAsyncP(...) → Promise（asyncio.promise）
 *
 * 与 func:callAsync 语义相同，但返回 lpromise Promise 对象，
 * 可在 async function 中直接 `await(func:callAsyncP(...))`。
 * 实现：同步 poll future 至完成，用结果 resolve / 错误 reject。
 */
int l_func_call_async_promise(lua_State *L) {
    wmt_Function *wf = (wmt_Function*)luaL_checkudata(L, 1, WMT_FUNC);
    int nargs = lua_gettop(L) - 1;

    wasmtime_context_t *ctx = wasmtime_store_context(wf->store);

    wasm_functype_t *ftype = wasmtime_func_type(ctx, &wf->func);
    if (!ftype) return luaL_error(L, "Failed to get function type");

    const wasm_valtype_vec_t *params = wasm_functype_params(ftype);
    const wasm_valtype_vec_t *results = wasm_functype_results(ftype);
    size_t expected_nargs = params->size;
    size_t nresults = results->size;
    int actual_nresults = (int)nresults;

    wasmtime_val_t *args = NULL;
    if (expected_nargs > 0) {
        args = (wasmtime_val_t*)malloc(expected_nargs * sizeof(wasmtime_val_t));
        if (!args) {
            wasm_functype_delete(ftype);
            return luaL_error(L, "out of memory");
        }
        for (size_t i = 0; i < expected_nargs; i++) {
            if ((int)i < nargs) {
                lua_to_wasmtime_val(L, i + 2, &args[i]);
            } else {
                args[i].kind = WASMTIME_I32;
                args[i].of.i32 = 0;
            }
        }
    }

    if (nresults == 0) nresults = 1;
    wasmtime_val_t *res_buf = (wasmtime_val_t*)calloc(nresults, sizeof(wasmtime_val_t));
    if (!res_buf) {
        free(args);
        wasm_functype_delete(ftype);
        return luaL_error(L, "out of memory");
    }

    wasm_trap_t *trap = NULL;
    wasmtime_error_t *err = NULL;
    wasmtime_call_future_t *future = wasmtime_func_call_async(
        ctx, &wf->func, args, expected_nargs, res_buf, nresults, &trap, &err);

    wasm_functype_delete(ftype);
    free(args);

    /* 同步 poll 至完成 */
    while (future) {
        if (wasmtime_call_future_poll(future)) {
            wasmtime_call_future_delete(future);
            future = NULL;
            break;
        }
        /* future 让出（燃料/epoch/async host fn）：同步等待其推进。
         * 若依赖外部事件会忙等——此类场景应改用 callAsync + future:poll。 */
    }

    event_loop *loop = aio_get_default_loop(L);
    promise *p = promise_new(L, loop);
    if (!p) {
        if (res_buf) free(res_buf);
        if (trap) wasm_trap_delete(trap);
        if (err) wasmtime_error_delete(err);
        return luaL_error(L, "out of memory");
    }

    if (err || trap) {
        if (err) {
            wasm_name_t msg;
            wasmtime_error_message(err, &msg);
            lua_pushlstring(L, msg.data, msg.size);
            wasm_byte_vec_delete(&msg);
            wasmtime_error_delete(err);
        } else {
            wasm_name_t msg;
            wasm_trap_message(trap, &msg);
            size_t msz = msg.size;
            while (msz > 0 && msg.data[msz - 1] == '\0') msz--;
            lua_pushlstring(L, msg.data, msz);
            wasm_byte_vec_delete(&msg);
            wasm_trap_delete(trap);
        }
        promise_reject(p, L);
    } else {
        int retc = 0;
        for (int i = 0; i < actual_nresults; i++) {
            wasmtime_val_to_lua(L, &res_buf[i]);
            retc++;
        }
        if (retc == 0) lua_pushnil(L);
        promise_resolve(p, L);
    }
    free(res_buf);

    /* 包装为 Promise userdata */
    promise **pp = (promise**)lua_newuserdata(L, sizeof(promise*));
    *pp = p;
    luaL_getmetatable(L, PROMISE_METATABLE);
    lua_setmetatable(L, -2);
    return 1;
}

/* ============================================================
 * future 方法
 * ============================================================ */

/** @brief future:poll() → bool  推进执行，true=已完成 */
static int l_future_poll(lua_State *L) {
    wmt_Future *fu = (wmt_Future*)luaL_checkudata(L, 1, WMT_FUTURE);
    if (!fu->done && fu->future) {
        if (wasmtime_call_future_poll(fu->future)) {
            wasmtime_call_future_delete(fu->future);
            fu->future = NULL;
            fu->done = 1;
        }
    }
    lua_pushboolean(L, fu->done);
    return 1;
}

/** @brief future:done() → bool  是否已完成（不推进执行） */
static int l_future_done(lua_State *L) {
    wmt_Future *fu = (wmt_Future*)luaL_checkudata(L, 1, WMT_FUTURE);
    lua_pushboolean(L, fu->done);
    return 1;
}

/** 内部：已完成时把结果/错误压栈，返回返回数 */
static int wmt_future_push_result(lua_State *L, wmt_Future *fu) {
    if (fu->trap) {
        lua_pushnil(L);
        wmt_Trap *wt = (wmt_Trap*)lua_newuserdata(L, sizeof(wmt_Trap));
        wt->trap = fu->trap;
        fu->trap = NULL; /* 所有权转移给 trap userdata */
        luaL_getmetatable(L, WMT_TRAP);
        lua_setmetatable(L, -2);

        wasm_name_t msg = {0};
        wasm_trap_message(wt->trap, &msg);
        size_t msz = msg.size;
        while (msz > 0 && msg.data[msz - 1] == '\0') msz--;
        lua_pushlstring(L, msg.data, msz);
        wasm_byte_vec_delete(&msg);
        return 3;
    }
    if (fu->error) {
        wasm_name_t msg = {0};
        wasmtime_error_message(fu->error, &msg);
        lua_pushnil(L);
        lua_pushnil(L);
        lua_pushlstring(L, msg.data, msg.size);
        wasm_byte_vec_delete(&msg);
        return 3;
    }
    int retc = 0;
    for (size_t i = 0; i < fu->nresults; i++) {
        wasmtime_val_to_lua(L, &fu->results[i]);
        retc++;
    }
    if (retc == 0) return 0;
    return retc;
}

/** @brief future:result() → 结果值（须先 poll 完成）；未完成时返回 nil */
static int l_future_result(lua_State *L) {
    wmt_Future *fu = (wmt_Future*)luaL_checkudata(L, 1, WMT_FUTURE);
    if (!fu->done) {
        lua_pushnil(L);
        return 1;
    }
    return wmt_future_push_result(L, fu);
}

/** @brief future:wait([timeoutMs]) → 结果值
 * 同步 poll 循环直至完成（默认无限等待）。
 * timeoutMs > 0 时超时返回 nil, nil, "timeout"。
 */
static int l_future_wait(lua_State *L) {
    wmt_Future *fu = (wmt_Future*)luaL_checkudata(L, 1, WMT_FUTURE);
    lua_Integer timeout_ms = -1;
    if (!lua_isnoneornil(L, 2)) {
        timeout_ms = luaL_checkinteger(L, 2);
    }

    clock_t start = clock();
    while (!fu->done) {
        if (fu->future) {
            if (wasmtime_call_future_poll(fu->future)) {
                wasmtime_call_future_delete(fu->future);
                fu->future = NULL;
                fu->done = 1;
                break;
            }
        } else {
            fu->done = 1;
            break;
        }
        if (timeout_ms >= 0) {
            double elapsed_ms = (double)(clock() - start) * 1000.0 / CLOCKS_PER_SEC;
            if (elapsed_ms > (double)timeout_ms) {
                lua_pushnil(L);
                lua_pushnil(L);
                lua_pushliteral(L, "async future await timeout");
                return 3;
            }
        }
    }

    return wmt_future_push_result(L, fu);
}

/** @brief future:delete()  显式释放（__gc 自动调用） */
static int l_future_delete(lua_State *L) {
    wmt_Future *fu = (wmt_Future*)luaL_checkudata(L, 1, WMT_FUTURE);
    wmt_future_release(L, fu);
    return 0;
}

static int wmt_future_gc(lua_State *L) {
    wmt_Future *fu = (wmt_Future*)luaL_checkudata(L, 1, WMT_FUTURE);
    wmt_future_release(L, fu);
    return 0;
}

/* ============================================================
 * 方法表
 * ============================================================ */

const struct luaL_Reg wmt_future_methods[] = {
    {"poll",   l_future_poll},
    {"done",   l_future_done},
    {"result", l_future_result},
    {"wait",   l_future_wait},
    {"delete", l_future_delete},
    {"__gc",   wmt_future_gc},
    {NULL, NULL}
};
