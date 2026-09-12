/**
 * @file wmt_value.c
 * @brief wasmtime Lua 绑定 —— 值类对象：Memory / Global / Table / ExternRef / SharedMemory
 *
 * 模块化拆分自 lwasmtime.c（wasmtime v48.0.1 C API）。
 * 共享类型与函数声明见 lwasmtime.h。
 */
#include "lwasmtime.h"

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
int l_new_externref(lua_State *L) {
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
/* ============================================================
 * SharedMemory  — 线程安全的 WASM 共享内存
 * ============================================================ */


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
int l_new_shared_memory(lua_State *L) {
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
 * 方法表
 * ============================================================ */

const struct luaL_Reg wmt_memory_methods[] = {
    {"read",     l_memory_read},
    {"write",    l_memory_write},
    {"size",     l_memory_size},
    {"dataSize", l_memory_data_size},
    {"grow",     l_memory_grow},
    {"getType",  l_memory_get_type},
    {"__gc",     wmt_memory_gc},
    {NULL, NULL}
};

const struct luaL_Reg wmt_global_methods[] = {
    {"get",  l_global_get},
    {"set",  l_global_set},
    {"__gc", wmt_global_gc},
    {NULL, NULL}
};

const struct luaL_Reg wmt_table_methods[] = {
    {"get",   l_table_get},
    {"set",   l_table_set},
    {"size",  l_table_size},
    {"grow",  l_table_grow},
    {"__gc",  wmt_table_gc},
    {NULL, NULL}
};
const struct luaL_Reg wmt_sharedmemory_methods[] = {
    {"size", l_sharedmemory_size},
    {"data", l_sharedmemory_data},
    {"__gc", wmt_sharedmemory_gc},
    {NULL, NULL}
};
