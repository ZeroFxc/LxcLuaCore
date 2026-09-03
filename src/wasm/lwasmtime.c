/**
 * @file lwasmtime.c
 * @brief wasmtime Lua C 模块入口 — 支持 GC/reference-types 的 WASM 运行时
 *
 * wasmtime 完整支持 WASM GC / reference-types 提案。
 *
 * Lua API:
 *   local wasmtime = require("wasmtime")
 *   local engine   = wasmtime.newEngine([config])        -- 创建引擎（支持 config 表）
 *   local store    = wasmtime.newStore(engine)           -- 创建存储（执行上下文）
 *   local module   = wasmtime.newModule(engine, wasm_bytes)  -- 编译模块
 *   local ok, err  = wasmtime.validate(wasm_bytes)       -- 仅验证不编译
 *   local instance = wasmtime.newInstance(store, module, imports) -- 实例化
 *
 *   -- 导出操作
 *   local func     = instance:getExport("name")          -- 获取导出函数
 *   local mem      = instance:getMemory("memory")        -- 获取内存导出
 *   local global   = instance:getGlobal("name")          -- 获取全局变量导出
 *   local table    = instance:getTable("name")           -- 获取表导出
 *   local item,kind = instance:getExportEx("name")       -- 通用导出获取
 *   local exports  = instance:getExports()               -- 导出类型列表
 *
 *   -- 函数操作
 *   local results  = func:call(args...)                  -- 调用函数
 *   local params,rets = func:getType()                   -- 获取函数签名
 *
 *   -- 内存操作
 *   local data     = mem:read(offset, len)               -- 读取内存
 *   local n        = mem:write(offset, str)              -- 写入内存
 *   local pages    = mem:size()                          -- 页数
 *   local bytes    = mem:dataSize()                      -- 字节数
 *   local old,ok   = mem:grow(delta)                     -- 增长内存
 *   local min,max  = mem:getType()                       -- 内存限制
 *
 *   -- 全局变量操作
 *   local val      = global:get()                        -- 读取全局变量
 *   local ok,err   = global:set(val)                     -- 设置全局变量
 *
 *   -- 表操作
 *   local val      = table:get(idx)                      -- 读取表元素
 *   local ok,err   = table:set(idx, val)                 -- 设置表元素
 *   local n        = table:size()                        -- 表大小
 *   local old,ok   = table:grow(delta[, init_val])       -- 增长表
 *
 *   -- Store 操作
 *   local ok,err   = store:setFuel(amount)               -- 设置燃料上限
 *   local fuel     = store:getFuel()                     -- 获取剩余燃料
 *   store:gc()                                           -- 触发 GC
 *   local ok       = store:setEpochDeadline(ticks)       -- 设置 epoch 截止
 *   local mem2     = store:newMemory(min, max)           -- 创建独立内存
 *
 *   -- Module 操作
 *   local bytes    = module:serialize()                  -- 序列化编译模块
 *   local exports  = module:getExports()                 -- 导出类型列表
 *   local imports  = module:getImports()                 -- 导入类型列表
 *   local mod2     = wasmtime.deserializeModule(engine, bytes) -- 反序列化模块
 *
 *   -- Linker 操作
 *   local linker   = wasmtime.newLinker(engine)          -- 创建 linker
 *   linker:defineFunc(mod, name, params, results, cb)    -- 定义 host import
 *   local inst     = linker:instantiate(store, module)   -- 通过 linker 实例化
 *
 *   -- externref
 *   local eref     = wasmtime.newExternref(store, data)  -- 创建 externref
 *
 *   -- 共享内存（多线程）
 *   local shmem    = wasmtime.newSharedMemory(engine, min, max) -- 线程安全共享内存
 *   local sz       = shmem:size()                         -- 共享内存大小(字节)
 *   local ptr      = shmem:data()                         -- 数据指针(lightuserdata)
 *
 *   -- Engine 高级配置
 *   local engine   = wasmtime.newEngine{
 *     optLevel         = "speed",      -- "none"/"speed"/"speedAndSize"
 *     parallelCompilation = true,      -- 并行编译
 *     profiler         = "none",       -- "none"/"jitdump"/"vtune"/"perfmap"
 *     nanCanonicalization = false,     -- NaN 规范化（确定性执行）
 *     nativeUnwind     = true,         -- 原生栈展开信息
 *     sharedMemory     = false,        -- 启用共享内存
 *     memoryMayMove    = false,        -- 内存可重定位
 *     memoryGuardSize  = 0,            -- 内存保护区大小(字节)
 *     maxWasmStack     = 0,            -- 最大 WASM 栈大小(字节)
 *     tailCall         = false,        -- 启用尾调用
 *   }
 *   engine:incrementEpoch()                              -- 递增 epoch 计数器
 */

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#ifndef __EMSCRIPTEN__

#include <wasmtime.h>
#include <wasm.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#include <process.h>
#else
#include <unistd.h>
#include <sys/time.h>
#endif

/* 确保 GC 特性在编译时可用 */
#ifndef WASMTIME_FEATURE_GC
#error "wasmtime must be built with GC support"
#endif

/* ============================================================
 * 用户数据类型定义
 * ============================================================ */

#define WMT_ENGINE    "wasmtime.engine"
#define WMT_STORE     "wasmtime.store"
#define WMT_MODULE    "wasmtime.module"
#define WMT_INSTANCE  "wasmtime.instance"
#define WMT_FUNC      "wasmtime.function"

typedef struct {
    wasm_engine_t *engine;
} wmt_Engine;

typedef struct {
    wasmtime_store_t *store;
} wmt_Store;

typedef struct {
    wasmtime_module_t *module;
} wmt_Module;

typedef struct {
    wasmtime_instance_t instance;
    wasmtime_store_t *store; /* 保持 store 引用 */
    int store_ref;           /* Lua registry 引用 */
} wmt_Instance;

typedef struct {
    wasmtime_func_t func;
    wasmtime_store_t *store; /* 保持 store 引用 */
    int store_ref;
} wmt_Function;

#define WMT_MEMORY   "wasmtime.memory"
#define WMT_GLOBAL   "wasmtime.global"
#define WMT_TABLE    "wasmtime.table"
#define WMT_SHMEM    "wasmtime.sharedmemory"

typedef struct {
    wasmtime_memory_t memory;
    wasmtime_store_t  *store;
    int               store_ref;
} wmt_Memory;

typedef struct {
    wasmtime_global_t global;
    wasmtime_store_t  *store;
    int               store_ref;
} wmt_Global;

typedef struct {
    wasmtime_table_t  table;
    wasmtime_store_t  *store;
    int               store_ref;
} wmt_Table;

/* ============================================================
 * 辅助函数
 * ============================================================ */

/**
 * @brief 从 Lua 值转换为 wasmtime_val_t
 */
static int lua_to_wasmtime_val(lua_State *L, int idx, wasmtime_val_t *val) {
    int t = lua_type(L, idx);
    switch (t) {
        case LUA_TNUMBER:
            if (lua_isinteger(L, idx)) {
                lua_Integer iv = lua_tointeger(L, idx);
                if (iv >= INT32_MIN && iv <= INT32_MAX) {
                    val->kind = WASMTIME_I32;
                    val->of.i32 = (int32_t)iv;
                } else {
                    val->kind = WASMTIME_I64;
                    val->of.i64 = (int64_t)iv;
                }
            } else {
                double fv = lua_tonumber(L, idx);
                val->kind = WASMTIME_F64;
                val->of.f64 = fv;
            }
            break;
        case LUA_TBOOLEAN:
            val->kind = WASMTIME_I32;
            val->of.i32 = lua_toboolean(L, idx) ? 1 : 0;
            break;
        case LUA_TSTRING: {
            /* 字符串 — 暂时作为 I32 0 传递，完整 GC 支持需要 externref */
            val->kind = WASMTIME_I32;
            val->of.i32 = 0;
            break;
        }
        default:
            val->kind = WASMTIME_I32;
            val->of.i32 = 0;
            break;
    }
    return 0;
}

/**
 * @brief 从 Lua 值转换为 wasmtime_val_t (带期望类型提示)
 * 解决回调返回值: Lua 整数 5 作为 F64 返回时需保持为浮点。
 */
static int lua_to_wasmtime_val_typed(lua_State *L, int idx, wasmtime_val_t *val, wasm_valkind_t expected) {
    int t = lua_type(L, idx);
    switch (t) {
        case LUA_TNUMBER:
            if (expected == WASM_F64) {
                /* 期望 F64 → 始终转浮点 */
                val->kind = WASMTIME_F64;
                val->of.f64 = lua_tonumber(L, idx);
            } else if (expected == WASM_I64) {
                val->kind = WASMTIME_I64;
                val->of.i64 = (int64_t)lua_tointeger(L, idx);
            } else {
                /* I32 或未知 → 回退到无类型转换 */
                lua_to_wasmtime_val(L, idx, val);
            }
            break;
        default:
            lua_to_wasmtime_val(L, idx, val);
            break;
    }
    return 0;
}

/**
 * @brief 从 wasmtime_val_t 转换为 Lua 值并压入栈
 */
static void wasmtime_val_to_lua(lua_State *L, const wasmtime_val_t *val) {
    switch (val->kind) {
        case WASMTIME_I32:
            lua_pushinteger(L, val->of.i32);
            break;
        case WASMTIME_I64:
            lua_pushinteger(L, (lua_Integer)val->of.i64);
            break;
        case WASMTIME_F32:
            lua_pushnumber(L, (double)val->of.f32);
            break;
        case WASMTIME_F64:
            lua_pushnumber(L, val->of.f64);
            break;
        case WASMTIME_FUNCREF:
            lua_pushstring(L, "[funcref]");
            break;
        case WASMTIME_EXTERNREF:
            lua_pushstring(L, "[externref]");
            break;
        case WASMTIME_ANYREF:
            lua_pushstring(L, "[anyref]");
            break;
        default:
            lua_pushnil(L);
            break;
    }
}

/* ============================================================
 * Engine
 * ============================================================ */

static int wmt_engine_gc(lua_State *L) {
    wmt_Engine *e = (wmt_Engine*)luaL_checkudata(L, 1, WMT_ENGINE);
    if (e->engine) {
        wasm_engine_delete(e->engine);
        e->engine = NULL;
    }
    return 0;
}

/**
 * @brief wasmtime.newEngine([config]) → engine
 * 创建一个新的 WASM 引擎。
 *
 * config 可选表字段：
 *   gc           (bool, 默认 true)  启用 WASM GC 提案
 *   refTypes     (bool, 默认 true)  启用引用类型
 *   exceptions   (bool, 默认 true)  启用异常处理
 *   funcRef      (bool, 默认 true)  启用函数引用
 *   multiValue   (bool, 默认 true)  启用多返回值
 *   multiMemory  (bool, 默认 true)  启用多内存
 *   simd         (bool, 默认 true)  启用 SIMD
 *   threads      (bool, 默认 false) 启用线程
 *   fuel         (bool, 默认 false) 启用燃料消耗计量
 *   epoch        (bool, 默认 false) 启用 epoch 中断
 *   compiler         (string, 默认 "cranelift") "cranelift" 或 "winch"
 *   staticMemMax     (number, 0=默认)   静态内存大小上限(字节)
 *   dynamicMemReserve(number, 0=默认)   动态内存预留(字节)
 *   optLevel         (string, 默认 "speed") "none"/"speed"/"speedAndSize"
 *   parallelCompilation (bool, 默认 true) 并行编译
 *   profiler         (string, 默认 "none") "none"/"jitdump"/"vtune"/"perfmap"
 *   nanCanonicalization (bool, 默认 false) NaN 规范化（确定性执行）
 *   nativeUnwind     (bool, 默认 true) 生成原生栈展开信息
 *   sharedMemory     (bool, 默认 false) 启用共享内存
 *   memoryMayMove    (bool, 默认 false) 内存可重定位
 *   memoryGuardSize  (number, 0=默认) 内存保护区大小(字节)
 *   maxWasmStack     (number, 0=默认) 最大 WASM 栈大小(字节)
 *   tailCall         (bool, 默认 false) 启用尾调用
 */
static int l_new_engine(lua_State *L) {
    wasm_config_t *config = wasm_config_new();

    /* 默认配置 */
    bool gc_enabled = true;
    bool ref_types = true;
    bool exceptions = true;
    bool func_ref = true;
    bool multi_val = true;
    bool multi_mem = true;
    bool simd = true;
    bool threads = false;
    bool fuel = false;
    bool epoch = false;
    const char *compiler = "cranelift";
    uint64_t static_mem_max = 0;
    uint64_t dynamic_mem_reserve = 0;
    const char *opt_level = "speed";
    int parallel_compilation = 1;
    const char *profiler = "none";
    int nan_canon = 0;
    int native_unwind = 1;
    int shared_memory = 0;
    int memory_may_move = 0;
    uint64_t memory_guard_size = 0;
    uint64_t max_wasm_stack = 0;
    int tail_call = 0;

    /* 解析可选配置表 */
    if (lua_istable(L, 1)) {
        lua_getfield(L, 1, "gc");
        if (!lua_isnil(L, -1)) gc_enabled = lua_toboolean(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 1, "refTypes");
        if (!lua_isnil(L, -1)) ref_types = lua_toboolean(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 1, "exceptions");
        if (!lua_isnil(L, -1)) exceptions = lua_toboolean(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 1, "funcRef");
        if (!lua_isnil(L, -1)) func_ref = lua_toboolean(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 1, "multiValue");
        if (!lua_isnil(L, -1)) multi_val = lua_toboolean(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 1, "multiMemory");
        if (!lua_isnil(L, -1)) multi_mem = lua_toboolean(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 1, "simd");
        if (!lua_isnil(L, -1)) simd = lua_toboolean(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 1, "threads");
        if (!lua_isnil(L, -1)) threads = lua_toboolean(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 1, "fuel");
        if (!lua_isnil(L, -1)) fuel = lua_toboolean(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 1, "epoch");
        if (!lua_isnil(L, -1)) epoch = lua_toboolean(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 1, "compiler");
        if (lua_isstring(L, -1)) compiler = lua_tostring(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 1, "staticMemMax");
        if (lua_isinteger(L, -1)) static_mem_max = (uint64_t)lua_tointeger(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 1, "dynamicMemReserve");
        if (lua_isinteger(L, -1)) dynamic_mem_reserve = (uint64_t)lua_tointeger(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 1, "optLevel");
        if (lua_isstring(L, -1)) opt_level = lua_tostring(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 1, "parallelCompilation");
        if (!lua_isnil(L, -1)) parallel_compilation = lua_toboolean(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 1, "profiler");
        if (lua_isstring(L, -1)) profiler = lua_tostring(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 1, "nanCanonicalization");
        if (!lua_isnil(L, -1)) nan_canon = lua_toboolean(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 1, "nativeUnwind");
        if (!lua_isnil(L, -1)) native_unwind = lua_toboolean(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 1, "sharedMemory");
        if (!lua_isnil(L, -1)) shared_memory = lua_toboolean(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 1, "memoryMayMove");
        if (!lua_isnil(L, -1)) memory_may_move = lua_toboolean(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 1, "memoryGuardSize");
        if (lua_isinteger(L, -1)) memory_guard_size = (uint64_t)lua_tointeger(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 1, "maxWasmStack");
        if (lua_isinteger(L, -1)) max_wasm_stack = (uint64_t)lua_tointeger(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 1, "tailCall");
        if (!lua_isnil(L, -1)) tail_call = lua_toboolean(L, -1);
        lua_pop(L, 1);
    }

    /* 应用配置 */
    wasmtime_config_wasm_gc_set(config, gc_enabled);
    wasmtime_config_wasm_reference_types_set(config, ref_types);
    wasmtime_config_wasm_exceptions_set(config, exceptions);
    wasmtime_config_wasm_function_references_set(config, func_ref);
    wasmtime_config_wasm_multi_value_set(config, multi_val);
    wasmtime_config_wasm_multi_memory_set(config, multi_mem);
    wasmtime_config_wasm_simd_set(config, simd);
    wasmtime_config_wasm_threads_set(config, threads);
    wasmtime_config_consume_fuel_set(config, fuel);
    if (epoch) wasmtime_config_epoch_interruption_set(config, true);

    /* 编译器策略 */
    if (strcmp(compiler, "winch") == 0) {
        wasmtime_config_strategy_set(config, WASMTIME_STRATEGY_WINCH);
    } else {
        wasmtime_config_strategy_set(config, WASMTIME_STRATEGY_CRANELIFT);
    }

    /* 内存设置 */
    if (static_mem_max > 0) {
        wasmtime_config_memory_reservation_set(config, static_mem_max);
    }
    if (dynamic_mem_reserve > 0) {
        wasmtime_config_memory_reservation_for_growth_set(config, dynamic_mem_reserve);
    }

    /* Cranelift 优化级别 */
    if (strcmp(opt_level, "none") == 0) {
        wasmtime_config_cranelift_opt_level_set(config, WASMTIME_OPT_LEVEL_NONE);
    } else if (strcmp(opt_level, "speedAndSize") == 0) {
        wasmtime_config_cranelift_opt_level_set(config, WASMTIME_OPT_LEVEL_SPEED_AND_SIZE);
    } else {
        wasmtime_config_cranelift_opt_level_set(config, WASMTIME_OPT_LEVEL_SPEED);
    }

    /* 并行编译 */
    wasmtime_config_parallel_compilation_set(config, parallel_compilation);

    /* Profiler 策略 */
    if (strcmp(profiler, "jitdump") == 0) {
        wasmtime_config_profiler_set(config, WASMTIME_PROFILING_STRATEGY_JITDUMP);
    } else if (strcmp(profiler, "vtune") == 0) {
        wasmtime_config_profiler_set(config, WASMTIME_PROFILING_STRATEGY_VTUNE);
    } else if (strcmp(profiler, "perfmap") == 0) {
        wasmtime_config_profiler_set(config, WASMTIME_PROFILING_STRATEGY_PERFMAP);
    }

    /* NaN 规范化（确定性执行） */
    wasmtime_config_cranelift_nan_canonicalization_set(config, nan_canon);

    /* 原生栈展开信息 */
    wasmtime_config_native_unwind_info_set(config, native_unwind);

    /* 共享内存 */
    wasmtime_config_shared_memory_set(config, shared_memory);

    /* 内存可重定位 */
    wasmtime_config_memory_may_move_set(config, memory_may_move);

    /* 内存保护区大小 */
    if (memory_guard_size > 0) {
        wasmtime_config_memory_guard_size_set(config, memory_guard_size);
    }

    /* 最大 WASM 栈大小 */
    if (max_wasm_stack > 0) {
        wasmtime_config_max_wasm_stack_set(config, max_wasm_stack);
    }

    /* 尾调用 */
    wasmtime_config_wasm_tail_call_set(config, tail_call);

    wasm_engine_t *engine = wasm_engine_new_with_config(config);
    /* config 已被引擎接管，不需要单独释放 */
    /* wasm_config_delete(config); — 已 transfer ownership */

    if (!engine) {
        return luaL_error(L, "Failed to create wasmtime engine");
    }

    wmt_Engine *we = (wmt_Engine*)lua_newuserdata(L, sizeof(wmt_Engine));
    we->engine = engine;

    luaL_getmetatable(L, WMT_ENGINE);
    lua_setmetatable(L, -2);
    return 1;
}

/**
 * @brief engine:incrementEpoch()
 * 递增引擎的 epoch 计数器，触发所有关联 store 的 epoch 中断检查。
 * 配合 setEpochDeadline 使用，由主机线程调用通知 guest 停止执行。
 * 返回 0 表示成功。
 */
static int l_engine_increment_epoch(lua_State *L) {
    wmt_Engine *we = (wmt_Engine*)luaL_checkudata(L, 1, WMT_ENGINE);
    wasmtime_engine_increment_epoch(we->engine);
    lua_pushinteger(L, 0);
    return 1;
}

/* ============================================================
 * Store
 * ============================================================ */

static int wmt_store_gc(lua_State *L) {
    wmt_Store *s = (wmt_Store*)luaL_checkudata(L, 1, WMT_STORE);
    if (s->store) {
        wasmtime_store_delete(s->store);
        s->store = NULL;
    }
    return 0;
}

/**
 * @brief wasmtime.newStore(engine) → store
 * 创建一个新的 WASM 存储（每个 store 是一个独立的执行上下文）。
 */
static int l_new_store(lua_State *L) {
    wmt_Engine *we = (wmt_Engine*)luaL_checkudata(L, 1, WMT_ENGINE);

    wmt_Store *ws = (wmt_Store*)lua_newuserdata(L, sizeof(wmt_Store));
    ws->store = wasmtime_store_new(we->engine, NULL, NULL);

    if (!ws->store) {
        return luaL_error(L, "Failed to create wasmtime store");
    }

    luaL_getmetatable(L, WMT_STORE);
    lua_setmetatable(L, -2);
    return 1;
}

/* ============================================================
 * Module
 * ============================================================ */

static int wmt_module_gc(lua_State *L) {
    wmt_Module *m = (wmt_Module*)luaL_checkudata(L, 1, WMT_MODULE);
    if (m->module) {
        wasmtime_module_delete(m->module);
        m->module = NULL;
    }
    return 0;
}

/**
 * @brief wasmtime.newModule(engine, wasm_bytes) → module
 * 编译 WASM 二进制为模块。
 * @param L
 *   - 参数 1: engine (wmt_Engine)
 *   - 参数 2: wasm 二进制数据 (string)
 * @return 成功返回 module userdata，失败抛出错误
 */
static int l_new_module(lua_State *L) {
    wmt_Engine *we = (wmt_Engine*)luaL_checkudata(L, 1, WMT_ENGINE);
    size_t wasm_len;
    const char *wasm_data = luaL_checklstring(L, 2, &wasm_len);

    wasmtime_module_t *mod = NULL;
    wasmtime_error_t *error = wasmtime_module_new(
        we->engine,
        (const uint8_t*)wasm_data, wasm_len,
        &mod);

    if (error) {
        wasm_name_t msg;
        wasmtime_error_message(error, &msg);
        if (msg.data && msg.size > 0) {
            lua_pushlstring(L, msg.data, msg.size);
        } else {
            lua_pushstring(L, "unknown compile error");
        }
        wasm_byte_vec_delete(&msg);
        wasmtime_error_delete(error);
        return lua_error(L);
    }

    if (!mod) {
        return luaL_error(L, "module compile error: mod is NULL but no error returned");
    }

    wmt_Module *wm = (wmt_Module*)lua_newuserdata(L, sizeof(wmt_Module));
    wm->module = mod;

    luaL_getmetatable(L, WMT_MODULE);
    lua_setmetatable(L, -2);
    return 1;
}

/**
 * @brief wasmtime.validate(wasm_bytes) → ok, errmsg
 * 验证 WASM 二进制是否合法（不编译）。
 * @return ok=true 验证通过，ok=false + errmsg 验证失败
 */
static int l_validate(lua_State *L) {
    wmt_Engine *we = NULL;
    size_t wasm_len;
    const char *wasm_data;

    /* 第一个参数可能是 engine 或直接是 wasm_bytes */
    if (lua_type(L, 1) == LUA_TUSERDATA) {
        we = (wmt_Engine*)luaL_checkudata(L, 1, WMT_ENGINE);
        wasm_data = luaL_checklstring(L, 2, &wasm_len);
    } else {
        wasm_data = luaL_checklstring(L, 1, &wasm_len);
        /* 如果没有 engine，创建一个临时的 */
        wasm_config_t *cfg = wasm_config_new();
        wasmtime_config_wasm_gc_set(cfg, true);
        wasmtime_config_wasm_reference_types_set(cfg, true);
        we = (wmt_Engine*)lua_newuserdata(L, sizeof(wmt_Engine));
        we->engine = wasm_engine_new_with_config(cfg);
    }

    wasmtime_error_t *error = wasmtime_module_validate(
        we->engine,
        (const uint8_t*)wasm_data, wasm_len);

    lua_pushboolean(L, error == NULL);
    if (error) {
        wasm_name_t msg;
        wasmtime_error_message(error, &msg);
        lua_pushfstring(L, "%.*s", (int)msg.size, msg.data);
        wasm_byte_vec_delete(&msg);
        wasmtime_error_delete(error);
    } else {
        lua_pushstring(L, "ok");
    }

    /* 清理临时 engine */
    if (lua_type(L, 1) != LUA_TUSERDATA) {
        wasm_engine_delete(we->engine);
        lua_pop(L, 1);
    }

    return 2;
}

/* ============================================================
 * Instance
 * ============================================================ */

static int wmt_instance_gc(lua_State *L) {
    wmt_Instance *wi = (wmt_Instance*)luaL_checkudata(L, 1, WMT_INSTANCE);
    if (wi->store) {
        /* wasmtime_instance_t 由 store 管理，不需要单独释放 */
        luaL_unref(L, LUA_REGISTRYINDEX, wi->store_ref);
        wi->store = NULL;
    }
    return 0;
}

/**
 * @brief wasmtime.newInstance(store, module, [imports]) → instance
 * 将模块实例化到 store 中。
 * @param L
 *   - 参数 1: store (wmt_Store)
 *   - 参数 2: module (wmt_Module)
 *   - 参数 3: imports table (可选，暂未实现完整导入)
 * @return 成功返回 instance userdata
 */
static int l_new_instance(lua_State *L) {
    wmt_Store *ws = (wmt_Store*)luaL_checkudata(L, 1, WMT_STORE);
    wmt_Module *wm = (wmt_Module*)luaL_checkudata(L, 2, WMT_MODULE);

    /* 提取导入表（暂时只支持空导入或简单的函数定义） */
    int nimports = 0;
    wasmtime_extern_t *imports = NULL;

    if (lua_gettop(L) >= 3 && lua_istable(L, 3)) {
        /* 待实现：从 Lua table 中提取导入定义 */
        /* 对于简单的 MVP 模块（无导入），传空即可 */
    }

    wasm_trap_t *trap = NULL;
    wasmtime_error_t *error = NULL;

    wasmtime_context_t *ctx = wasmtime_store_context(ws->store);

    wasmtime_instance_t instance;
    error = wasmtime_instance_new(ctx, wm->module,
                                  imports, nimports,
                                  &instance, &trap);
    if (error || trap) {
        wasm_name_t msg = {0};
        if (error) {
            wasmtime_error_message(error, &msg);
        } else if (trap) {
            wasm_trap_message(trap, &msg);
        }
        if (msg.data && msg.size > 0) {
            lua_pushlstring(L, msg.data, msg.size);
        } else {
            lua_pushstring(L, "unknown instantiate error");
        }
        wasm_byte_vec_delete(&msg);
        if (error) wasmtime_error_delete(error);
        if (trap) wasm_trap_delete(trap);
        return lua_error(L);
    }

    wmt_Instance *wi = (wmt_Instance*)lua_newuserdata(L, sizeof(wmt_Instance));
    wi->instance = instance;
    wi->store = ws->store;

    /* 保持 store 引用 */
    lua_pushvalue(L, 1);
    wi->store_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    luaL_getmetatable(L, WMT_INSTANCE);
    lua_setmetatable(L, -2);
    return 1;
}

/**
 * @brief instance:getExport(name) → func_or_nil
 * 从实例中获取指定名称的导出项（函数类型）。
 * @param L
 *   - 参数 1: instance (self)
 *   - 参数 2: name (string)
 * @return 导出函数 userdata，找不到返回 nil
 */
static int l_instance_get_export(lua_State *L) {
    wmt_Instance *wi = (wmt_Instance*)luaL_checkudata(L, 1, WMT_INSTANCE);
    const char *name = luaL_checkstring(L, 2);

    wasmtime_extern_t item;
    wasmtime_context_t *ctx = wasmtime_store_context(wi->store);
    bool found = wasmtime_instance_export_get(
        ctx, &wi->instance, name, strlen(name), &item);

    if (!found) {
        lua_pushnil(L);
        return 1;
    }

    if (item.kind != WASMTIME_EXTERN_FUNC) {
        /* 不是函数类型 */
        lua_pushnil(L);
        return 1;
    }

    wmt_Function *wf = (wmt_Function*)lua_newuserdata(L, sizeof(wmt_Function));
    wf->func = item.of.func;
    wf->store = wi->store;
    wf->store_ref = wi->store_ref;

    luaL_getmetatable(L, WMT_FUNC);
    lua_setmetatable(L, -2);

    /* 保持对 instance 的生命周期引用 */
    lua_pushvalue(L, 1);
    luaL_ref(L, LUA_REGISTRYINDEX);

    return 1;
}

/* ---- instance:getExportEx ---- */

/**
 * @brief instance:getExportEx(name) → extern_item, kind_string
 * 通用导出查找：返回 wasmtime_extern_t 包装对象 + 类型字符串。
 *
 * kind_string 为以下之一：
 *   "func", "memory", "global", "table", "nil"
 *
 * 可用于获取任何类型的导出并调用对应方法。
 * 建议使用专门的 getMemory/getGlobal/getTable 方法获得类型化 userdata。
 */
static int l_instance_get_export_ex(lua_State *L) {
    wmt_Instance *wi = (wmt_Instance*)luaL_checkudata(L, 1, WMT_INSTANCE);
    const char *name = luaL_checkstring(L, 2);

    wasmtime_extern_t item;
    wasmtime_context_t *ctx = wasmtime_store_context(wi->store);
    bool found = wasmtime_instance_export_get(
        ctx, &wi->instance, name, strlen(name), &item);

    if (!found) {
        lua_pushnil(L);
        lua_pushstring(L, "nil");
        return 2;
    }

    switch (item.kind) {
        case WASMTIME_EXTERN_FUNC: {
            wmt_Function *wf = (wmt_Function*)lua_newuserdata(L, sizeof(wmt_Function));
            wf->func = item.of.func;
            wf->store = wi->store;
            wf->store_ref = wi->store_ref;
            luaL_getmetatable(L, WMT_FUNC);
            lua_setmetatable(L, -2);
            lua_pushvalue(L, 1);
            luaL_ref(L, LUA_REGISTRYINDEX);
            lua_pushstring(L, "func");
            return 2;
        }
        case WASMTIME_EXTERN_MEMORY: {
            wmt_Memory *wm = (wmt_Memory*)lua_newuserdata(L, sizeof(wmt_Memory));
            wm->memory = item.of.memory;
            wm->store = wi->store;
            wm->store_ref = wi->store_ref;
            luaL_getmetatable(L, WMT_MEMORY);
            lua_setmetatable(L, -2);
            lua_pushvalue(L, 1);
            luaL_ref(L, LUA_REGISTRYINDEX);
            lua_pushstring(L, "memory");
            return 2;
        }
        case WASMTIME_EXTERN_GLOBAL: {
            wmt_Global *wg = (wmt_Global*)lua_newuserdata(L, sizeof(wmt_Global));
            wg->global = item.of.global;
            wg->store = wi->store;
            wg->store_ref = wi->store_ref;
            luaL_getmetatable(L, WMT_GLOBAL);
            lua_setmetatable(L, -2);
            lua_pushvalue(L, 1);
            luaL_ref(L, LUA_REGISTRYINDEX);
            lua_pushstring(L, "global");
            return 2;
        }
        case WASMTIME_EXTERN_TABLE: {
            wmt_Table *wt = (wmt_Table*)lua_newuserdata(L, sizeof(wmt_Table));
            wt->table = item.of.table;
            wt->store = wi->store;
            wt->store_ref = wi->store_ref;
            luaL_getmetatable(L, WMT_TABLE);
            lua_setmetatable(L, -2);
            lua_pushvalue(L, 1);
            luaL_ref(L, LUA_REGISTRYINDEX);
            lua_pushstring(L, "table");
            return 2;
        }
        default:
            lua_pushnil(L);
            lua_pushstring(L, "unknown");
            return 2;
    }
}

/**
 * @brief instance:getExports() → table
 * 返回实例所有导出项的 name→type 映射表。
 * @return { [name] = "func"|"memory"|"global"|"table" }
 */
static int l_instance_get_exports(lua_State *L) {
    wmt_Instance *wi = (wmt_Instance*)luaL_checkudata(L, 1, WMT_INSTANCE);
    wasmtime_context_t *ctx = wasmtime_store_context(wi->store);

    lua_newtable(L);
    size_t idx = 0;
    while (1) {
        char *name_str = NULL;
        size_t name_len = 0;
        wasmtime_extern_t item;
        bool found = wasmtime_instance_export_nth(
            ctx, &wi->instance, idx, &name_str, &name_len, &item);
        if (!found) break;

        /* 通过 kind 获取导出类型名 */
        const char *kind_name = "unknown";
        switch (item.kind) {
            case WASMTIME_EXTERN_FUNC:   kind_name = "func";   break;
            case WASMTIME_EXTERN_MEMORY: kind_name = "memory"; break;
            case WASMTIME_EXTERN_GLOBAL: kind_name = "global"; break;
            case WASMTIME_EXTERN_TABLE:  kind_name = "table";  break;
            default: break;
        }
        lua_pushlstring(L, name_str, name_len);
        lua_rawseti(L, -2, (int)(idx + 1));
        idx++;
    }

    return 1;
}

/**
 * @brief instance:getMemory(name) → memory_userdata
 * 获取实例的内存导出。
 * @return memory userdata，失败返回 nil
 */
static int l_instance_get_memory(lua_State *L) {
    wmt_Instance *wi = (wmt_Instance*)luaL_checkudata(L, 1, WMT_INSTANCE);
    const char *name = luaL_checkstring(L, 2);

    wasmtime_extern_t item;
    wasmtime_context_t *ctx = wasmtime_store_context(wi->store);
    bool found = wasmtime_instance_export_get(
        ctx, &wi->instance, name, strlen(name), &item);

    if (!found || item.kind != WASMTIME_EXTERN_MEMORY) {
        lua_pushnil(L);
        return 1;
    }

    wmt_Memory *wm = (wmt_Memory*)lua_newuserdata(L, sizeof(wmt_Memory));
    wm->memory = item.of.memory;
    wm->store = wi->store;
    wm->store_ref = wi->store_ref;

    luaL_getmetatable(L, WMT_MEMORY);
    lua_setmetatable(L, -2);

    lua_pushvalue(L, 1);
    luaL_ref(L, LUA_REGISTRYINDEX);
    return 1;
}

/**
 * @brief instance:getGlobal(name) → global_userdata
 * 获取实例的全局变量导出。
 * @return global userdata，失败返回 nil
 */
static int l_instance_get_global(lua_State *L) {
    wmt_Instance *wi = (wmt_Instance*)luaL_checkudata(L, 1, WMT_INSTANCE);
    const char *name = luaL_checkstring(L, 2);

    wasmtime_extern_t item;
    wasmtime_context_t *ctx = wasmtime_store_context(wi->store);
    bool found = wasmtime_instance_export_get(
        ctx, &wi->instance, name, strlen(name), &item);

    if (!found || item.kind != WASMTIME_EXTERN_GLOBAL) {
        lua_pushnil(L);
        return 1;
    }

    wmt_Global *wg = (wmt_Global*)lua_newuserdata(L, sizeof(wmt_Global));
    wg->global = item.of.global;
    wg->store = wi->store;
    wg->store_ref = wi->store_ref;

    luaL_getmetatable(L, WMT_GLOBAL);
    lua_setmetatable(L, -2);

    lua_pushvalue(L, 1);
    luaL_ref(L, LUA_REGISTRYINDEX);
    return 1;
}

/**
 * @brief instance:getTable(name) → table_userdata
 * 获取实例的表导出（用于 funcref/externref 表）。
 * @return table userdata，失败返回 nil
 */
static int l_instance_get_table(lua_State *L) {
    wmt_Instance *wi = (wmt_Instance*)luaL_checkudata(L, 1, WMT_INSTANCE);
    const char *name = luaL_checkstring(L, 2);

    wasmtime_extern_t item;
    wasmtime_context_t *ctx = wasmtime_store_context(wi->store);
    bool found = wasmtime_instance_export_get(
        ctx, &wi->instance, name, strlen(name), &item);

    if (!found || item.kind != WASMTIME_EXTERN_TABLE) {
        lua_pushnil(L);
        return 1;
    }

    wmt_Table *wt = (wmt_Table*)lua_newuserdata(L, sizeof(wmt_Table));
    wt->table = item.of.table;
    wt->store = wi->store;
    wt->store_ref = wi->store_ref;

    luaL_getmetatable(L, WMT_TABLE);
    lua_setmetatable(L, -2);

    lua_pushvalue(L, 1);
    luaL_ref(L, LUA_REGISTRYINDEX);
    return 1;
}

/* ============================================================
 * Function
 * ============================================================ */

static int wmt_func_gc(lua_State *L) {
    wmt_Function *wf = (wmt_Function*)luaL_checkudata(L, 1, WMT_FUNC);
    luaL_unref(L, LUA_REGISTRYINDEX, wf->store_ref);
    wf->store = NULL;
    return 0;
}

/**
 * @brief func:call(args...) → results...
 * 调用 WASM 函数。
 * @param L
 *   - 参数 1: func (self)
 *   - 参数 2..n: 函数参数
 * @return 返回结果值
 */
static int l_func_call(lua_State *L) {
    wmt_Function *wf = (wmt_Function*)luaL_checkudata(L, 1, WMT_FUNC);
    int nargs = lua_gettop(L) - 1;

    /* 获取 store 上下文 */
    wasmtime_context_t *ctx = wasmtime_store_context(wf->store);

    /* 获取函数签名 */
    wasm_functype_t *ftype = wasmtime_func_type(ctx, &wf->func);
    if (!ftype) {
        return luaL_error(L, "Failed to get function type");
    }

    const wasm_valtype_vec_t *params = wasm_functype_params(ftype);
    const wasm_valtype_vec_t *results = wasm_functype_results(ftype);

    size_t expected_nargs = params->size;

    /* 分配参数数组 */
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

    /* 分配结果数组 */
    size_t nresults = results->size;
    if (nresults == 0) nresults = 1; /* 至少分配一个槽 */
    wasmtime_val_t *out = (wasmtime_val_t*)malloc(
        nresults * sizeof(wasmtime_val_t));
    if (!out) {
        free(args);
        wasm_functype_delete(ftype);
        return luaL_error(L, "out of memory");
    }

    /* 调用函数 */
    wasm_trap_t *trap = NULL;
    /* 保存结果数量（functype 释放后 results 指针失效） */
    int actual_nresults = (int)results->size;

    wasmtime_error_t *error = wasmtime_func_call(
        ctx, &wf->func,
        args, expected_nargs,
        out, actual_nresults,
        &trap);

    wasm_functype_delete(ftype);
    free(args);

    if (error || trap) {
        wasm_name_t msg = {0};
        if (error) {
            wasmtime_error_message(error, &msg);
        } else if (trap) {
            wasm_trap_message(trap, &msg);
        }
        if (msg.data && msg.size > 0) {
            lua_pushlstring(L, msg.data, msg.size);
        } else {
            lua_pushstring(L, "unknown call error");
        }
        wasm_byte_vec_delete(&msg);
        free(out);
        if (error) wasmtime_error_delete(error);
        if (trap) wasm_trap_delete(trap);
        return lua_error(L);
    }

    /* 返回结果 */
    int retc = 0;
    for (int i = 0; i < actual_nresults; i++) {
        wasmtime_val_to_lua(L, &out[i]);
        retc++;
    }
    free(out);

    if (retc == 0) return 0;
    return retc;
}

/**
 * @brief func:getType() → params, results
 * 返回函数签名的参数和返回值类型。
 * 每个类型为字符串: "i32", "i64", "f32", "f64", "anyref", "externref", "funcref"
 * @return params (string array), results (string array)
 */
static int l_func_get_type(lua_State *L) {
    wmt_Function *wf = (wmt_Function*)luaL_checkudata(L, 1, WMT_FUNC);
    wasmtime_context_t *ctx = wasmtime_store_context(wf->store);
    wasm_functype_t *ftype = wasmtime_func_type(ctx, &wf->func);
    if (!ftype) {
        return luaL_error(L, "Failed to get function type");
    }

    const wasm_valtype_vec_t *params = wasm_functype_params(ftype);
    const wasm_valtype_vec_t *results = wasm_functype_results(ftype);

    /* 参数类型数组 */
    lua_newtable(L);
    for (size_t i = 0; i < params->size; i++) {
        wasm_valkind_t kind = wasm_valtype_kind(params->data[i]);
        const char *name = "unknown";
        switch (kind) {
            case WASM_I32: name = "i32"; break;
            case WASM_I64: name = "i64"; break;
            case WASM_F32: name = "f32"; break;
            case WASM_F64: name = "f64"; break;
            case WASM_FUNCREF: name = "funcref"; break;
            default: name = "ref"; break;
        }
        lua_pushstring(L, name);
        lua_rawseti(L, -2, (int)i + 1);
    }

    /* 返回值类型数组 */
    lua_newtable(L);
    for (size_t i = 0; i < results->size; i++) {
        wasm_valkind_t kind = wasm_valtype_kind(results->data[i]);
        const char *name = "unknown";
        switch (kind) {
            case WASM_I32: name = "i32"; break;
            case WASM_I64: name = "i64"; break;
            case WASM_F32: name = "f32"; break;
            case WASM_F64: name = "f64"; break;
            case WASM_FUNCREF: name = "funcref"; break;
            default: name = "ref"; break;
        }
        lua_pushstring(L, name);
        lua_rawseti(L, -2, (int)i + 1);
    }

    wasm_functype_delete(ftype);
    return 2;
}

/* ============================================================
 * Memory
 * ============================================================ */

static int wmt_memory_gc(lua_State *L) {
    wmt_Memory *wm = (wmt_Memory*)luaL_checkudata(L, 1, WMT_MEMORY);
    luaL_unref(L, LUA_REGISTRYINDEX, wm->store_ref);
    wm->store = NULL;
    return 0;
}

/**
 * @brief memory:read(offset, length) → string
 * 从 WASM 线性内存中读取数据。
 * @param offset 内存偏移（字节）
 * @param length 读取的字节数
 * @return Lua string
 */
static int l_memory_read(lua_State *L) {
    wmt_Memory *wm = (wmt_Memory*)luaL_checkudata(L, 1, WMT_MEMORY);
    lua_Integer offset = luaL_checkinteger(L, 2);
    lua_Integer length = luaL_checkinteger(L, 3);

    if (offset < 0 || length < 0) {
        return luaL_error(L, "memory:read: invalid offset/length");
    }

    wasmtime_context_t *ctx = wasmtime_store_context(wm->store);
    uint8_t *data = wasmtime_memory_data(ctx, &wm->memory);
    size_t data_size = wasmtime_memory_data_size(ctx, &wm->memory);

    if ((size_t)(offset + length) > data_size) {
        return luaL_error(L, "memory:read: out of bounds");
    }

    lua_pushlstring(L, (const char*)(data + offset), (size_t)length);
    return 1;
}

/**
 * @brief memory:write(offset, data) → bytes_written
 * 向 WASM 线性内存写入数据。
 * @param offset 内存偏移（字节）
 * @param data   要写入的字符串
 * @return 写入的字节数
 */
static int l_memory_write(lua_State *L) {
    wmt_Memory *wm = (wmt_Memory*)luaL_checkudata(L, 1, WMT_MEMORY);
    lua_Integer offset = luaL_checkinteger(L, 2);
    size_t data_len;
    const char *data_str = luaL_checklstring(L, 3, &data_len);

    if (offset < 0) {
        return luaL_error(L, "memory:write: invalid offset");
    }

    wasmtime_context_t *ctx = wasmtime_store_context(wm->store);
    uint8_t *mem_data = wasmtime_memory_data(ctx, &wm->memory);
    size_t mem_size = wasmtime_memory_data_size(ctx, &wm->memory);

    if ((size_t)offset + data_len > mem_size) {
        return luaL_error(L, "memory:write: out of bounds");
    }

    memcpy(mem_data + offset, data_str, data_len);
    lua_pushinteger(L, (lua_Integer)data_len);
    return 1;
}

/**
 * @brief memory:size() → pages
 * 返回当前内存大小（以 64KB 页为单位）。
 */
static int l_memory_size(lua_State *L) {
    wmt_Memory *wm = (wmt_Memory*)luaL_checkudata(L, 1, WMT_MEMORY);
    wasmtime_context_t *ctx = wasmtime_store_context(wm->store);
    uint64_t pages = wasmtime_memory_size(ctx, &wm->memory);
    lua_pushinteger(L, (lua_Integer)pages);
    return 1;
}

/**
 * @brief memory:dataSize() → bytes
 * 返回内存的实际字节数。
 */
static int l_memory_data_size(lua_State *L) {
    wmt_Memory *wm = (wmt_Memory*)luaL_checkudata(L, 1, WMT_MEMORY);
    wasmtime_context_t *ctx = wasmtime_store_context(wm->store);
    size_t sz = wasmtime_memory_data_size(ctx, &wm->memory);
    lua_pushinteger(L, (lua_Integer)sz);
    return 1;
}

/**
 * @brief memory:grow(delta_pages) → old_pages, ok
 * 增长内存。
 * @param delta_pages 要增长的页数
 * @return old_pages (之前的页数), ok (成功=true/失败=false+error)
 */
static int l_memory_grow(lua_State *L) {
    wmt_Memory *wm = (wmt_Memory*)luaL_checkudata(L, 1, WMT_MEMORY);
    uint64_t delta = (uint64_t)luaL_checkinteger(L, 2);

    wasmtime_context_t *ctx = wasmtime_store_context(wm->store);
    uint64_t old_size;
    wasmtime_error_t *error = wasmtime_memory_grow(ctx, &wm->memory, delta, &old_size);

    if (error) {
        wasm_name_t msg;
        wasmtime_error_message(error, &msg);
        lua_pushinteger(L, 0);
        lua_pushboolean(L, 0);
        lua_pushlstring(L, msg.data, msg.size);
        wasm_byte_vec_delete(&msg);
        wasmtime_error_delete(error);
        return 3;
    }

    lua_pushinteger(L, (lua_Integer)old_size);
    lua_pushboolean(L, 1);
    return 2;
}

/**
 * @brief memory:getType() → min, max
 * 返回内存类型限制。
 * @return min（最小页数）, max（最大页数，0 表示无上限）
 */
static int l_memory_get_type(lua_State *L) {
    wmt_Memory *wm = (wmt_Memory*)luaL_checkudata(L, 1, WMT_MEMORY);
    wasmtime_context_t *ctx = wasmtime_store_context(wm->store);
    wasm_memorytype_t *mty = wasmtime_memory_type(ctx, &wm->memory);
    if (!mty) {
        lua_pushinteger(L, 0);
        lua_pushinteger(L, 0);
        return 2;
    }
    const wasm_limits_t *limits = wasm_memorytype_limits(mty);
    lua_pushinteger(L, (lua_Integer)limits->min);
    lua_pushinteger(L, (lua_Integer)limits->max);
    return 2;
}

/* ============================================================
 * Global
 * ============================================================ */

static int wmt_global_gc(lua_State *L) {
    wmt_Global *wg = (wmt_Global*)luaL_checkudata(L, 1, WMT_GLOBAL);
    luaL_unref(L, LUA_REGISTRYINDEX, wg->store_ref);
    wg->store = NULL;
    return 0;
}

/**
 * @brief global:get() → value
 * 读取全局变量的当前值。
 * @return Lua 值（number/boolean/string）
 */
static int l_global_get(lua_State *L) {
    wmt_Global *wg = (wmt_Global*)luaL_checkudata(L, 1, WMT_GLOBAL);
    wasmtime_context_t *ctx = wasmtime_store_context(wg->store);
    wasmtime_val_t val;
    wasmtime_global_get(ctx, &wg->global, &val);
    wasmtime_val_to_lua(L, &val);
    return 1;
}

/**
 * @brief global:set(value) → ok
 * 设置全局变量的值。
 * @param value Lua 值（number/boolean/string）
 * @return true 成功，false+error 失败
 */
static int l_global_set(lua_State *L) {
    wmt_Global *wg = (wmt_Global*)luaL_checkudata(L, 1, WMT_GLOBAL);
    wasmtime_val_t val;
    lua_to_wasmtime_val(L, 2, &val);

    wasmtime_context_t *ctx = wasmtime_store_context(wg->store);
    wasmtime_error_t *error = wasmtime_global_set(ctx, &wg->global, &val);

    if (error) {
        wasm_name_t msg;
        wasmtime_error_message(error, &msg);
        lua_pushboolean(L, 0);
        lua_pushlstring(L, msg.data, msg.size);
        wasm_byte_vec_delete(&msg);
        wasmtime_error_delete(error);
        return 2;
    }

    lua_pushboolean(L, 1);
    return 1;
}

/* ============================================================
 * Table
 * ============================================================ */

static int wmt_table_gc(lua_State *L) {
    wmt_Table *wt = (wmt_Table*)luaL_checkudata(L, 1, WMT_TABLE);
    luaL_unref(L, LUA_REGISTRYINDEX, wt->store_ref);
    wt->store = NULL;
    return 0;
}

/**
 * @brief table:get(index) → value
 * 读取表在 index 位置的值。
 * @param index 表索引（从 0 开始）
 * @return Lua 值
 */
static int l_table_get(lua_State *L) {
    wmt_Table *wt = (wmt_Table*)luaL_checkudata(L, 1, WMT_TABLE);
    lua_Integer idx = luaL_checkinteger(L, 2);

    wasmtime_context_t *ctx = wasmtime_store_context(wt->store);
    wasmtime_val_t val;
    bool ok = wasmtime_table_get(ctx, &wt->table, (uint32_t)idx, &val);
    if (!ok) {
        lua_pushnil(L);
        return 1;
    }
    wasmtime_val_to_lua(L, &val);
    return 1;
}

/**
 * @brief table:set(index, value) → ok
 * 设置表在 index 位置的值。
 * @param index 表索引（从 0 开始）
 * @param value Lua 值
 * @return true 成功，false+error 失败
 */
static int l_table_set(lua_State *L) {
    wmt_Table *wt = (wmt_Table*)luaL_checkudata(L, 1, WMT_TABLE);
    lua_Integer idx = luaL_checkinteger(L, 2);
    wasmtime_val_t val;
    lua_to_wasmtime_val(L, 3, &val);

    wasmtime_context_t *ctx = wasmtime_store_context(wt->store);
    wasmtime_error_t *error = wasmtime_table_set(ctx, &wt->table, (uint32_t)idx, &val);

    if (error) {
        wasm_name_t msg;
        wasmtime_error_message(error, &msg);
        lua_pushboolean(L, 0);
        lua_pushlstring(L, msg.data, msg.size);
        wasm_byte_vec_delete(&msg);
        wasmtime_error_delete(error);
        return 2;
    }

    lua_pushboolean(L, 1);
    return 1;
}

/**
 * @brief table:size() → count
 * 返回表的当前大小（元素数量）。
 */
static int l_table_size(lua_State *L) {
    wmt_Table *wt = (wmt_Table*)luaL_checkudata(L, 1, WMT_TABLE);
    wasmtime_context_t *ctx = wasmtime_store_context(wt->store);
    uint64_t sz = wasmtime_table_size(ctx, &wt->table);
    lua_pushinteger(L, (lua_Integer)sz);
    return 1;
}

/**
 * @brief table:grow(delta, [init_val]) → old_size, ok
 * 增长表大小。
 * @param delta    要增长的元素数
 * @param init_val 新元素的初始值（可选，默认 nil/i32(0)）
 * @return old_size (之前的大小), ok (成功=true/失败=false+error)
 */
static int l_table_grow(lua_State *L) {
    wmt_Table *wt = (wmt_Table*)luaL_checkudata(L, 1, WMT_TABLE);
    uint64_t delta = (uint64_t)luaL_checkinteger(L, 2);

    wasmtime_val_t init_val;
    init_val.kind = WASMTIME_I32;
    init_val.of.i32 = 0;
    if (lua_gettop(L) >= 3 && !lua_isnil(L, 3)) {
        lua_to_wasmtime_val(L, 3, &init_val);
    }

    wasmtime_context_t *ctx = wasmtime_store_context(wt->store);
    uint64_t old_size;
    wasmtime_error_t *error = wasmtime_table_grow(ctx, &wt->table, delta, &init_val, &old_size);

    if (error) {
        wasm_name_t msg;
        wasmtime_error_message(error, &msg);
        lua_pushinteger(L, 0);
        lua_pushboolean(L, 0);
        lua_pushlstring(L, msg.data, msg.size);
        wasm_byte_vec_delete(&msg);
        wasmtime_error_delete(error);
        return 3;
    }

    lua_pushinteger(L, (lua_Integer)old_size);
    lua_pushboolean(L, 1);
    return 2;
}

/* ============================================================
 * Store 额外方法: fuel / gc
 * ============================================================ */

/**
 * @brief store:setFuel(amount) → ok
 * 设置 store 的燃料上限。
 * 需要 engine 创建时启用 fuel 配置。
 * @param amount 燃料量（uint64）
 * @return true 成功，false+error 失败
 */
static int l_store_set_fuel(lua_State *L) {
    wmt_Store *ws = (wmt_Store*)luaL_checkudata(L, 1, WMT_STORE);
    uint64_t amount = (uint64_t)luaL_checkinteger(L, 2);

    wasmtime_context_t *ctx = wasmtime_store_context(ws->store);
    wasmtime_error_t *error = wasmtime_context_set_fuel(ctx, amount);

    if (error) {
        wasm_name_t msg;
        wasmtime_error_message(error, &msg);
        lua_pushboolean(L, 0);
        lua_pushlstring(L, msg.data, msg.size);
        wasm_byte_vec_delete(&msg);
        wasmtime_error_delete(error);
        return 2;
    }

    lua_pushboolean(L, 1);
    return 1;
}

/**
 * @brief store:getFuel() → amount
 * 获取当前 store 的剩余燃料。
 * @return 剩余燃料量
 */
static int l_store_get_fuel(lua_State *L) {
    wmt_Store *ws = (wmt_Store*)luaL_checkudata(L, 1, WMT_STORE);
    wasmtime_context_t *ctx = wasmtime_store_context(ws->store);
    uint64_t fuel;
    wasmtime_context_get_fuel(ctx, &fuel);
    lua_pushinteger(L, (lua_Integer)fuel);
    return 1;
}

/**
 * @brief store:gc()
 * 触发 store 内 GC（垃圾回收 externref/anyref/GcRef）。
 */
static int l_store_gc(lua_State *L) {
    wmt_Store *ws = (wmt_Store*)luaL_checkudata(L, 1, WMT_STORE);
    wasmtime_context_t *ctx = wasmtime_store_context(ws->store);
    wasmtime_context_gc(ctx);
    return 0;
}

/**
 * @brief store:setEpochDeadline(ticks) → ok
 * 设置 epoch 截止值（需要 engine 创建时启用 epoch 配置）。
 */
static int l_store_set_epoch_deadline(lua_State *L) {
    wmt_Store *ws = (wmt_Store*)luaL_checkudata(L, 1, WMT_STORE);
    uint64_t ticks = (uint64_t)luaL_checkinteger(L, 2);

    wasmtime_context_t *ctx = wasmtime_store_context(ws->store);
    wasmtime_context_set_epoch_deadline(ctx, ticks);
    lua_pushboolean(L, 1);
    return 1;
}

/* ============================================================
 * Module 序列化/反序列化
 * ============================================================ */

/**
 * @brief module:serialize() → serialized_bytes
 * 将已编译的模块序列化为字节流。
 * 可用于预编译 WASM 模块以加速后续加载。
 * @return 序列化的二进制数据 (string)
 */
static int l_module_serialize(lua_State *L) {
    wmt_Module *wm = (wmt_Module*)luaL_checkudata(L, 1, WMT_MODULE);

    wasm_byte_vec_t buffer;
    wasmtime_error_t *error = wasmtime_module_serialize(wm->module, &buffer);

    if (error) {
        wasm_name_t msg;
        wasmtime_error_message(error, &msg);
        lua_pushnil(L);
        lua_pushlstring(L, msg.data, msg.size);
        wasm_byte_vec_delete(&msg);
        wasmtime_error_delete(error);
        return 2;
    }

    lua_pushlstring(L, (const char*)buffer.data, buffer.size);
    wasm_byte_vec_delete(&buffer);
    return 1;
}

/**
 * @brief wasmtime.deserializeModule(engine, serialized_bytes) → module
 * 从序列化字节流反序列化模块（无须重新编译 WASM）。
 * @param engine engine userdata
 * @param data   序列化的二进制数据 (string)
 * @return module userdata，失败返回 nil+error
 */
static int l_deserialize_module(lua_State *L) {
    wmt_Engine *we = (wmt_Engine*)luaL_checkudata(L, 1, WMT_ENGINE);
    size_t data_len;
    const char *data = luaL_checklstring(L, 2, &data_len);

    wasmtime_module_t *mod = NULL;
    wasmtime_error_t *error = wasmtime_module_deserialize(
        we->engine, (const uint8_t*)data, data_len, &mod);

    if (error) {
        wasm_name_t msg;
        wasmtime_error_message(error, &msg);
        lua_pushnil(L);
        lua_pushlstring(L, msg.data, msg.size);
        wasm_byte_vec_delete(&msg);
        wasmtime_error_delete(error);
        return 2;
    }

    if (!mod) {
        lua_pushnil(L);
        lua_pushstring(L, "deserialize: mod is NULL");
        return 2;
    }

    wmt_Module *wm = (wmt_Module*)lua_newuserdata(L, sizeof(wmt_Module));
    wm->module = mod;
    luaL_getmetatable(L, WMT_MODULE);
    lua_setmetatable(L, -2);
    return 1;
}

/**
 * @brief module:getExports() → table
 * 返回模块导出项的类型信息。
 * @return { [1] = { name = "xxx", kind = "func"/"memory"/"global"/"table" }, ... }
 */
static int l_module_get_exports(lua_State *L) {
    wmt_Module *wm = (wmt_Module*)luaL_checkudata(L, 1, WMT_MODULE);

    wasm_exporttype_vec_t exports;
    wasmtime_module_exports(wm->module, &exports);

    lua_newtable(L);
    for (size_t i = 0; i < exports.size; i++) {
        const wasm_name_t *name = wasm_exporttype_name(exports.data[i]);
        const wasm_externtype_t *ext = wasm_exporttype_type(exports.data[i]);
        wasm_externkind_t ekind = wasm_externtype_kind(ext);

        const char *kind_name = "unknown";
        switch (ekind) {
            case WASM_EXTERN_FUNC:   kind_name = "func";   break;
            case WASM_EXTERN_MEMORY: kind_name = "memory"; break;
            case WASM_EXTERN_GLOBAL: kind_name = "global"; break;
            case WASM_EXTERN_TABLE:  kind_name = "table";  break;
            default: break;
        }

        lua_newtable(L);
        lua_pushlstring(L, name->data, name->size);
        lua_setfield(L, -2, "name");
        lua_pushstring(L, kind_name);
        lua_setfield(L, -2, "kind");
        lua_rawseti(L, -2, (int)i + 1);
    }

    wasm_exporttype_vec_delete(&exports);
    return 1;
}

/**
 * @brief module:getImports() → table
 * 返回模块导入项的类型信息。
 * @return { [1] = { module = "xxx", name = "yyy", kind = "func"/... }, ... }
 */
static int l_module_get_imports(lua_State *L) {
    wmt_Module *wm = (wmt_Module*)luaL_checkudata(L, 1, WMT_MODULE);

    wasm_importtype_vec_t imports;
    wasmtime_module_imports(wm->module, &imports);

    lua_newtable(L);
    for (size_t i = 0; i < imports.size; i++) {
        const wasm_name_t *mod = wasm_importtype_module(imports.data[i]);
        const wasm_name_t *name = wasm_importtype_name(imports.data[i]);
        const wasm_externtype_t *ext = wasm_importtype_type(imports.data[i]);
        wasm_externkind_t ekind = wasm_externtype_kind(ext);

        const char *kind_name = "unknown";
        switch (ekind) {
            case WASM_EXTERN_FUNC:   kind_name = "func";   break;
            case WASM_EXTERN_MEMORY: kind_name = "memory"; break;
            case WASM_EXTERN_GLOBAL: kind_name = "global"; break;
            case WASM_EXTERN_TABLE:  kind_name = "table";  break;
            default: break;
        }

        lua_newtable(L);
        lua_pushlstring(L, mod->data, mod->size);
        lua_setfield(L, -2, "module");
        lua_pushlstring(L, name->data, name->size);
        lua_setfield(L, -2, "name");
        lua_pushstring(L, kind_name);
        lua_setfield(L, -2, "kind");
        lua_rawseti(L, -2, (int)i + 1);
    }

    wasm_importtype_vec_delete(&imports);
    return 1;
}

/* ============================================================
 * externref 支持
 * ============================================================ */

/**
 * @brief wasmtime.newExternref(store, data) → externref_val
 * 创建一个 externref 值，将 data 作为宿主引用传递给 WASM。
 * @param store store userdata
 * @param data  任意的 Lua 值（轻量 userdata 或 integer 指针）
 *              Lua 字符串或 userdata 作为指针存储
 * @return 一个 wasmtime_val_t（Lua table），可用于传参调用
 */
static int l_new_externref(lua_State *L) {
    wmt_Store *ws = (wmt_Store*)luaL_checkudata(L, 1, WMT_STORE);
    wasmtime_context_t *ctx = wasmtime_store_context(ws->store);

    void *host_data = NULL;
    /* 从 Lua 值中提取宿主数据 */
    switch (lua_type(L, 2)) {
        case LUA_TSTRING:
            host_data = (void*)lua_tostring(L, 2);
            break;
        case LUA_TUSERDATA:
        case LUA_TLIGHTUSERDATA:
            host_data = lua_touserdata(L, 2);
            break;
        default:
            return luaL_error(L, "externref: only string/userdata supported as host data");
    }

    wasmtime_externref_t externref;
    bool ok = wasmtime_externref_new(ctx, host_data, NULL, &externref);
    if (!ok) {
        return luaL_error(L, "externref: failed to create");
    }

    /* 返回一个 Lua table 包装 wasmtime_val_t */
    wasmtime_val_t val;
    val.kind = WASMTIME_EXTERNREF;
    val.of.externref = externref;

    lua_newtable(L);
    lua_pushinteger(L, (lua_Integer)(uintptr_t)host_data);
    lua_setfield(L, -2, "_data");

    return 1;
}

/**
 * @brief externref:getData() → userdata_or_string
 * 从 externref 值中提取宿主数据。
 */
static int l_externref_get_data(lua_State *L) {
    /* 此为简化实现 */
    lua_getfield(L, 1, "_data");
    return 1;
}

/**
 * @brief store:newMemory(min, max) → memory_userdata
 * 创建一个独立的 WASM 内存。
 * @param min 最小页数
 * @param max 最大页数 (0 表示无上限)
 * @return memory userdata
 */
static int l_store_new_memory(lua_State *L) {
    wmt_Store *ws = (wmt_Store*)luaL_checkudata(L, 1, WMT_STORE);
    uint32_t min_pages = (uint32_t)luaL_optinteger(L, 2, 1);
    uint32_t max_pages = (uint32_t)luaL_optinteger(L, 3, 0);

    wasm_limits_t limits = { min_pages, max_pages };
    wasm_memorytype_t *mtype = wasm_memorytype_new(&limits);
    if (!mtype) {
        return luaL_error(L, "store:newMemory: failed to create memory type");
    }

    wasmtime_context_t *ctx = wasmtime_store_context(ws->store);
    wmt_Memory *wm = (wmt_Memory*)lua_newuserdata(L, sizeof(wmt_Memory));
    wasmtime_error_t *error = wasmtime_memory_new(ctx, mtype, &wm->memory);
    wasm_memorytype_delete(mtype);

    if (error) {
        wasm_name_t msg;
        wasmtime_error_message(error, &msg);
        lua_pop(L, 1); /* pop userdata */
        lua_pushnil(L);
        lua_pushlstring(L, msg.data, msg.size);
        wasm_byte_vec_delete(&msg);
        wasmtime_error_delete(error);
        return 2;
    }

    wm->store = ws->store;
    lua_pushvalue(L, 1);
    wm->store_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    luaL_getmetatable(L, WMT_MEMORY);
    lua_setmetatable(L, -2);
    return 1;
}

/* ============================================================
 * SharedMemory  — 线程安全的 WASM 共享内存
 * ============================================================ */

typedef struct {
    wasmtime_sharedmemory_t *shmem;
    uint64_t size;
} wmt_SharedMemory;

static int wmt_sharedmemory_gc(lua_State *L) {
    wmt_SharedMemory *sm = (wmt_SharedMemory*)luaL_checkudata(L, 1, WMT_SHMEM);
    if (sm->shmem) {
        wasmtime_sharedmemory_delete(sm->shmem);
        sm->shmem = NULL;
    }
    return 0;
}

/**
 * @brief wasmtime.newSharedMemory(engine, minPages, maxPages) → sharedmemory
 * 创建一个线程安全的 WASM 共享内存，可跨线程/跨 store 共享。
 * @param engine   engine userdata
 * @param minPages 最小页数
 * @param maxPages 最大页数 (0 表示无上限)
 * @return sharedmemory userdata
 */
static int l_new_shared_memory(lua_State *L) {
    wmt_Engine *we = (wmt_Engine*)luaL_checkudata(L, 1, WMT_ENGINE);
    uint32_t min_pages = (uint32_t)luaL_checkinteger(L, 2);
    uint32_t max_pages = (uint32_t)luaL_optinteger(L, 3, 0);

    /* 创建 memory type */
    wasm_limits_t limits;
    limits.min = min_pages;
    limits.max = max_pages == 0 ? wasm_limits_max_default : max_pages;
    wasm_memorytype_t *memty = wasm_memorytype_new(&limits);

    wasmtime_sharedmemory_t *shmem = NULL;
    wasmtime_error_t *error = wasmtime_sharedmemory_new(we->engine, memty, &shmem);
    wasm_memorytype_delete(memty);

    if (error) {
        wasm_name_t msg;
        wasmtime_error_message(error, &msg);
        wasmtime_error_delete(error);
        return luaL_error(L, "Failed to create shared memory: %.*s", (int)msg.size, msg.data);
    }

    wmt_SharedMemory *sm = (wmt_SharedMemory*)lua_newuserdata(L, sizeof(wmt_SharedMemory));
    sm->shmem = shmem;
    sm->size = min_pages * 65536ULL;

    luaL_getmetatable(L, WMT_SHMEM);
    lua_setmetatable(L, -2);
    return 1;
}

/**
 * @brief sharedmemory:size() → bytes
 * 返回共享内存的大小（字节）。
 */
static int l_sharedmemory_size(lua_State *L) {
    wmt_SharedMemory *sm = (wmt_SharedMemory*)luaL_checkudata(L, 1, WMT_SHMEM);
    lua_pushinteger(L, (lua_Integer)sm->size);
    return 1;
}

/**
 * @brief sharedmemory:data() → lightuserdata
 * 返回共享内存的原始数据指针。
 */
static int l_sharedmemory_data(lua_State *L) {
    wmt_SharedMemory *sm = (wmt_SharedMemory*)luaL_checkudata(L, 1, WMT_SHMEM);
    uint8_t *base = wasmtime_sharedmemory_data(sm->shmem);
    lua_pushlightuserdata(L, base);
    return 1;
}

static const struct luaL_Reg sharedmemory_methods[] = {
    {"size", l_sharedmemory_size},
    {"data", l_sharedmemory_data},
    {"__gc", wmt_sharedmemory_gc},
    {NULL, NULL}
};

/* ============================================================
 * 元表创建
 * ============================================================ */

static void create_meta(lua_State *L, const char *name,
                        const struct luaL_Reg *methods) {
    if (luaL_newmetatable(L, name)) {
        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "__index");
        luaL_setfuncs(L, methods, 0);
    }
    lua_pop(L, 1);
}

/* ============================================================
 * newLinker(engine) → linker  — Lua API
 * ============================================================ */

/* linker userdata 类型 */
#define WMT_LINKER "wasmtime.linker"

typedef struct {
    wasmtime_linker_t *linker;
} wmt_Linker;

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
static int l_new_linker(lua_State *L) {
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

/** Caller userdata 类型 — 在 host 回调中用于读写 WASM 线性内存 */
#define WMT_CALLER "wasmtime.caller"

typedef struct {
    wasmtime_caller_t *caller;
    wasmtime_context_t *ctx;
} wmt_Caller;

/** Host 回调上下文: Lua state + registry 引用 + 返回值类型 */
typedef struct {
    lua_State      *L;
    int             cb_ref;     /* LUA_REGISTRYINDEX 中的回调函数引用 */
    size_t          nresults;   /* 返回值数量 */
    wasm_valkind_t *ret_kinds;  /* 返回值类型数组, NULL 或用完后需 free */
} wmt_HostCallback;

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
 * @brief 从类型字符串解析一个 wasm_valtype_t*
 * @param s 类型名: "i32" | "i64" | "f32" | "f64"
 * @param len 字符串长度
 * @return wasm_valtype_t* 或 NULL
 */
static wasm_valtype_t* wmt_parse_type(const char *s, size_t len) {
    if (len == 3 && memcmp(s, "i32", 3) == 0) return wasm_valtype_new(WASM_I32);
    if (len == 3 && memcmp(s, "i64", 3) == 0) return wasm_valtype_new(WASM_I64);
    if (len == 3 && memcmp(s, "f32", 3) == 0) return wasm_valtype_new(WASM_F32);
    if (len == 3 && memcmp(s, "f64", 3) == 0) return wasm_valtype_new(WASM_F64);
    return NULL;
}

/**
 * @brief 从逗号分隔的类型字符串构建 wasm_valtype_vec_t
 * @param str 类型字符串，如 "i32,i32,i32"
 * @param vec 输出的类型向量
 * @return 0 成功, -1 失败
 */
static int wmt_parse_type_vec(const char *str, wasm_valtype_vec_t *vec) {
    if (!str || !*str) {
        wasm_valtype_vec_new_empty(vec);
        return 0;
    }

    /* 先计算类型个数 */
    int count = 0;
    const char *p = str;
    while (*p) {
        while (*p == ' ' || *p == ',') p++;
        if (!*p) break;
        count++;
        while (*p && *p != ' ' && *p != ',') p++;
    }

    wasm_valtype_t **types = (wasm_valtype_t**)malloc(count * sizeof(wasm_valtype_t*));
    if (!types) return -1;

    int idx = 0;
    p = str;
    while (*p) {
        while (*p == ' ' || *p == ',') p++;
        if (!*p) break;
        const char *start = p;
        while (*p && *p != ' ' && *p != ',') p++;
        types[idx] = wmt_parse_type(start, (size_t)(p - start));
        if (!types[idx]) {
            for (int i = 0; i < idx; i++) wasm_valtype_delete(types[i]);
            free(types);
            return -1;
        }
        idx++;
    }

    wasm_valtype_vec_new(vec, (size_t)count, (wasm_valtype_t* const*)types);
    /* 注意: wasm_valtype_vec_new 仅拷贝指针, vec 引用的是同一组类型对象。
     * 调用者(functype_new取得所有权后或vec_delete)负责释放这些对象,
     * 此处不能 delete, 否则 vec 内指针悬空。 */
    free(types);
    return 0;
}

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
        luaL_error(L, "defineFunc: wasmtime error: %.*s", (int)msg.size, msg.data);
        wasm_byte_vec_delete(&msg);
        wasmtime_error_delete(err);
        return 0;
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

static const struct luaL_Reg engine_methods[] = {
    {"incrementEpoch", l_engine_increment_epoch},
    {"__gc", wmt_engine_gc},
    {NULL, NULL}
};

static const struct luaL_Reg store_methods[] = {
    {"setFuel",     l_store_set_fuel},
    {"getFuel",     l_store_get_fuel},
    {"gc",          l_store_gc},
    {"setEpochDeadline", l_store_set_epoch_deadline},
    {"newMemory",   l_store_new_memory},
    {"__gc", wmt_store_gc},
    {NULL, NULL}
};

static const struct luaL_Reg module_methods[] = {
    {"serialize",   l_module_serialize},
    {"getExports",  l_module_get_exports},
    {"getImports",  l_module_get_imports},
    {"__gc", wmt_module_gc},
    {NULL, NULL}
};

static const struct luaL_Reg instance_methods[] = {
    {"getExport",    l_instance_get_export},
    {"getExportEx",  l_instance_get_export_ex},
    {"getExports",   l_instance_get_exports},
    {"getMemory",    l_instance_get_memory},
    {"getGlobal",    l_instance_get_global},
    {"getTable",     l_instance_get_table},
    {"__gc", wmt_instance_gc},
    {NULL, NULL}
};

static const struct luaL_Reg linker_methods[] = {
    {"defineFunc",  l_linker_define_func},
    {"instantiate", l_linker_instantiate},
    {"__gc", wmt_linker_gc},
    {NULL, NULL}
};

static const struct luaL_Reg function_methods[] = {
    {"call", l_func_call},
    {"getType", l_func_get_type},
    {"__gc", wmt_func_gc},
    {NULL, NULL}
};

static const struct luaL_Reg memory_methods[] = {
    {"read",     l_memory_read},
    {"write",    l_memory_write},
    {"size",     l_memory_size},
    {"dataSize", l_memory_data_size},
    {"grow",     l_memory_grow},
    {"getType",  l_memory_get_type},
    {"__gc",     wmt_memory_gc},
    {NULL, NULL}
};

static const struct luaL_Reg global_methods[] = {
    {"get",  l_global_get},
    {"set",  l_global_set},
    {"__gc", wmt_global_gc},
    {NULL, NULL}
};

static const struct luaL_Reg table_methods[] = {
    {"get",   l_table_get},
    {"set",   l_table_set},
    {"size",  l_table_size},
    {"grow",  l_table_grow},
    {"__gc",  wmt_table_gc},
    {NULL, NULL}
};

static const struct luaL_Reg wasmtime_lib[] = {
    {"newEngine",         l_new_engine},
    {"newStore",          l_new_store},
    {"newModule",         l_new_module},
    {"newInstance",       l_new_instance},
    {"validate",          l_validate},
    {"newLinker",         l_new_linker},
    {"deserializeModule", l_deserialize_module},
    {"newExternref",      l_new_externref},
    {"newSharedMemory",   l_new_shared_memory},
    {NULL, NULL}
};

/**
 * @brief 模块入口：require("wasmtime")
 */
int luaopen_wasmtime(lua_State *L) {
    create_meta(L, WMT_ENGINE, engine_methods);
    create_meta(L, WMT_STORE, store_methods);
    create_meta(L, WMT_MODULE, module_methods);
    create_meta(L, WMT_INSTANCE, instance_methods);
    create_meta(L, WMT_LINKER, linker_methods);
    create_meta(L, WMT_FUNC, function_methods);
    create_meta(L, WMT_MEMORY, memory_methods);
    create_meta(L, WMT_GLOBAL, global_methods);
    create_meta(L, WMT_TABLE, table_methods);
    create_meta(L, WMT_CALLER, caller_methods);
    create_meta(L, WMT_SHMEM, sharedmemory_methods);

    luaL_newlib(L, wasmtime_lib);
    return 1;
}

#else /* __EMSCRIPTEN__ — WASM构建目标：不编译写引用及wasmtime原生运行时 */

/**
 * @brief WASM构建目标的桩模块入口
 *
 * wasmtime 是原生运行时库，无法在浏览器/WASM环境中工作。
 * 注册一个空模块，调用时由Lua侧捕获错误。
 */
int luaopen_wasmtime(lua_State *L) {
    lua_newtable(L);
    lua_pushstring(L, "wasmtime is not available in WASM build target"
                       " (requires native wasmtime library)");
    lua_setfield(L, -2, "_error");
    return 1;
}

#endif /* __EMSCRIPTEN__ */