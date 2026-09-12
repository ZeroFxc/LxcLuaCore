/**
 * @file lwasmtime.h
 * @brief wasmtime Lua 绑定 —— 公共头（模块化拆分）
 *
 * 原单一文件 lwasmtime.c 拆分为：
 *   lwasmtime.c  入口：lib 方法表 + luaopen
 *   wmt_util.c   值转换 / 类型解析 / 元表创建
 *   wmt_engine.c Engine + Store
 *   wmt_module.c Module + validate
 *   wmt_instance.c Instance + Function
 *   wmt_value.c  Memory / Global / Table / ExternRef / SharedMemory
 *   wmt_linker.c Linker / Caller / HostCallback
 *   wmt_wasi.c   WASI preview1 配置
 *   wmt_component.c Component Model（组件）
 *   wmt_async.c  Async：func:callAsync / future / callAsyncP（lpromise）
 *
 * 本头文件集中共享的 userdata 结构、类型宏与跨模块函数声明。
 */
#ifndef LWASMTIME_H
#define LWASMTIME_H

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

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

#ifdef __cplusplus
extern "C" {
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

/* Linker / Caller / HostCallback 类型（原 wmt_linker.c 内定义，上移到公共头） */
#define WMT_LINKER "wasmtime.linker"

typedef struct {
    wasmtime_linker_t *linker;
} wmt_Linker;

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

/* SharedMemory 类型（原 wmt_value.c 内定义，上移到公共头） */
typedef struct {
    wasmtime_sharedmemory_t *shmem;
    uint64_t size;
} wmt_SharedMemory;

/* WASI 配置类型（wmt_wasi.c） */
#define WMT_WASI "wasmtime.wasi"

typedef struct {
    wasi_config_t *config;
} wmt_Wasi;

/* Trap 对象类型（wmt_instance.c，func 调用失败产生） */
#define WMT_TRAP "wasmtime.trap"

typedef struct {
    wasm_trap_t *trap;
} wmt_Trap;

/* Async future 类型（wmt_async.c，func:callAsync 产生） */
#define WMT_FUTURE "wasmtime.future"

typedef struct {
    wasmtime_call_future_t *future;  /* NULL = 已同步完成 */
    wasmtime_val_t *results;         /* wasmtime 写结果到这里（存活到 delete） */
    size_t nresults;
    wasm_trap_t *trap;               /* 完成后的 trap（如有） */
    wasmtime_error_t *error;         /* 完成后的 error（如有） */
    int done;
    int store_ref;                   /* 保护 store 生命周期 */
} wmt_Future;

/* Component 类型（wmt_component.c，component model） */
#define WMT_COMPONENT          "wasmtime.component"
#define WMT_COMPONENT_LINKER   "wasmtime.component_linker"
#define WMT_COMPONENT_INSTANCE "wasmtime.component_instance"
#define WMT_COMPONENT_FUNC     "wasmtime.component_func"

typedef struct {
    wasmtime_component_t *component;
    wasm_engine_t        *engine;   /* 类型查询需要 engine */
    int                   engine_ref;
} wmt_Component;

typedef struct {
    wasmtime_component_linker_t *linker;
    wasm_engine_t               *engine;
    int                          engine_ref;
} wmt_ComponentLinker;

typedef struct {
    wasmtime_component_instance_t instance; /* 纯值，无析构 */
    wasmtime_store_t              *store;
    int                            store_ref;
} wmt_ComponentInstance;

typedef struct {
    wasmtime_component_func_t func;  /* 纯值，无析构 */
    wasmtime_store_t          *store;
    int                        store_ref;
} wmt_ComponentFunc;

/* Component async future（wmt_component.c，comp_func:callAsync 产生） */
#define WMT_CFUTURE "wasmtime.component_future"

typedef struct {
    wasmtime_call_future_t *future;   /* NULL = 已同步完成 */
    wasmtime_component_val_t *args;   /* 存活到 future delete */
    size_t nargs;
    wasmtime_component_val_t *results;/* wasmtime 写结果到这里 */
    size_t nresults;
    wasm_trap_t *trap;
    wasmtime_error_t *error;
    int done;
    int store_ref;
} wmt_ComponentFuture;

/* ============================================================
 * 共享辅助函数（wmt_util.c）
 * ============================================================ */

int  lua_to_wasmtime_val(lua_State *L, int idx, wasmtime_val_t *val);
int  lua_to_wasmtime_val_typed(lua_State *L, int idx, wasmtime_val_t *val, wasm_valkind_t expected);
void wasmtime_val_to_lua(lua_State *L, const wasmtime_val_t *val);
wasm_valtype_t* wmt_parse_type(const char *s, size_t len);
int  wmt_parse_type_vec(const char *str, wasm_valtype_vec_t *vec);
void create_meta(lua_State *L, const char *name, const struct luaL_Reg *methods);

/* ============================================================
 * lib 级入口函数（各模块实现，lwasmtime.c 的 wasmtime_lib 引用）
 * ============================================================ */

int l_new_engine(lua_State *L);
int l_new_store(lua_State *L);
int l_new_module(lua_State *L);
int l_new_instance(lua_State *L);
int l_validate(lua_State *L);
int l_new_linker(lua_State *L);
int l_deserialize_module(lua_State *L);
int l_new_externref(lua_State *L);
int l_new_shared_memory(lua_State *L);
int l_wat2wasm(lua_State *L);
int l_new_wasi(lua_State *L);
int l_new_trap(lua_State *L);
int l_new_trap_code(lua_State *L);
int l_new_component(lua_State *L);
int l_new_component_linker(lua_State *L);
int l_func_call_async(lua_State *L);
int l_func_call_async_promise(lua_State *L);
int l_func_call_with_fuel(lua_State *L);

/* ============================================================
 * 各模块方法表（各模块文件内定义，lwasmtime.c 的 luaopen 引用）
 * ============================================================ */

extern const struct luaL_Reg wmt_engine_methods[];
extern const struct luaL_Reg wmt_store_methods[];
extern const struct luaL_Reg wmt_module_methods[];
extern const struct luaL_Reg wmt_instance_methods[];
extern const struct luaL_Reg wmt_function_methods[];
extern const struct luaL_Reg wmt_linker_methods[];
extern const struct luaL_Reg wmt_memory_methods[];
extern const struct luaL_Reg wmt_global_methods[];
extern const struct luaL_Reg wmt_table_methods[];
extern const struct luaL_Reg wmt_caller_methods[];
extern const struct luaL_Reg wmt_sharedmemory_methods[];
extern const struct luaL_Reg wmt_wasi_methods[];
extern const struct luaL_Reg wmt_trap_methods[];
extern const struct luaL_Reg wmt_component_methods[];
extern const struct luaL_Reg wmt_component_linker_methods[];
extern const struct luaL_Reg wmt_component_instance_methods[];
extern const struct luaL_Reg wmt_component_func_methods[];
extern const struct luaL_Reg wmt_future_methods[];
extern const struct luaL_Reg wmt_component_future_methods[];

#ifdef __cplusplus
}
#endif

#endif /* LWASMTIME_H */
