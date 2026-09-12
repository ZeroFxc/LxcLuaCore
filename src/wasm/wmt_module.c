/**
 * @file wmt_module.c
 * @brief wasmtime Lua 绑定 —— Module：编译、验证、序列化/反序列化、导入导出类型
 *
 * 模块化拆分自 lwasmtime.c（wasmtime v48.0.1 C API）。
 * 共享类型与函数声明见 lwasmtime.h。
 */
#include "lwasmtime.h"

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
int l_new_module(lua_State *L) {
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
int l_validate(lua_State *L) {
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
int l_deserialize_module(lua_State *L) {
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

/**
 * @brief wasmtime.wat2wasm(wat_text) → wasm_bytes | nil, err
 *
 * 将 WAT（WebAssembly Text）文本编译为 WASM 二进制。
 * 依赖 wasmtime 的 WAT 特性（v48 预编译库已开启）。
 *
 * @param wat_text WAT 文本（string）
 * @return wasm 二进制 string；失败时返回 nil, errmsg
 */
int l_wat2wasm(lua_State *L) {
    size_t len;
    const char *wat = luaL_checklstring(L, 1, &len);

    wasm_byte_vec_t bytes;
    wasmtime_error_t *err = wasmtime_wat2wasm(wat, len, &bytes);
    if (err) {
        wasm_name_t msg;
        wasmtime_error_message(err, &msg);
        lua_pushnil(L);
        lua_pushlstring(L, msg.data, msg.size);
        wasm_byte_vec_delete(&msg);
        wasmtime_error_delete(err);
        return 2;
    }

    lua_pushlstring(L, bytes.data, bytes.size);
    wasm_byte_vec_delete(&bytes);
    return 1;
}

/* ============================================================
 * 方法表
 * ============================================================ */

const struct luaL_Reg wmt_module_methods[] = {
    {"serialize",   l_module_serialize},
    {"getExports",  l_module_get_exports},
    {"getImports",  l_module_get_imports},
    {"__gc", wmt_module_gc},
    {NULL, NULL}
};
