/**
 * @file wmt_engine.c
 * @brief wasmtime Lua 绑定 —— Engine 与 Store：引擎配置/GC/epoch，store 燃料/epoch/内存
 *
 * 模块化拆分自 lwasmtime.c（wasmtime v48.0.1 C API）。
 * 共享类型与函数声明见 lwasmtime.h。
 */
#include "lwasmtime.h"

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
 *   cache            (string, 默认无)   编译缓存配置文件路径（wasmtime_config_cache_config_load）
 */
int l_new_engine(lua_State *L) {
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
    const char *cache_config = NULL;

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

        lua_getfield(L, 1, "cache");
        if (lua_isstring(L, -1)) cache_config = lua_tostring(L, -1);
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

    /* 编译缓存：加载缓存配置文件（wasmtime 会自动读写编译缓存） */
    if (cache_config && *cache_config) {
        wasmtime_error_t *cerr = wasmtime_config_cache_config_load(config, cache_config);
        if (cerr) {
            wasm_name_t msg;
            wasmtime_error_message(cerr, &msg);
            char buf[512];
            int n = snprintf(buf, sizeof(buf), "newEngine: cache config load failed: %.*s",
                             (int)msg.size, msg.data);
            wasm_byte_vec_delete(&msg);
            wasmtime_error_delete(cerr);
            wasm_config_delete(config);
            return luaL_error(L, "%s", buf);
        }
    }

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
int l_new_store(lua_State *L) {
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
 * store:setWasi(wasi) — 应用 WASI 配置到 store 上下文
 * ============================================================ */

/**
 * @brief store:setWasi(wasi) → store
 * 将 newWasi 创建的配置应用到本 store（wasmtime_context_set_wasi）。
 * 配置所有权转移给 context，wasi userdata 之后不可再用。
 * @param wasi wasmtime.newWasi{...} 返回的配置对象
 * @return store（self，链式调用）
 */
static int l_store_set_wasi(lua_State *L) {
    wmt_Store *ws = (wmt_Store*)luaL_checkudata(L, 1, WMT_STORE);
    wmt_Wasi  *ww = (wmt_Wasi*)luaL_checkudata(L, 2, WMT_WASI);

    if (!ww->config) {
        return luaL_error(L, "store:setWasi: config already consumed by another store");
    }

    wasmtime_context_t *ctx = wasmtime_store_context(ws->store);
    wasmtime_context_set_wasi(ctx, ww->config);  /* 所有权转移给 context */
    ww->config = NULL;                           /* 防止 __gc double-free */

    lua_pushvalue(L, 1);
    return 1;
}

/* ============================================================
 * 方法表
 * ============================================================ */

const struct luaL_Reg wmt_engine_methods[] = {
    {"incrementEpoch", l_engine_increment_epoch},
    {"__gc", wmt_engine_gc},
    {NULL, NULL}
};

const struct luaL_Reg wmt_store_methods[] = {
    {"setFuel",     l_store_set_fuel},
    {"getFuel",     l_store_get_fuel},
    {"gc",          l_store_gc},
    {"setEpochDeadline", l_store_set_epoch_deadline},
    {"newMemory",   l_store_new_memory},
    {"setWasi",     l_store_set_wasi},
    {"__gc", wmt_store_gc},
    {NULL, NULL}
};
