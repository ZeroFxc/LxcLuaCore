#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "quickjs/quickjs.h"

/* Metatable names */
#define QJS_RUNTIME_MT "qjs_runtime"
#define QJS_CONTEXT_MT "qjs_context"
#define QJS_VALUE_MT "qjs_value"

/* Wrapper structures */
typedef struct {
    JSRuntime *rt;
} qjs_runtime_t;

typedef struct {
    JSContext *ctx;
} qjs_context_t;

typedef struct {
    JSContext *ctx;
    JSValue val;
} qjs_value_t;

/* --- JSValue wrapper methods --- */

static int l_value_gc(lua_State *L) {
    qjs_value_t *v = (qjs_value_t *)luaL_checkudata(L, 1, QJS_VALUE_MT);
    if (v->ctx) {
        JS_FreeValue(v->ctx, v->val);
    }
    v->ctx = NULL;
    return 0;
}

static int l_value_tostring(lua_State *L) {
    qjs_value_t *v = (qjs_value_t *)luaL_checkudata(L, 1, QJS_VALUE_MT);
    if (!v->ctx) {
        lua_pushstring(L, "freed JSValue");
        return 1;
    }
    const char *str = JS_ToCString(v->ctx, v->val);
    if (str) {
        lua_pushstring(L, str);
        JS_FreeCString(v->ctx, str);
    } else {
        lua_pushstring(L, "[JS exception/conversion error]");
    }
    return 1;
}

static int l_value_tonumber(lua_State *L) {
    qjs_value_t *v = (qjs_value_t *)luaL_checkudata(L, 1, QJS_VALUE_MT);
    if (!v->ctx) return 0;

    double d;
    if (JS_ToFloat64(v->ctx, &d, v->val) == 0) {
        lua_pushnumber(L, d);
        return 1;
    }
    return 0;
}

static const luaL_Reg qjs_value_methods[] = {
    {"__gc", l_value_gc},
    {"__tostring", l_value_tostring},
    {"tonumber", l_value_tonumber},
    {"tostring", l_value_tostring},
    {NULL, NULL}
};

/* --- JSContext wrapper methods --- */

static int push_js_value(lua_State *L, int ctx_idx, JSContext *ctx, JSValue val) {
    qjs_value_t *v = (qjs_value_t *)lua_newuserdata(L, sizeof(qjs_value_t));
    v->ctx = ctx;
    v->val = val;
    luaL_getmetatable(L, QJS_VALUE_MT);
    lua_setmetatable(L, -2);
    /* Link value to context to prevent GC of context */
    lua_pushvalue(L, ctx_idx);
    lua_setuservalue(L, -2);
    return 1;
}

static int l_ctx_gc(lua_State *L) {
    qjs_context_t *c = (qjs_context_t *)luaL_checkudata(L, 1, QJS_CONTEXT_MT);
    if (c->ctx) {
        JS_FreeContext(c->ctx);
        c->ctx = NULL;
    }
    return 0;
}

static int l_ctx_eval(lua_State *L) {
    qjs_context_t *c = (qjs_context_t *)luaL_checkudata(L, 1, QJS_CONTEXT_MT);
    size_t len;
    const char *script = luaL_checklstring(L, 2, &len);
    const char *filename = luaL_optstring(L, 3, "<eval>");

    JSValue val = JS_Eval(c->ctx, script, len, filename, JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(val)) {
        JSValue exception_val = JS_GetException(c->ctx);
        const char *err_str = JS_ToCString(c->ctx, exception_val);
        if (err_str) {
            lua_pushstring(L, err_str);
            JS_FreeCString(c->ctx, err_str);
        } else {
            lua_pushstring(L, "Unknown JavaScript Error");
        }
        JS_FreeValue(c->ctx, exception_val);
        JS_FreeValue(c->ctx, val);
        return lua_error(L);
    }

    return push_js_value(L, 1, c->ctx, val);
}

static const luaL_Reg qjs_context_methods[] = {
    {"__gc", l_ctx_gc},
    {"eval", l_ctx_eval},
    {NULL, NULL}
};

/* --- JSRuntime wrapper methods --- */

static int l_rt_gc(lua_State *L) {
    qjs_runtime_t *r = (qjs_runtime_t *)luaL_checkudata(L, 1, QJS_RUNTIME_MT);
    if (r->rt) {
        JS_FreeRuntime(r->rt);
        r->rt = NULL;
    }
    return 0;
}

static int l_rt_new_context(lua_State *L) {
    qjs_runtime_t *r = (qjs_runtime_t *)luaL_checkudata(L, 1, QJS_RUNTIME_MT);
    JSContext *ctx = JS_NewContext(r->rt);
    if (!ctx) {
        return luaL_error(L, "Failed to create JS context");
    }
    qjs_context_t *c = (qjs_context_t *)lua_newuserdata(L, sizeof(qjs_context_t));
    c->ctx = ctx;
    luaL_getmetatable(L, QJS_CONTEXT_MT);
    lua_setmetatable(L, -2);
    /* Link context to runtime to prevent GC of runtime */
    lua_pushvalue(L, 1);
    lua_setuservalue(L, -2);
    return 1;
}

static const luaL_Reg qjs_runtime_methods[] = {
    {"__gc", l_rt_gc},
    {"new_context", l_rt_new_context},
    {NULL, NULL}
};

/* --- Module functions --- */

static int l_new_runtime(lua_State *L) {
    JSRuntime *rt = JS_NewRuntime();
    if (!rt) {
        return luaL_error(L, "Failed to create JS runtime");
    }
    qjs_runtime_t *r = (qjs_runtime_t *)lua_newuserdata(L, sizeof(qjs_runtime_t));
    r->rt = rt;
    luaL_getmetatable(L, QJS_RUNTIME_MT);
    lua_setmetatable(L, -2);
    return 1;
}

static const luaL_Reg qjs_funcs[] = {
    {"new_runtime", l_new_runtime},
    {NULL, NULL}
};

/* Create metatable and populate it */
static void create_metatable(lua_State *L, const char *tname, const luaL_Reg *methods) {
    luaL_newmetatable(L, tname);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    luaL_setfuncs(L, methods, 0);
    lua_pop(L, 1);
}

int luaopen_quickjs(lua_State *L) {
    create_metatable(L, QJS_RUNTIME_MT, qjs_runtime_methods);
    create_metatable(L, QJS_CONTEXT_MT, qjs_context_methods);
    create_metatable(L, QJS_VALUE_MT, qjs_value_methods);

    luaL_newlib(L, qjs_funcs);
    return 1;
}
