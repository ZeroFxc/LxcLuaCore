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
 *
 * 模块化说明：绑定实现拆分至 wmt_*.c（见 lwasmtime.h），本文件仅保留
 * lib 方法表与 luaopen 入口。
 */

#include "lwasmtime.h"

#ifndef __EMSCRIPTEN__

/* ============================================================
 * lib 级方法表
 * ============================================================ */

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
    {"wat2wasm",          l_wat2wasm},
    {"newWasi",           l_new_wasi},
    {"newTrap",           l_new_trap},
    {"newTrapCode",       l_new_trap_code},
    {"newComponent",      l_new_component},
    {"newComponentLinker", l_new_component_linker},
    {NULL, NULL}
};

/**
 * @brief 模块入口：require("wasmtime")
 */
int luaopen_wasmtime(lua_State *L) {
    create_meta(L, WMT_ENGINE, wmt_engine_methods);
    create_meta(L, WMT_STORE, wmt_store_methods);
    create_meta(L, WMT_MODULE, wmt_module_methods);
    create_meta(L, WMT_INSTANCE, wmt_instance_methods);
    create_meta(L, WMT_LINKER, wmt_linker_methods);
    create_meta(L, WMT_FUNC, wmt_function_methods);
    create_meta(L, WMT_MEMORY, wmt_memory_methods);
    create_meta(L, WMT_GLOBAL, wmt_global_methods);
    create_meta(L, WMT_TABLE, wmt_table_methods);
    create_meta(L, WMT_CALLER, wmt_caller_methods);
    create_meta(L, WMT_SHMEM, wmt_sharedmemory_methods);
    create_meta(L, WMT_WASI, wmt_wasi_methods);
    create_meta(L, WMT_TRAP, wmt_trap_methods);
    create_meta(L, WMT_COMPONENT, wmt_component_methods);
    create_meta(L, WMT_COMPONENT_LINKER, wmt_component_linker_methods);
    create_meta(L, WMT_COMPONENT_INSTANCE, wmt_component_instance_methods);
    create_meta(L, WMT_COMPONENT_FUNC, wmt_component_func_methods);
    create_meta(L, WMT_FUTURE, wmt_future_methods);
    create_meta(L, WMT_CFUTURE, wmt_component_future_methods);

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
