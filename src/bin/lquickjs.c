#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "quickjs.h"

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

static int push_js_value(lua_State *L, int ctx_idx, JSContext *ctx, JSValue val);
static int js_to_lua(lua_State *L, int ctx_idx, JSContext *ctx, JSValue val);
static JSValue lua_to_js(lua_State *L, JSContext *ctx, int idx);

static JSClassID js_lua_func_class_id = 0;

typedef struct {
    lua_State *L;
    int ref;
    int ctx_idx;
} js_lua_func_t;

static void js_lua_func_finalizer(JSRuntime *rt, JSValue val) {
    js_lua_func_t *f = JS_GetOpaque(val, js_lua_func_class_id);
    if (f) {
        luaL_unref(f->L, LUA_REGISTRYINDEX, f->ref);
        js_free_rt(rt, f);
    }
}

static JSClassDef js_lua_func_class = {
    "LuaFunction",
    .finalizer = js_lua_func_finalizer,
};

static JSValue js_lua_func_call(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic, JSValue *func_data) {
    js_lua_func_t *f = JS_GetOpaque(func_data[0], js_lua_func_class_id);
    if (!f) return JS_EXCEPTION;

    lua_State *L = f->L;
    int top = lua_gettop(L);

    lua_rawgeti(L, LUA_REGISTRYINDEX, f->ref);
    if (!lua_isfunction(L, -1)) {
        lua_settop(L, top);
        return JS_ThrowTypeError(ctx, "not a lua function");
    }

    /* We need to pass arguments */
    for (int i = 0; i < argc; i++) {
        js_to_lua(L, f->ctx_idx, ctx, JS_DupValue(ctx, argv[i]));
    }

    if (lua_pcall(L, argc, 1, 0) != LUA_OK) {
        JSValue err = JS_NewString(ctx, lua_tostring(L, -1));
        lua_settop(L, top);
        return JS_Throw(ctx, err);
    }

    JSValue ret = lua_to_js(L, ctx, -1);
    lua_settop(L, top);
    return ret;
}


static JSValue lua_to_js(lua_State *L, JSContext *ctx, int idx) {
    int type = lua_type(L, idx);
    switch (type) {
        case LUA_TNIL:
            return JS_NULL;
        case LUA_TBOOLEAN:
            return JS_NewBool(ctx, lua_toboolean(L, idx));
        case LUA_TNUMBER:
            return JS_NewFloat64(ctx, lua_tonumber(L, idx));
        case LUA_TSTRING: {
            size_t len;
            const char *str = lua_tolstring(L, idx, &len);
            return JS_NewStringLen(ctx, str, len);
        }
        case LUA_TUSERDATA: {
            qjs_value_t *v = (qjs_value_t *)luaL_testudata(L, idx, QJS_VALUE_MT);
            if (v) {
                return JS_DupValue(ctx, v->val);
            }
            return JS_UNDEFINED;
        }
        case LUA_TFUNCTION: {
            if (js_lua_func_class_id == 0) {
                JS_NewClassID(&js_lua_func_class_id);
                JS_NewClass(JS_GetRuntime(ctx), js_lua_func_class_id, &js_lua_func_class);
            }
            
            js_lua_func_t *f = js_malloc(ctx, sizeof(*f));
            if (!f) return JS_EXCEPTION;
            
            f->L = L;
            lua_pushvalue(L, idx);
            f->ref = luaL_ref(L, LUA_REGISTRYINDEX);
            /* finding ctx_idx is tricky. Assume it's available or set to a sensible default.
               For robust code, we could look it up via the context registry or pass it.
               Here we assume ctx_idx is 1 for the context object */
            f->ctx_idx = 1;

            JSValue obj = JS_NewObjectClass(ctx, js_lua_func_class_id);
            if (JS_IsException(obj)) {
                js_free(ctx, f);
                return obj;
            }
            JS_SetOpaque(obj, f);
            
            /* Create a C function wrapper that calls our object */
            /* We can use JS_NewCFunctionData */
            JSValue func_data[1] = { obj };
            JSValue func = JS_NewCFunctionData(ctx, js_lua_func_call, 0, 0, 1, func_data);
            JS_FreeValue(ctx, obj);
            return func;
        }
        case LUA_TTABLE: {
            int is_array = 1;
            lua_pushnil(L);
            while (lua_next(L, idx < 0 ? idx - 1 : idx) != 0) {
                if (lua_type(L, -2) != LUA_TNUMBER || lua_tonumber(L, -2) <= 0 || lua_tonumber(L, -2) != (int)lua_tonumber(L, -2)) {
                    is_array = 0;
                    lua_pop(L, 2);
                    break;
                }
                lua_pop(L, 1);
            }
            
            JSValue obj = is_array ? JS_NewArray(ctx) : JS_NewObject(ctx);
            lua_pushnil(L);
            while (lua_next(L, idx < 0 ? idx - 1 : idx) != 0) {
                JSValue val = lua_to_js(L, ctx, lua_gettop(L));
                if (is_array) {
                    JS_SetPropertyUint32(ctx, obj, (uint32_t)lua_tonumber(L, -2) - 1, val);
                } else {
                    /* Safely convert key to string without modifying it in place (which breaks lua_next) */
                    lua_pushvalue(L, -2);
                    const char *key = lua_tostring(L, -1);
                    if (key) {
                        JS_SetPropertyStr(ctx, obj, key, val);
                    } else {
                        JS_FreeValue(ctx, val);
                    }
                    lua_pop(L, 1);
                }
                lua_pop(L, 1);
            }
            return obj;
        }
        default:
            return JS_UNDEFINED;
    }
}

static int js_to_lua(lua_State *L, int ctx_idx, JSContext *ctx, JSValue val) {
    if (JS_IsException(val)) {
        JSValue exception_val = JS_GetException(ctx);
        const char *err_str = JS_ToCString(ctx, exception_val);
        if (err_str) {
            lua_pushstring(L, err_str);
            JS_FreeCString(ctx, err_str);
        } else {
            lua_pushstring(L, "Unknown JavaScript Error");
        }
        JS_FreeValue(ctx, exception_val);
        JS_FreeValue(ctx, val);
        return lua_error(L);
    }
    
    if (JS_IsNumber(val)) {
        double d;
        JS_ToFloat64(ctx, &d, val);
        lua_pushnumber(L, d);
        JS_FreeValue(ctx, val);
        return 1;
    } else if (JS_IsBool(val)) {
        lua_pushboolean(L, JS_ToBool(ctx, val));
        JS_FreeValue(ctx, val);
        return 1;
    } else if (JS_IsString(val)) {
        const char *str = JS_ToCString(ctx, val);
        if (str) {
            lua_pushstring(L, str);
            JS_FreeCString(ctx, str);
        } else {
            lua_pushnil(L);
        }
        JS_FreeValue(ctx, val);
        return 1;
    } else if (JS_IsNull(val) || JS_IsUndefined(val) || JS_IsUninitialized(val)) {
        lua_pushnil(L);
        JS_FreeValue(ctx, val);
        return 1;
    } else {
        return push_js_value(L, ctx_idx, ctx, val);
    }
}

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

static int l_value_index(lua_State *L) {
    qjs_value_t *v = (qjs_value_t *)luaL_checkudata(L, 1, QJS_VALUE_MT);
    if (!v->ctx) return 0;
    
    if (lua_type(L, 2) == LUA_TSTRING) {
        const char *key = lua_tostring(L, 2);
        if (strcmp(key, "tostring") == 0) {
            lua_pushcfunction(L, l_value_tostring);
            return 1;
        }
        if (strcmp(key, "tonumber") == 0) {
            lua_pushcfunction(L, l_value_tonumber);
            return 1;
        }
    }
    
    JSValue prop;
    if (lua_type(L, 2) == LUA_TNUMBER) {
        prop = JS_GetPropertyUint32(v->ctx, v->val, (uint32_t)lua_tonumber(L, 2));
    } else {
        const char *key = luaL_checkstring(L, 2);
        prop = JS_GetPropertyStr(v->ctx, v->val, key);
    }
    
    lua_getuservalue(L, 1);
    int ctx_idx = lua_gettop(L);
    return js_to_lua(L, ctx_idx, v->ctx, prop);
}

static int l_value_newindex(lua_State *L) {
    qjs_value_t *v = (qjs_value_t *)luaL_checkudata(L, 1, QJS_VALUE_MT);
    if (!v->ctx) return 0;
    
    JSValue set_val = lua_to_js(L, v->ctx, 3);
    
    if (lua_type(L, 2) == LUA_TNUMBER) {
        JS_SetPropertyUint32(v->ctx, v->val, (uint32_t)lua_tonumber(L, 2), set_val);
    } else {
        const char *key = luaL_checkstring(L, 2);
        JS_SetPropertyStr(v->ctx, v->val, key, set_val);
    }
    
    return 0;
}

static int l_value_call(lua_State *L) {
    qjs_value_t *f = (qjs_value_t *)luaL_checkudata(L, 1, QJS_VALUE_MT);
    if (!f->ctx) return 0;
    
    int nargs = lua_gettop(L) - 1;
    JSValue *args = NULL;
    if (nargs > 0) {
        args = malloc(sizeof(JSValue) * nargs);
        for (int i = 0; i < nargs; i++) {
            args[i] = lua_to_js(L, f->ctx, i + 2);
        }
    }
    
    JSValue ret = JS_Call(f->ctx, f->val, JS_UNDEFINED, nargs, args);
    
    if (args) {
        for (int i = 0; i < nargs; i++) {
            JS_FreeValue(f->ctx, args[i]);
        }
        free(args);
    }
    
    lua_getuservalue(L, 1);
    int ctx_idx = lua_gettop(L);
    return js_to_lua(L, ctx_idx, f->ctx, ret);
}

static const luaL_Reg qjs_value_methods[] = {
    {"__gc", l_value_gc},
    {"__tostring", l_value_tostring},
    {"__index", l_value_index},
    {"__newindex", l_value_newindex},
    {"__call", l_value_call},
    {"tonumber", l_value_tonumber},
    {"tostring", l_value_tostring},
    {NULL, NULL}
};

/* --- JSContext wrapper methods --- */

static int push_js_value(lua_State *L, int ctx_idx, JSContext *ctx, JSValue val) {
    qjs_value_t *v = (qjs_value_t *)lua_newuserdatauv(L, sizeof(qjs_value_t), 1);
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
    return js_to_lua(L, 1, c->ctx, val);
}

static int l_ctx_global(lua_State *L) {
    qjs_context_t *c = (qjs_context_t *)luaL_checkudata(L, 1, QJS_CONTEXT_MT);
    JSValue global = JS_GetGlobalObject(c->ctx);
    return push_js_value(L, 1, c->ctx, global);
}

static int l_ctx_compile(lua_State *L) {
    qjs_context_t *c = (qjs_context_t *)luaL_checkudata(L, 1, QJS_CONTEXT_MT);
    size_t len;
    const char *script = luaL_checklstring(L, 2, &len);
    const char *filename = luaL_optstring(L, 3, "<eval>");
    int flags = JS_EVAL_FLAG_COMPILE_ONLY;
    if (lua_toboolean(L, 4)) {
        flags |= JS_EVAL_TYPE_MODULE;
    } else {
        flags |= JS_EVAL_TYPE_GLOBAL;
    }

    JSValue val = JS_Eval(c->ctx, script, len, filename, flags);
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
    
    size_t out_buf_len;
    int write_flags = JS_WRITE_OBJ_BYTECODE;
    uint8_t *out_buf = JS_WriteObject(c->ctx, &out_buf_len, val, write_flags);
    JS_FreeValue(c->ctx, val);

    if (!out_buf) {
        return luaL_error(L, "Failed to compile JS to bytecode");
    }

    lua_pushlstring(L, (const char *)out_buf, out_buf_len);
    js_free(c->ctx, out_buf);
    return 1;
}

static int l_ctx_eval_binary(lua_State *L) {
    qjs_context_t *c = (qjs_context_t *)luaL_checkudata(L, 1, QJS_CONTEXT_MT);
    size_t len;
    const uint8_t *buf = (const uint8_t *)luaL_checklstring(L, 2, &len);

    JSValue obj = JS_ReadObject(c->ctx, buf, len, JS_READ_OBJ_BYTECODE);
    if (JS_IsException(obj)) {
        JSValue exception_val = JS_GetException(c->ctx);
        const char *err_str = JS_ToCString(c->ctx, exception_val);
        if (err_str) {
            lua_pushstring(L, err_str);
            JS_FreeCString(c->ctx, err_str);
        } else {
            lua_pushstring(L, "Unknown JavaScript Error");
        }
        JS_FreeValue(c->ctx, exception_val);
        JS_FreeValue(c->ctx, obj);
        return lua_error(L);
    }

    JSValue val;
    if (JS_ResolveModule(c->ctx, obj) < 0) {
        JS_FreeValue(c->ctx, obj);
        return luaL_error(L, "Failed to resolve JS module");
    }
    
    val = JS_EvalFunction(c->ctx, obj);
    
    lua_pushvalue(L, 1);
    int ctx_idx = lua_gettop(L);
    int ret = js_to_lua(L, ctx_idx, c->ctx, val);
    lua_remove(L, ctx_idx);
    
    return ret;
}

static int l_ctx_index(lua_State *L) {
    if (lua_type(L, 2) == LUA_TSTRING) {
        const char *key = lua_tostring(L, 2);
        if (strcmp(key, "eval") == 0) {
            lua_pushcfunction(L, l_ctx_eval);
            return 1;
        } else if (strcmp(key, "compile") == 0) {
            lua_pushcfunction(L, l_ctx_compile);
            return 1;
        } else if (strcmp(key, "evalBinary") == 0) {
            lua_pushcfunction(L, l_ctx_eval_binary);
            return 1;
        } else if (strcmp(key, "getGlobal") == 0) {
            lua_pushcfunction(L, l_ctx_global);
            return 1;
        }
    }
    return 0;
}

static const luaL_Reg qjs_context_methods[] = {
    {"__gc", l_ctx_gc},
    {"__index", l_ctx_index},
    {"eval", l_ctx_eval},
    {"compile", l_ctx_compile},
    {"evalBinary", l_ctx_eval_binary},
    {"getGlobal", l_ctx_global},
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
    qjs_context_t *c = (qjs_context_t *)lua_newuserdatauv(L, sizeof(qjs_context_t), 1);
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
    {"newContext", l_rt_new_context},
    {NULL, NULL}
};

/* --- Module functions --- */

static int l_new_runtime(lua_State *L) {
    JSRuntime *rt = JS_NewRuntime();
    if (!rt) {
        return luaL_error(L, "Failed to create JS runtime");
    }
    qjs_runtime_t *r = (qjs_runtime_t *)lua_newuserdatauv(L, sizeof(qjs_runtime_t), 1);
    r->rt = rt;
    luaL_getmetatable(L, QJS_RUNTIME_MT);
    lua_setmetatable(L, -2);
    return 1;
}

static const luaL_Reg qjs_funcs[] = {
    {"newRuntime", l_new_runtime},
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
