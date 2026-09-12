/**
 * @file wmt_instance.c
 * @brief wasmtime Lua 绑定 —— Instance 与 Function：实例导出、函数调用与签名
 *
 * 模块化拆分自 lwasmtime.c（wasmtime v48.0.1 C API）。
 * 共享类型与函数声明见 lwasmtime.h。
 */
#include "lwasmtime.h"

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
int l_new_instance(lua_State *L) {
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
        free(out);
        if (trap) {
            /* 返回 nil, trap, message
             * trap userdata 接管 wasm_trap_t 的所有权 */
            lua_pushnil(L);
            wmt_Trap *wt = (wmt_Trap*)lua_newuserdata(L, sizeof(wmt_Trap));
            wt->trap = trap;
            luaL_getmetatable(L, WMT_TRAP);
            lua_setmetatable(L, -2);

            wasm_name_t msg = {0};
            wasm_trap_message(trap, &msg);
            size_t msz = msg.size;
            while (msz > 0 && msg.data[msz - 1] == '\0') msz--;
            lua_pushlstring(L, msg.data, msz);
            wasm_byte_vec_delete(&msg);
            if (error) wasmtime_error_delete(error);
            return 3;
        }
        /* 非 trap 错误：nil, nil, message */
        wasm_name_t msg = {0};
        wasmtime_error_message(error, &msg);
        lua_pushnil(L);
        lua_pushnil(L);
        lua_pushlstring(L, msg.data, msg.size);
        wasm_byte_vec_delete(&msg);
        wasmtime_error_delete(error);
        return 3;
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
 * @brief func:callWithFuel(fuel, ...) → 结果值
 * 以燃料上限调用 wasm 函数：调用前 set_fuel(fuel)，wasm 消耗完燃料即抛
 * OUT_OF_FUEL trap（nil, trap, msg），防止死循环 wasm 卡死宿主。
 * 调用后恢复燃料为无上限。需要引擎开启 fuel（newEngine{fuel=true}）。
 */
int l_func_call_with_fuel(lua_State *L) {
    wmt_Function *wf = (wmt_Function*)luaL_checkudata(L, 1, WMT_FUNC);
    uint64_t fuel = (uint64_t)luaL_checkinteger(L, 2);
    int nargs = lua_gettop(L) - 2;

    wasmtime_context_t *ctx = wasmtime_store_context(wf->store);

    /* 设置燃料上限 */
    wasmtime_error_t *ferr = wasmtime_context_set_fuel(ctx, fuel);
    if (ferr) {
        wasm_name_t msg;
        wasmtime_error_message(ferr, &msg);
        lua_pushnil(L);
        lua_pushlstring(L, msg.data, msg.size);
        wasm_byte_vec_delete(&msg);
        wasmtime_error_delete(ferr);
        return 2;
    }

    wasm_functype_t *ftype = wasmtime_func_type(ctx, &wf->func);
    if (!ftype) {
        wasmtime_context_set_fuel(ctx, UINT64_MAX);
        return luaL_error(L, "Failed to get function type");
    }

    const wasm_valtype_vec_t *params = wasm_functype_params(ftype);
    const wasm_valtype_vec_t *results = wasm_functype_results(ftype);
    size_t expected_nargs = params->size;

    wasmtime_val_t *args = NULL;
    if (expected_nargs > 0) {
        args = (wasmtime_val_t*)malloc(expected_nargs * sizeof(wasmtime_val_t));
        if (!args) {
            wasm_functype_delete(ftype);
            wasmtime_context_set_fuel(ctx, UINT64_MAX);
            return luaL_error(L, "out of memory");
        }
        for (size_t i = 0; i < expected_nargs; i++) {
            if ((int)i < nargs) {
                lua_to_wasmtime_val(L, i + 3, &args[i]);
            } else {
                args[i].kind = WASMTIME_I32;
                args[i].of.i32 = 0;
            }
        }
    }

    size_t nresults = results->size;
    if (nresults == 0) nresults = 1;
    wasmtime_val_t *out = (wasmtime_val_t*)malloc(nresults * sizeof(wasmtime_val_t));
    if (!out) {
        free(args);
        wasm_functype_delete(ftype);
        wasmtime_context_set_fuel(ctx, UINT64_MAX);
        return luaL_error(L, "out of memory");
    }

    wasm_trap_t *trap = NULL;
    int actual_nresults = (int)results->size;

    wasmtime_error_t *error = wasmtime_func_call(
        ctx, &wf->func, args, expected_nargs, out, actual_nresults, &trap);

    /* 无论结果如何，恢复燃料无上限 */
    wasmtime_context_set_fuel(ctx, UINT64_MAX);

    wasm_functype_delete(ftype);
    free(args);

    if (error || trap) {
        free(out);
        if (trap) {
            lua_pushnil(L);
            wmt_Trap *wt = (wmt_Trap*)lua_newuserdata(L, sizeof(wmt_Trap));
            wt->trap = trap;
            luaL_getmetatable(L, WMT_TRAP);
            lua_setmetatable(L, -2);

            wasm_name_t msg = {0};
            wasm_trap_message(trap, &msg);
            size_t msz = msg.size;
            while (msz > 0 && msg.data[msz - 1] == '\0') msz--;
            lua_pushlstring(L, msg.data, msz);
            wasm_byte_vec_delete(&msg);
            if (error) wasmtime_error_delete(error);
            return 3;
        }
        wasm_name_t msg = {0};
        wasmtime_error_message(error, &msg);
        lua_pushnil(L);
        lua_pushnil(L);
        lua_pushlstring(L, msg.data, msg.size);
        wasm_byte_vec_delete(&msg);
        wasmtime_error_delete(error);
        return 3;
    }

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


/* ============================================================
 * Trap 对象 — 调用失败的结构化诊断
 * ============================================================ */

/**
 * @brief trap code 枚举 → Lua 可读字符串
 */
static const char* wmt_trap_code_name(wasmtime_trap_code_t code) {
    switch (code) {
        case WASMTIME_TRAP_CODE_STACK_OVERFLOW:             return "stack_overflow";
        case WASMTIME_TRAP_CODE_MEMORY_OUT_OF_BOUNDS:       return "memory_out_of_bounds";
        case WASMTIME_TRAP_CODE_HEAP_MISALIGNED:            return "heap_misaligned";
        case WASMTIME_TRAP_CODE_TABLE_OUT_OF_BOUNDS:        return "table_out_of_bounds";
        case WASMTIME_TRAP_CODE_INDIRECT_CALL_TO_NULL:      return "indirect_call_to_null";
        case WASMTIME_TRAP_CODE_BAD_SIGNATURE:              return "bad_signature";
        case WASMTIME_TRAP_CODE_INTEGER_OVERFLOW:           return "integer_overflow";
        case WASMTIME_TRAP_CODE_INTEGER_DIVISION_BY_ZERO:   return "integer_division_by_zero";
        case WASMTIME_TRAP_CODE_BAD_CONVERSION_TO_INTEGER:  return "bad_conversion_to_integer";
        case WASMTIME_TRAP_CODE_UNREACHABLE_CODE_REACHED:   return "unreachable";
        case WASMTIME_TRAP_CODE_INTERRUPT:                  return "interrupt";
        case WASMTIME_TRAP_CODE_OUT_OF_FUEL:                return "out_of_fuel";
        case WASMTIME_TRAP_CODE_NULL_REFERENCE:             return "null_reference";
        case WASMTIME_TRAP_CODE_ARRAY_OUT_OF_BOUNDS:        return "array_out_of_bounds";
        case WASMTIME_TRAP_CODE_ALLOCATION_TOO_LARGE:       return "allocation_too_large";
        case WASMTIME_TRAP_CODE_CAST_FAILURE:               return "cast_failure";
        default:                                            return "unknown";
    }
}

/**
 * @brief 字符串 trap code 名 → 枚举
 * @return 匹配返回 0，否则 -1
 */
static int wmt_trap_code_from_name(const char *name, wasmtime_trap_code_t *out) {
    struct { const char *n; wasmtime_trap_code_t c; } map[] = {
        {"stack_overflow",            WASMTIME_TRAP_CODE_STACK_OVERFLOW},
        {"memory_out_of_bounds",      WASMTIME_TRAP_CODE_MEMORY_OUT_OF_BOUNDS},
        {"heap_misaligned",           WASMTIME_TRAP_CODE_HEAP_MISALIGNED},
        {"table_out_of_bounds",       WASMTIME_TRAP_CODE_TABLE_OUT_OF_BOUNDS},
        {"indirect_call_to_null",     WASMTIME_TRAP_CODE_INDIRECT_CALL_TO_NULL},
        {"bad_signature",             WASMTIME_TRAP_CODE_BAD_SIGNATURE},
        {"integer_overflow",          WASMTIME_TRAP_CODE_INTEGER_OVERFLOW},
        {"integer_division_by_zero",  WASMTIME_TRAP_CODE_INTEGER_DIVISION_BY_ZERO},
        {"bad_conversion_to_integer", WASMTIME_TRAP_CODE_BAD_CONVERSION_TO_INTEGER},
        {"unreachable",               WASMTIME_TRAP_CODE_UNREACHABLE_CODE_REACHED},
        {"interrupt",                 WASMTIME_TRAP_CODE_INTERRUPT},
        {"out_of_fuel",               WASMTIME_TRAP_CODE_OUT_OF_FUEL},
        {"null_reference",            WASMTIME_TRAP_CODE_NULL_REFERENCE},
        {"array_out_of_bounds",       WASMTIME_TRAP_CODE_ARRAY_OUT_OF_BOUNDS},
        {"allocation_too_large",      WASMTIME_TRAP_CODE_ALLOCATION_TOO_LARGE},
        {"cast_failure",              WASMTIME_TRAP_CODE_CAST_FAILURE},
    };
    for (size_t i = 0; i < sizeof(map)/sizeof(map[0]); i++) {
        if (strcmp(name, map[i].n) == 0) { *out = map[i].c; return 0; }
    }
    return -1;
}

/**
 * @brief 构造一个 trap userdata（包装已持有的 wasm_trap_t*）
 */
static int wmt_trap_push(lua_State *L, wasm_trap_t *trap) {
    wmt_Trap *wt = (wmt_Trap*)lua_newuserdata(L, sizeof(wmt_Trap));
    wt->trap = trap;
    luaL_getmetatable(L, WMT_TRAP);
    lua_setmetatable(L, -2);
    return 1;
}

/**
 * @brief wasmtime.newTrap(message) → trap
 * 用自定义消息创建一个 trap 对象。
 */
int l_new_trap(lua_State *L) {
    size_t len;
    const char *msg = luaL_checklstring(L, 1, &len);
    wasm_trap_t *trap = wasmtime_trap_new(msg, len);
    if (!trap) return luaL_error(L, "newTrap: failed to create trap");
    wmt_trap_push(L, trap);
    return 1;
}

/**
 * @brief wasmtime.newTrapCode(code_name) → trap
 * 用指令 trap 代码创建一个 trap 对象，如 "integer_division_by_zero"。
 */
int l_new_trap_code(lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
    wasmtime_trap_code_t code;
    if (wmt_trap_code_from_name(name, &code) != 0) {
        return luaL_error(L, "newTrapCode: unknown trap code: %s", name);
    }
    wasm_trap_t *trap = wasmtime_trap_new_code(code);
    if (!trap) return luaL_error(L, "newTrapCode: failed to create trap");
    wmt_trap_push(L, trap);
    return 1;
}

/**
 * @brief trap:code() → code_name, code_num | nil, nil
 * 若为指令陷阱返回可读名与数值；非指令陷阱（如宿主创建）返回 nil。
 */
static int l_trap_code(lua_State *L) {
    wmt_Trap *wt = (wmt_Trap*)luaL_checkudata(L, 1, WMT_TRAP);
    wasmtime_trap_code_t code;
    if (wasmtime_trap_code(wt->trap, &code)) {
        lua_pushstring(L, wmt_trap_code_name(code));
        lua_pushinteger(L, (lua_Integer)code);
        return 2;
    }
    lua_pushnil(L);
    lua_pushnil(L);
    return 2;
}

/**
 * @brief trap:message() → string
 * 去除 wasmtime 消息 vec 尾部的 null 字节。
 */
static int l_trap_message(lua_State *L) {
    wmt_Trap *wt = (wmt_Trap*)luaL_checkudata(L, 1, WMT_TRAP);
    wasm_name_t msg;
    wasm_trap_message(wt->trap, &msg);
    size_t sz = msg.size;
    while (sz > 0 && msg.data[sz - 1] == '\0') sz--;
    lua_pushlstring(L, msg.data, sz);
    wasm_byte_vec_delete(&msg);
    return 1;
}

/**
 * @brief trap:frames() → { {module=, func=, funcIndex=, funcOffset=, moduleOffset=}, ... }
 * 返回 trap 调用栈帧数组（由内到外）。
 * func/module 名为 wasmtime 扩展；如无符号名则该字段为 nil。
 */
static int l_trap_frames(lua_State *L) {
    wmt_Trap *wt = (wmt_Trap*)luaL_checkudata(L, 1, WMT_TRAP);

    wasm_frame_vec_t frames;
    wasm_trap_trace(wt->trap, &frames);

    lua_createtable(L, (int)frames.size, 0);
    for (size_t i = 0; i < frames.size; i++) {
        wasm_frame_t *fr = frames.data[i];
        lua_createtable(L, 0, 5);

        const wasm_name_t *fn = wasmtime_frame_func_name(fr);
        if (fn && fn->data && fn->size > 0) {
            lua_pushlstring(L, fn->data, fn->size);
            lua_setfield(L, -2, "func");
        }
        const wasm_name_t *mn = wasmtime_frame_module_name(fr);
        if (mn && mn->data && mn->size > 0) {
            lua_pushlstring(L, mn->data, mn->size);
            lua_setfield(L, -2, "module");
        }
        lua_pushinteger(L, (lua_Integer)wasm_frame_func_index(fr));
        lua_setfield(L, -2, "funcIndex");
        lua_pushinteger(L, (lua_Integer)wasm_frame_func_offset(fr));
        lua_setfield(L, -2, "funcOffset");
        lua_pushinteger(L, (lua_Integer)wasm_frame_module_offset(fr));
        lua_setfield(L, -2, "moduleOffset");

        lua_rawseti(L, -2, (lua_Integer)i + 1);
    }
    wasm_frame_vec_delete(&frames);
    return 1;
}

/**
 * @brief trap __gc — 释放 wasm_trap_t
 */
static int wmt_trap_gc(lua_State *L) {
    wmt_Trap *wt = (wmt_Trap*)luaL_checkudata(L, 1, WMT_TRAP);
    if (wt->trap) {
        wasm_trap_delete(wt->trap);
        wt->trap = NULL;
    }
    return 0;
}

const struct luaL_Reg wmt_trap_methods[] = {
    {"code",    l_trap_code},
    {"message", l_trap_message},
    {"frames",  l_trap_frames},
    {"__gc",    wmt_trap_gc},
    {NULL, NULL}
};

/* ============================================================
 * 方法表
 * ============================================================ */

const struct luaL_Reg wmt_instance_methods[] = {
    {"getExport",    l_instance_get_export},
    {"getExportEx",  l_instance_get_export_ex},
    {"getExports",   l_instance_get_exports},
    {"getMemory",    l_instance_get_memory},
    {"getGlobal",    l_instance_get_global},
    {"getTable",     l_instance_get_table},
    {"__gc", wmt_instance_gc},
    {NULL, NULL}
};
const struct luaL_Reg wmt_function_methods[] = {
    {"call", l_func_call},
    {"callWithFuel", l_func_call_with_fuel},
    {"callAsync", l_func_call_async},
    {"callAsyncP", l_func_call_async_promise},
    {"getType", l_func_get_type},
    {"__gc", wmt_func_gc},
    {NULL, NULL}
};
