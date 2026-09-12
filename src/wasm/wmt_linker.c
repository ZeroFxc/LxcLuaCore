/**
 * @file wmt_linker.c
 * @brief wasmtime Lua 绑定 —— Linker / Caller / HostCallback：host import 定义与模块实例化
 *
 * 模块化拆分自 lwasmtime.c（wasmtime v48.0.1 C API）。
 * 共享类型与函数声明见 lwasmtime.h。
 */
#include "lwasmtime.h"

/* ============================================================
 * newLinker(engine) → linker  — Lua API
 * ============================================================ */

/* linker userdata 类型 */

static int wmt_linker_gc(lua_State *L) {
    wmt_Linker *wl = (wmt_Linker*)luaL_checkudata(L, 1, WMT_LINKER);
    if (wl->linker) {
        wasmtime_linker_delete(wl->linker);
        wl->linker = NULL;
    }
    return 0;
}

/**
 * @brief wasmtime.newLinker(engine) → linker
 * 创建一个新的 linker，用于组合模块和定义 host imports。
 */
int l_new_linker(lua_State *L) {
    wmt_Engine *we = (wmt_Engine*)luaL_checkudata(L, 1, WMT_ENGINE);

    wmt_Linker *wl = (wmt_Linker*)lua_newuserdata(L, sizeof(wmt_Linker));
    wl->linker = wasmtime_linker_new(we->engine);

    if (!wl->linker) {
        return luaL_error(L, "Failed to create wasmtime linker");
    }

    luaL_getmetatable(L, WMT_LINKER);
    lua_setmetatable(L, -2);
    return 1;
}

/* ============================================================
 * Host Callback 环境 — 通用 import 定义 (linker:defineFunc)
 * ============================================================ */


/* ---- Caller 方法: readMem ---- */

/**
 * @brief caller:readMem(ptr, len) → string
 * 从 WASM 实例的线性内存中读取 len 字节。
 * @param ptr WASM 内存偏移量
 * @param len 要读取的字节数
 * @return Lua string 包含读取的数据
 */
static int l_caller_readMem(lua_State *L) {
    wmt_Caller *wc = (wmt_Caller*)luaL_checkudata(L, 1, WMT_CALLER);
    lua_Integer ptr = luaL_checkinteger(L, 2);
    lua_Integer len = luaL_checkinteger(L, 3);

    if (ptr < 0 || len < 0)
        return luaL_error(L, "readMem: invalid ptr/len");

    /* 每次调用都重新查找 memory export（避免跨调用悬空指针） */
    wasmtime_extern_t item;
    if (!wasmtime_caller_export_get(wc->caller, "memory", 6, &item) ||
        item.kind != WASMTIME_EXTERN_MEMORY) {
        return luaL_error(L, "readMem: no exported memory");
    }

    uint8_t *mem_data = wasmtime_memory_data(wc->ctx, &item.of.memory);
    size_t   mem_size = wasmtime_memory_data_size(wc->ctx, &item.of.memory);

    if ((size_t)(ptr + len) > mem_size)
        return luaL_error(L, "readMem: out of bounds (ptr=%I + len=%I, mem=%I)", ptr, len, (lua_Integer)mem_size);

    lua_pushlstring(L, (const char*)(mem_data + ptr), (size_t)len);
    return 1;
}

/**
 * @brief caller:writeMem(ptr, data) → bytes_written
 * 向 WASM 实例的线性内存写入数据。
 * @param ptr WASM 内存偏移量
 * @param data 要写入的字符串
 * @return 写入的字节数
 */
static int l_caller_writeMem(lua_State *L) {
    wmt_Caller *wc = (wmt_Caller*)luaL_checkudata(L, 1, WMT_CALLER);
    lua_Integer ptr = luaL_checkinteger(L, 2);
    size_t data_len;
    const char *data_str = luaL_checklstring(L, 3, &data_len);

    if (ptr < 0)
        return luaL_error(L, "writeMem: invalid ptr");

    /* 每次调用都重新查找 memory export */
    wasmtime_extern_t item;
    if (!wasmtime_caller_export_get(wc->caller, "memory", 6, &item) ||
        item.kind != WASMTIME_EXTERN_MEMORY) {
        return luaL_error(L, "writeMem: no exported memory");
    }

    uint8_t *mem_data = wasmtime_memory_data(wc->ctx, &item.of.memory);
    size_t   mem_size = wasmtime_memory_data_size(wc->ctx, &item.of.memory);

    if ((size_t)ptr + data_len > mem_size)
        return luaL_error(L, "writeMem: out of bounds");

    memcpy(mem_data + ptr, data_str, data_len);
    lua_pushinteger(L, (lua_Integer)data_len);
    return 1;
}

static const struct luaL_Reg caller_methods[] = {
    {"readMem",  l_caller_readMem},
    {"writeMem", l_caller_writeMem},
    {NULL, NULL}
};

/* ---- 通用 host 回调包装器 ---- */

/**
 * @brief 通用 host 回调入口 — 由 wasmtime 调用, 转发到 Lua
 *
 * 从 env 中获取 Lua callback, 压入 caller + args, pcall, 转换结果。
 * 如果 Lua 回调返回 (nil, error_string), 转换为 wasm_trap。
 */
static wasm_trap_t* wmt_host_callback_invoke(
    void *env,
    wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t nargs,
    wasmtime_val_t *results, size_t nresults)
{
    wmt_HostCallback *hc = (wmt_HostCallback*)env;
    lua_State *L = hc->L;

    /* 压入回调函数 */
    lua_rawgeti(L, LUA_REGISTRYINDEX, hc->cb_ref);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return wasmtime_trap_new("host callback function lost", 28);
    }

    /* 创建临时 caller userdata */
    wmt_Caller *wc = (wmt_Caller*)lua_newuserdata(L, sizeof(wmt_Caller));
    wc->caller = caller;
    wc->ctx = wasmtime_caller_context(caller);
    luaL_getmetatable(L, WMT_CALLER);
    lua_setmetatable(L, -2);

    /* 压入参数: caller 作为第一参数 */
    int nargs_lua = 1;  /* caller userdata */

    /* 转换 C args → Lua values */
    for (size_t i = 0; i < nargs; i++) {
        wasmtime_val_to_lua(L, &args[i]);
        nargs_lua++;
    }

    /* pcall: Lua C API — 返回 LUA_OK(0) 表示成功, 非0 表示失败 */
    int pc_ok = lua_pcall(L, nargs_lua, (int)nresults, 0);

    if (pc_ok != LUA_OK) {
        /* Lua 回调抛出 error → wasm trap */
        const char *err = lua_tostring(L, -1);
        wasm_trap_t *trap = wasmtime_trap_new(err ? err : "host callback error", 
                                               err ? strlen(err) : 21);
        lua_pop(L, 1);
        return trap;
    }

    /* 转换返回值: Lua values → C wasmtime_val_t
     * 用负索引从栈顶取值（pcall 后栈可能残留 func_ud 在下层）
     * 少返回值 → 填充默认值, 多返回值 → 忽略多余 */
    int nret = lua_gettop(L);
    for (size_t i = 0; i < nresults; i++) {
        int lua_idx = -(int)nresults + (int)i; /* 负索引, -1 是最后一个返回值 */
        if (-lua_idx <= nret) {
            /* 根据期望类型正确转换 */
            lua_to_wasmtime_val_typed(L, lua_idx, &results[i],
                (i < hc->nresults && hc->ret_kinds) ? hc->ret_kinds[i] : WASM_I32);
        } else {
            /* 填充默认值 */
            results[i].kind = WASMTIME_I32;
            results[i].of.i32 = 0;
        }
    }

    lua_pop(L, (int)nresults); /* 只弹出返回值, 保留底层栈 */
    return NULL;  /* 无 trap */
}

/**
 * @brief host callback finalizer — 清理 registry 引用
 */
static void wmt_host_callback_finalizer(void *data) {
    wmt_HostCallback *hc = (wmt_HostCallback*)data;
    if (hc->cb_ref != LUA_NOREF) {
        luaL_unref(hc->L, LUA_REGISTRYINDEX, hc->cb_ref);
    }
    if (hc->ret_kinds) {
        free(hc->ret_kinds);
    }
    free(hc);
}

/* ---- linker:defineFunc ---- */

/**
 * @brief linker:defineFunc(module, name, params, results, callback)
 *
 * 注册一个 Lua 回调作为 WASM host import。
 * 当 WASM 调用此 import 时, callback 被调用:
 *   callback(caller, arg1, arg2, ...) → ret1, ret2, ...
 *
 * @param module   WASM import 模块名 (string)
 * @param name     WASM import 函数名 (string)
 * @param params   参数类型字符串, 如 "i32,i32,i32" 或 "" (无参数)
 * @param results  返回值类型字符串, 如 "i32" 或 "" (无返回值)
 * @param callback Lua 回调函数
 * @return linker (self, 链式调用)
 */
static int l_linker_define_func(lua_State *L) {
    wmt_Linker *wl = (wmt_Linker*)luaL_checkudata(L, 1, WMT_LINKER);

    const char *module   = luaL_checkstring(L, 2);
    const char *name     = luaL_checkstring(L, 3);
    const char *params   = luaL_optstring(L, 4, "");
    const char *results  = luaL_optstring(L, 5, "");
    luaL_checktype(L, 6, LUA_TFUNCTION);

    /* 解析类型 */
    wasm_valtype_vec_t param_vec, result_vec;
    if (wmt_parse_type_vec(params, &param_vec) != 0)
        return luaL_error(L, "defineFunc: invalid params type string: %s", params);
    if (wmt_parse_type_vec(results, &result_vec) != 0) {
        wasm_valtype_vec_delete(&param_vec);
        return luaL_error(L, "defineFunc: invalid results type string: %s", results);
    }

    wasm_functype_t *func_type = wasm_functype_new(&param_vec, &result_vec);
    /* 注意: wasm_functype_new 取得 param_vec/result_vec 的所有权,
     * 不要再 delete 它们, 否则会 double-free。 */

    if (!func_type)
        return luaL_error(L, "defineFunc: failed to create functype");

    /* 创建回调环境 */
    wmt_HostCallback *hc = (wmt_HostCallback*)malloc(sizeof(wmt_HostCallback));
    if (!hc) {
        wasm_functype_delete(func_type);
        return luaL_error(L, "defineFunc: out of memory");
    }
    hc->L = L;
    lua_pushvalue(L, 6);  /* 复制 callback 到栈顶 */
    hc->cb_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    hc->nresults = result_vec.size;
    hc->ret_kinds = NULL;
    /* 提取返回值类型 kind, 供回调返回值转换时使用 */
    if (result_vec.size > 0) {
        hc->ret_kinds = (wasm_valkind_t*)malloc(result_vec.size * sizeof(wasm_valkind_t));
        if (hc->ret_kinds) {
            for (size_t i = 0; i < result_vec.size; i++) {
                hc->ret_kinds[i] = wasm_valtype_kind(result_vec.data[i]);
            }
        }
    }

    /* 注册到 wasmtime linker */
    wasmtime_error_t *err = wasmtime_linker_define_func(
        wl->linker,
        module, strlen(module),
        name, strlen(name),
        func_type,
        wmt_host_callback_invoke,
        hc,
        wmt_host_callback_finalizer);

    wasm_functype_delete(func_type);

    if (err) {
        wasm_name_t msg;
        wasmtime_error_message(err, &msg);
        char buf[512];
        int n = snprintf(buf, sizeof(buf), "defineFunc: wasmtime error: %.*s",
                         (int)msg.size, msg.data);
        wasm_byte_vec_delete(&msg);
        wasmtime_error_delete(err);
        return luaL_error(L, "%s", buf);
    }

    /* 返回 self (链式调用) */
    lua_pushvalue(L, 1);
    return 1;
}

/* ---- linker:instantiate ---- */

/**
 * @brief linker:instantiate(store, module) → instance
 *
 * 使用已注册的 host imports 实例化模块。
 * 搭配 defineFunc 使用, 替代 newInstance 用于需要 import 的模块。
 *
 * @param store  wasmtime store
 * @param module wasmtime module
 * @return instance (wmt_Instance userdata)
 */
static int l_linker_instantiate(lua_State *L) {
    wmt_Linker *wl   = (wmt_Linker*)luaL_checkudata(L, 1, WMT_LINKER);
    wmt_Store  *ws   = (wmt_Store*)luaL_checkudata(L, 2, WMT_STORE);
    wmt_Module *wm   = (wmt_Module*)luaL_checkudata(L, 3, WMT_MODULE);

    wasmtime_context_t *ctx = wasmtime_store_context(ws->store);

    wmt_Instance *wi = (wmt_Instance*)lua_newuserdata(L, sizeof(wmt_Instance));
    memset(wi, 0, sizeof(wmt_Instance));

    wasm_trap_t *trap = NULL;
    wasmtime_error_t *err = wasmtime_linker_instantiate(
        wl->linker, ctx, wm->module, &wi->instance, &trap);

    if (err || trap) {
        const char *msg_str = "linker instantiation failed";
        int msg_len = 26;
        wasm_name_t trap_msg = {0};
        if (err) {
            wasmtime_error_message(err, &trap_msg);
            msg_str = trap_msg.data;
            msg_len = (int)trap_msg.size;
        } else if (trap) {
            wasm_trap_message(trap, &trap_msg);
            msg_str = trap_msg.data;
            msg_len = (int)trap_msg.size;
        }
        lua_pushnil(L);
        lua_pushlstring(L, msg_str, msg_len);
        if (err) {
            wasm_byte_vec_delete(&trap_msg);
            wasmtime_error_delete(err);
        }
        if (trap) {
            wasm_byte_vec_delete(&trap_msg);
            wasm_trap_delete(trap);
        }
        return 2;
    }

    wi->store = ws->store;
    lua_pushvalue(L, 2);  /* store 在参数 2 */
    wi->store_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    luaL_getmetatable(L, WMT_INSTANCE);
    lua_setmetatable(L, -2);
    return 1;
}

/* ============================================================
 * 模块注册
 * ============================================================ */


/* ============================================================
 * linker:defineWasi() — 注册 WASI imports
 * ============================================================ */

/**
 * @brief linker:defineWasi() → linker | nil, err
 * 注册所有 WASI 导入（需先在 store 上应用 wasi 配置）。
 * 调用顺序：store:setWasi(wasi) 之后，linker:instantiate 之前。
 * @return linker（self，链式调用）；失败时 nil, errmsg
 */
static int l_linker_define_wasi(lua_State *L) {
    wmt_Linker *wl = (wmt_Linker*)luaL_checkudata(L, 1, WMT_LINKER);

    wasmtime_error_t *err = wasmtime_linker_define_wasi(wl->linker);
    if (err) {
        wasm_name_t msg;
        wasmtime_error_message(err, &msg);
        lua_pushnil(L);
        lua_pushlstring(L, msg.data, msg.size);
        wasm_byte_vec_delete(&msg);
        wasmtime_error_delete(err);
        return 2;
    }

    lua_pushvalue(L, 1);
    return 1;
}

/* ============================================================
 * Linker 深化：allowShadowing / define / defineInstance
 * ============================================================ */

/**
 * @brief linker:allowShadowing(bool) → linker
 * 设置是否允许后定义的同名 import 覆盖先前定义（默认 false）。
 * @return linker（self，链式调用）
 */
static int l_linker_allow_shadowing(lua_State *L) {
    wmt_Linker *wl = (wmt_Linker*)luaL_checkudata(L, 1, WMT_LINKER);
    bool allow = lua_toboolean(L, 2);
    wasmtime_linker_allow_shadowing(wl->linker, allow);
    lua_pushvalue(L, 1);
    return 1;
}

/** 从 userdata 提取 wasmtime_extern_t（func/global/memory/table），返回 0 成功 */
static int wmt_userdata_to_extern(lua_State *L, int idx, wasmtime_extern_t *out) {
    memset(out, 0, sizeof(*out));
    if (luaL_testudata(L, idx, WMT_FUNC)) {
        wmt_Function *wf = (wmt_Function*)lua_touserdata(L, idx);
        out->kind = WASMTIME_EXTERN_FUNC;
        out->of.func = wf->func;
        return 0;
    }
    if (luaL_testudata(L, idx, WMT_GLOBAL)) {
        wmt_Global *wg = (wmt_Global*)lua_touserdata(L, idx);
        out->kind = WASMTIME_EXTERN_GLOBAL;
        out->of.global = wg->global;
        return 0;
    }
    if (luaL_testudata(L, idx, WMT_MEMORY)) {
        wmt_Memory *wm = (wmt_Memory*)lua_touserdata(L, idx);
        out->kind = WASMTIME_EXTERN_MEMORY;
        out->of.memory = wm->memory;
        return 0;
    }
    if (luaL_testudata(L, idx, WMT_TABLE)) {
        wmt_Table *wt = (wmt_Table*)lua_touserdata(L, idx);
        out->kind = WASMTIME_EXTERN_TABLE;
        out->of.table = wt->table;
        return 0;
    }
    return -1;
}

/**
 * @brief linker:define(store, module, name, item) → linker | nil, err
 * 在 linker 中定义任意 item（func/global/memory/table）。
 * @param store  项所属的 store
 * @param module 模块名（被实例化的模块 import 的命名空间）
 * @param name   import 字段名
 * @param item   wasmtime.function/global/memory/table userdata
 * @return linker（self）；失败时 nil, errmsg
 */
static int l_linker_define(lua_State *L) {
    wmt_Linker *wl = (wmt_Linker*)luaL_checkudata(L, 1, WMT_LINKER);
    wmt_Store  *ws = (wmt_Store*)luaL_checkudata(L, 2, WMT_STORE);
    size_t mod_len, name_len;
    const char *mod = luaL_checklstring(L, 3, &mod_len);
    const char *name = luaL_checklstring(L, 4, &name_len);

    wasmtime_extern_t item;
    if (wmt_userdata_to_extern(L, 5, &item) != 0) {
        return luaL_error(L, "linker:define: 第 5 参数需为 function/global/memory/table userdata");
    }

    wasmtime_context_t *ctx = wasmtime_store_context(ws->store);
    wasmtime_error_t *err = wasmtime_linker_define(
        wl->linker, ctx, mod, mod_len, name, name_len, &item);
    if (err) {
        wasm_name_t msg;
        wasmtime_error_message(err, &msg);
        lua_pushnil(L);
        lua_pushlstring(L, msg.data, msg.size);
        wasm_byte_vec_delete(&msg);
        wasmtime_error_delete(err);
        return 2;
    }

    lua_pushvalue(L, 1);
    return 1;
}

/**
 * @brief linker:defineInstance(store, name, instance) → linker | nil, err
 * 将实例的全部导出以 name 为模块名注册进 linker（字段名=导出名）。
 * 常用于模块间依赖注入（导出即 import）。
 * @param store   实例所属 store
 * @param name    注册用的模块名
 * @param instance wasmtime.instance userdata
 * @return linker（self）；失败时 nil, errmsg
 */
static int l_linker_define_instance(lua_State *L) {
    wmt_Linker *wl = (wmt_Linker*)luaL_checkudata(L, 1, WMT_LINKER);
    wmt_Store  *ws = (wmt_Store*)luaL_checkudata(L, 2, WMT_STORE);
    size_t name_len;
    const char *name = luaL_checklstring(L, 3, &name_len);
    wmt_Instance *wi = (wmt_Instance*)luaL_checkudata(L, 4, WMT_INSTANCE);

    wasmtime_context_t *ctx = wasmtime_store_context(ws->store);
    wasmtime_error_t *err = wasmtime_linker_define_instance(
        wl->linker, ctx, name, name_len, &wi->instance);
    if (err) {
        wasm_name_t msg;
        wasmtime_error_message(err, &msg);
        lua_pushnil(L);
        lua_pushlstring(L, msg.data, msg.size);
        wasm_byte_vec_delete(&msg);
        wasmtime_error_delete(err);
        return 2;
    }

    lua_pushvalue(L, 1);
    return 1;
}

/* ============================================================
 * 方法表
 * ============================================================ */

const struct luaL_Reg wmt_linker_methods[] = {
    {"defineFunc",     l_linker_define_func},
    {"defineWasi",     l_linker_define_wasi},
    {"define",         l_linker_define},
    {"defineInstance", l_linker_define_instance},
    {"allowShadowing", l_linker_allow_shadowing},
    {"instantiate",    l_linker_instantiate},
    {"__gc", wmt_linker_gc},
    {NULL, NULL}
};
const struct luaL_Reg wmt_caller_methods[] = {
    {"readMem",  l_caller_readMem},
    {"writeMem", l_caller_writeMem},
    {NULL, NULL}
};
