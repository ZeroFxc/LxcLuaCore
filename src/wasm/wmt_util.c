/**
 * @file wmt_util.c
 * @brief wasmtime Lua 绑定 —— 共享辅助：Lua↔wasmtime 值转换、类型字符串解析、元表创建
 *
 * 模块化拆分自 lwasmtime.c（wasmtime v48.0.1 C API）。
 * 共享类型与函数声明见 lwasmtime.h。
 */
#include "lwasmtime.h"

int lua_to_wasmtime_val(lua_State *L, int idx, wasmtime_val_t *val) {
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
int lua_to_wasmtime_val_typed(lua_State *L, int idx, wasmtime_val_t *val, wasm_valkind_t expected) {
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
void wasmtime_val_to_lua(lua_State *L, const wasmtime_val_t *val) {
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
 * 元表创建
 * ============================================================ */

void create_meta(lua_State *L, const char *name,
                        const struct luaL_Reg *methods) {
    if (luaL_newmetatable(L, name)) {
        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "__index");
        luaL_setfuncs(L, methods, 0);
    }
    lua_pop(L, 1);
}

/* ============================================================
 * 类型字符串解析（供 linker:defineFunc 使用）
 * ============================================================ */

/**
 * @brief 从类型字符串解析一个 wasm_valtype_t*
 * @param s 类型名: "i32" | "i64" | "f32" | "f64"
 * @param len 字符串长度
 * @return wasm_valtype_t* 或 NULL
 */
wasm_valtype_t* wmt_parse_type(const char *s, size_t len) {
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
int wmt_parse_type_vec(const char *str, wasm_valtype_vec_t *vec) {
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
