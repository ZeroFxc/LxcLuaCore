#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "quickjs.h"
#include "quickjs-libc.h"

/* Metatable names */
#define QJS_RUNTIME_MT "qjs_runtime"
#define QJS_CONTEXT_MT "qjs_context"
#define QJS_VALUE_MT "qjs_value"

/* ================================================================
 * 模块注册表 — 用于支持 JS 的 import 语句
 * 每个 runtime 维护一个链表，存储预注册的模块名→源代码映射
 * ================================================================ */
typedef struct ModuleEntry {
    char *name;             /* 模块名 (如 "math", "./utils.js") */
    char *source;           /* 模块源代码 */
    size_t source_len;      /* 源代码长度 */
    struct ModuleEntry *next;
} ModuleEntry;

/* Wrapper structures */
typedef struct {
    JSRuntime *rt;
    ModuleEntry *modules;   /* 模块注册表链表头 */
} qjs_runtime_t;

typedef struct {
    JSContext *ctx;
    qjs_runtime_t *rt;      /* 反向引用 runtime，访问模块注册表 */
} qjs_context_t;

typedef struct {
    JSContext *ctx;
    JSValue val;
} qjs_value_t;

static int push_js_value(lua_State *L, int ctx_idx, JSContext *ctx, JSValue val);
static int js_to_lua(lua_State *L, int ctx_idx, JSContext *ctx, JSValue val);
static JSValue lua_to_js(lua_State *L, JSContext *ctx, int idx);

/* ================================================================
 * JS 模块加载器回调 — QuickJS 遇到 import 时调用
 * 在模块注册表中查找，找到则编译返回；否则返回 NULL 触发错误
 * ================================================================ */
static JSModuleDef *lqjs_module_loader(JSContext *ctx, const char *module_name, void *opaque) {
    qjs_runtime_t *rt = (qjs_runtime_t *)opaque;
    if (!rt) return NULL;

    /* 在注册表中查找匹配的模块 */
    ModuleEntry *entry = rt->modules;
    while (entry) {
        if (strcmp(entry->name, module_name) == 0) {
            /* 找到模块，编译为 JSModuleDef */
            JSValue func_val = JS_Eval(ctx, entry->source, entry->source_len,
                                       module_name, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
            if (JS_IsException(func_val)) {
                /* 编译失败，打印错误到 stderr */
                JSValue exc = JS_GetException(ctx);
                const char *err = JS_ToCString(ctx, exc);
                if (err) {
                    fprintf(stderr, "[quickjs] module loader compile error for '%s': %s\n",
                            module_name, err);
                    JS_FreeCString(ctx, err);
                }
                JS_FreeValue(ctx, exc);
                JS_FreeValue(ctx, func_val);
                return NULL;
            }
            /* func_val 是一个 compiled module function，包装成 JSModuleDef */
            JSModuleDef *m = (JSModuleDef *)JS_VALUE_GET_PTR(func_val);
            /* 注意: 不能 free func_val，因为 JSModuleDef 实际上就是它 */
            return m;
        }
        entry = entry->next;
    }

    fprintf(stderr, "[quickjs] module not found in registry: '%s'\n", module_name);
    return NULL;
}

/* ================================================================
 * JS 模块路径规范化回调 — 可用于处理相对路径
 * 当前实现: 原样返回模块名
 * ================================================================ */
static char *lqjs_module_normalize(JSContext *ctx, const char *module_base_name,
                                  const char *module_name, void *opaque) {
    /* 简单实现: 直接返回模块名的副本 */
    return js_strdup(ctx, module_name);
}

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

static JSValue js_lua_func_call(JSContext *ctx, JSValueConst this_val, int argc,
                                 JSValueConst *argv, int magic, JSValue *func_data) {
    js_lua_func_t *f = JS_GetOpaque(func_data[0], js_lua_func_class_id);
    if (!f) return JS_EXCEPTION;

    lua_State *L = f->L;
    int top = lua_gettop(L);

    lua_rawgeti(L, LUA_REGISTRYINDEX, f->ref);
    if (!lua_isfunction(L, -1)) {
        lua_settop(L, top);
        return JS_ThrowTypeError(ctx, "not a lua function");
    }

    /* 将 JS 参数转换为 Lua 值压栈 */
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
            f->ctx_idx = 1;

            JSValue obj = JS_NewObjectClass(ctx, js_lua_func_class_id);
            if (JS_IsException(obj)) {
                js_free(ctx, f);
                return obj;
            }
            JS_SetOpaque(obj, f);
            
            JSValue func_data[1] = { obj };
            JSValue func = JS_NewCFunctionData(ctx, js_lua_func_call, 0, 0, 1, func_data);
            JS_FreeValue(ctx, obj);
            return func;
        }
        case LUA_TTABLE: {
            int is_array = 1;
            lua_pushnil(L);
            while (lua_next(L, idx < 0 ? idx - 1 : idx) != 0) {
                if (lua_type(L, -2) != LUA_TNUMBER || lua_tonumber(L, -2) <= 0
                    || lua_tonumber(L, -2) != (int)lua_tonumber(L, -2)) {
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
        /* Lua 使用 1-based 索引，JS 使用 0-based。
           arr[1] (Lua) → arr[0] (JS), arr[2] (Lua) → arr[1] (JS), ...
           这样 ipairs 遍历数组时就能正确访问所有元素 */
        lua_Integer luai = lua_tointeger(L, 2);
        prop = JS_GetPropertyUint32(v->ctx, v->val, (uint32_t)(luai >= 1 ? luai - 1 : 0));
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
        lua_Integer luai = lua_tointeger(L, 2);
        JS_SetPropertyUint32(v->ctx, v->val, (uint32_t)(luai >= 1 ? luai - 1 : 0), set_val);
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
    JSValue this_obj = JS_UNDEFINED;
    int start_arg = 2;  /* 第一个参数在栈上的位置 */

    /* 如果第一个参数是 JSValue userdata，用它作为 JS 的 this 对象
       这对应 Lua 的 : 方法调用语法 (obj:method(args)) */
    if (nargs >= 1) {
        qjs_value_t *this_v = (qjs_value_t *)luaL_testudata(L, 2, QJS_VALUE_MT);
        if (this_v && this_v->ctx) {
            this_obj = this_v->val;
            start_arg = 3;
            nargs--;
        }
    }

    JSValue *args = NULL;
    if (nargs > 0) {
        args = malloc(sizeof(JSValue) * nargs);
        for (int i = 0; i < nargs; i++) {
            args[i] = lua_to_js(L, f->ctx, start_arg + i);
        }
    }
    
    JSValue ret = JS_Call(f->ctx, f->val, this_obj, nargs, args);
    
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

/* ================================================================
 * __len 元方法 — 返回 JS 对象长度
 * 对 JS 数组返回 array.length，对字符串返回字节长度，其他返回 0
 * ================================================================ */
static int l_value_len(lua_State *L) {
    qjs_value_t *v = (qjs_value_t *)luaL_checkudata(L, 1, QJS_VALUE_MT);
    if (!v->ctx) {
        lua_pushinteger(L, 0);
        return 1;
    }

    /* 尝试获取数组的 length 属性 */
    JSValue len_val = JS_GetPropertyStr(v->ctx, v->val, "length");
    if (!JS_IsException(len_val)) {
        uint32_t len;
        if (JS_ToUint32(v->ctx, &len, len_val) == 0) {
            JS_FreeValue(v->ctx, len_val);
            lua_pushinteger(L, (lua_Integer)len);
            return 1;
        }
        JS_FreeValue(v->ctx, len_val);
    } else {
        JS_FreeValue(v->ctx, len_val);
    }

    /* 非数组：如果是字符串，返回字符串长度 */
    if (JS_IsString(v->val)) {
        size_t slen;
        const char *str = JS_ToCStringLen(v->ctx, &slen, v->val);
        lua_pushinteger(L, (lua_Integer)slen);
        JS_FreeCString(v->ctx, str);
        return 1;
    }

    lua_pushinteger(L, 0);
    return 1;
}

/* ================================================================
 * __ipairs 元方法 — Lua 1-based 索引迭代 JS 数组
 *
 * 用法：for i, v in ipairs(js_arr) do ... end
 *      等价于 Lua 中的 for i=1,#arr do 遍历
 *
 * Lua 的 for-in 循环将上一次迭代返回的第一个值作为下一次迭代
 * 的第二个参数。因此迭代器必须返回 "下一个索引" (next key)，
 * 而不是 "当前索引"。
 *
 * 调用示例: first = ipairs_iter(arr, 0) → returns (1, arr[0])
 *           next  = ipairs_iter(arr, 1) → returns (2, arr[1])
 *
 * 返回: (迭代器函数, 被迭代的对象, 起始索引 0)
 * ================================================================ */
static int l_value_ipairs_iter(lua_State *L) {
    /* 参数: arg1 = JSValue userdata, arg2 = 上一次返回的 key */
    qjs_value_t *v = (qjs_value_t *)luaL_checkudata(L, 1, QJS_VALUE_MT);
    lua_Integer prev_i = luaL_checkinteger(L, 2);
    lua_Integer next_i = prev_i + 1;  /* 计算下一个 Lua 1-based 索引 */

    if (!v->ctx) {
        lua_pushnil(L);
        lua_pushnil(L);
        return 2;
    }

    /* 获取数组长度判断越界 */
    JSValue len_val = JS_GetPropertyStr(v->ctx, v->val, "length");
    uint32_t length = 0;
    if (!JS_IsException(len_val)) {
        JS_ToUint32(v->ctx, &length, len_val);
        JS_FreeValue(v->ctx, len_val);
    } else {
        JS_FreeValue(v->ctx, len_val);
    }
    if (next_i > (lua_Integer)length) {
        lua_pushnil(L);
        lua_pushnil(L);
        return 2;
    }

    /* JS 数组是 0-based，Lua 是 1-based：
       next_i=1 → arr[0], next_i=2 → arr[1], ... */
    JSValue elem = JS_GetPropertyUint32(v->ctx, v->val, (uint32_t)(next_i - 1));
    lua_pushinteger(L, next_i);  /* 返回下一个索引作为 key */
    lua_getuservalue(L, 1);
    return js_to_lua(L, lua_gettop(L), v->ctx, elem);
}

static int l_value_ipairs(lua_State *L) {
    (void)luaL_checkudata(L, 1, QJS_VALUE_MT);
    lua_pushcfunction(L, l_value_ipairs_iter);
    lua_pushvalue(L, 1);   /* 被迭代的 JSValue 对象 */
    lua_pushinteger(L, 0); /* 起始 key = 0 → 第一次迭代产生 i=1 */
    return 3;
}

/* ================================================================
 * __pairs 元方法 — 遍历 JS 对象的所有自有可枚举属性
 *
 * 将 JS 对象的属性复制到临时 Lua 表中，然后用标准 next 函数迭代。
 * 这样 for k, v in pairs(js_obj) do ... end 就能正常工作。
 *
 * 返回: (next 函数, 临时 Lua 表, nil)
 * ================================================================ */
static int l_value_pairs(lua_State *L) {
    qjs_value_t *v = (qjs_value_t *)luaL_checkudata(L, 1, QJS_VALUE_MT);
    if (!v->ctx) {
        lua_pushnil(L);
        return 1;
    }

    /* 创建临时 Lua 表存储 JS 对象属性 */
    lua_newtable(L);
    int result_table = lua_gettop(L);

    JSPropertyEnum *tab = NULL;
    uint32_t tab_len = 0;
    if (JS_GetOwnPropertyNames(v->ctx, &tab, &tab_len, v->val,
                               JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) >= 0
        && tab) {
        for (uint32_t i = 0; i < tab_len; i++) {
            const char *key = JS_AtomToCString(v->ctx, tab[i].atom);
            if (key) {
                JSValue prop_val = JS_GetProperty(v->ctx, v->val, tab[i].atom);
                /* 转换为 Lua 值并存入表 */
                lua_getuservalue(L, 1);
                js_to_lua(L, lua_gettop(L), v->ctx, prop_val);
                lua_setfield(L, result_table, key);
                JS_FreeCString(v->ctx, key);
            }
        }
        js_free(v->ctx, tab);
    }

    /* 返回标准三值: next, table, nil → pairs(table) 的行为 */
    lua_getglobal(L, "next");
    lua_pushvalue(L, result_table);
    lua_pushnil(L);
    return 3;
}

/* ================================================================
 * __eq 元方法 — JS 严格相等比较 (===)
 *
 * 用法：if js_a == js_b then ... end
 * 两个 JSValue 分别包装的 JS 值，用 JS_SameValue (Object.is 语义)
 * 进行比较。数字 NaN === NaN 在此返回 true。
 * ================================================================ */
static int l_value_eq(lua_State *L) {
    qjs_value_t *a = (qjs_value_t *)luaL_checkudata(L, 1, QJS_VALUE_MT);
    qjs_value_t *b = (qjs_value_t *)luaL_testudata(L, 2, QJS_VALUE_MT);
    if (!b || !a->ctx || !b->ctx) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, JS_SameValue(a->ctx, a->val, b->val));
    return 1;
}

/* ================================================================
 * __lt 元方法 — JS 大小比较 (<)
 *
 * 优先尝试数字比较 (JS_ToFloat64)，其次字符串比较 (JS_ToCString)，
 * 无法比较的返回 false。
 * ================================================================ */
static int l_value_lt(lua_State *L) {
    qjs_value_t *a = (qjs_value_t *)luaL_checkudata(L, 1, QJS_VALUE_MT);
    qjs_value_t *b = (qjs_value_t *)luaL_testudata(L, 2, QJS_VALUE_MT);
    if (!b || !a->ctx) {
        lua_pushboolean(L, 0);
        return 1;
    }

    /* 1. 两边都是 JS 数字 → 数字比较 */
    if (JS_IsNumber(a->val) && JS_IsNumber(b->val)) {
        double da, db;
        JS_ToFloat64(a->ctx, &da, a->val);
        JS_ToFloat64(b->ctx, &db, b->val);
        lua_pushboolean(L, da < db);
        return 1;
    }

    /* 2. 至少一边不是数字 → 尝试转为字符串比较
       JS_ToCString 对任何值都会调用 ToString 转换，
       数组 → "1,2,3"，对象 → "[object Object]"，等 */
    const char *sa = JS_ToCString(a->ctx, a->val);
    const char *sb = JS_ToCString(b->ctx, b->val);
    if (sa && sb) {
        lua_pushboolean(L, strcmp(sa, sb) < 0);
        JS_FreeCString(b->ctx, sb);
        JS_FreeCString(a->ctx, sa);
        return 1;
    }
    if (sa) JS_FreeCString(a->ctx, sa);
    if (sb) JS_FreeCString(b->ctx, sb);

    lua_pushboolean(L, 0);
    return 1;
}

/* ================================================================
 * __le 元方法 — JS 大小比较 (<=)
 *
 * 逻辑同 __lt，使用 <= 比较。
 * ================================================================ */
static int l_value_le(lua_State *L) {
    qjs_value_t *a = (qjs_value_t *)luaL_checkudata(L, 1, QJS_VALUE_MT);
    qjs_value_t *b = (qjs_value_t *)luaL_testudata(L, 2, QJS_VALUE_MT);
    if (!b || !a->ctx) {
        lua_pushboolean(L, 0);
        return 1;
    }

    /* 1. 两边都是 JS 数字 → 数字比较 */
    if (JS_IsNumber(a->val) && JS_IsNumber(b->val)) {
        double da, db;
        JS_ToFloat64(a->ctx, &da, a->val);
        JS_ToFloat64(b->ctx, &db, b->val);
        lua_pushboolean(L, da <= db);
        return 1;
    }

    /* 2. 至少一边不是数字 → 转为字符串比较 */
    const char *sa = JS_ToCString(a->ctx, a->val);
    const char *sb = JS_ToCString(b->ctx, b->val);
    if (sa && sb) {
        lua_pushboolean(L, strcmp(sa, sb) <= 0);
        JS_FreeCString(b->ctx, sb);
        JS_FreeCString(a->ctx, sa);
        return 1;
    }
    if (sa) JS_FreeCString(a->ctx, sa);
    if (sb) JS_FreeCString(b->ctx, sb);

    lua_pushboolean(L, 0);
    return 1;
}

/* ================================================================
 * __concat 元方法 — JS 值字符串拼接
 *
 * 用法：js_val .. " suffix"  或  "prefix " .. js_val
 * 调用 JS_ToCString 转为字符串后拼接。
 * ================================================================ */
static int l_value_concat(lua_State *L) {
    qjs_value_t *a = (qjs_value_t *)luaL_checkudata(L, 1, QJS_VALUE_MT);
    if (!a->ctx) {
        lua_pushstring(L, "freed JSValue");
        return 1;
    }

    /* 左操作数转为 JS 字符串 */
    const char *s1 = JS_ToCString(a->ctx, a->val);
    int free_s1 = (s1 != NULL);  /* 记录是否需要释放 */

    /* 右操作数：可能是 JSValue 也可能是 Lua 原生字符串 */
    qjs_value_t *b = (qjs_value_t *)luaL_testudata(L, 2, QJS_VALUE_MT);
    const char *s2 = NULL;
    int free_s2 = 0;
    if (b && b->ctx) {
        s2 = JS_ToCString(b->ctx, b->val);
        free_s2 = (s2 != NULL);
    } else {
        s2 = lua_tostring(L, 2);
        /* lua_tostring 返回的是内部字符串，不需要释放 */
    }

    /* 拼接并推入结果 */
    lua_pushfstring(L, "%s%s",
                    s1 ? s1 : "undefined",
                    s2 ? s2 : "undefined");

    if (free_s2) JS_FreeCString(b->ctx, s2);
    if (free_s1) JS_FreeCString(a->ctx, s1);
    return 1;
}

static const luaL_Reg qjs_value_methods[] = {
    {"__gc", l_value_gc},
    {"__tostring", l_value_tostring},
    {"__index", l_value_index},
    {"__newindex", l_value_newindex},
    {"__call", l_value_call},
    {"__len", l_value_len},
    {"__ipairs", l_value_ipairs},
    {"__pairs", l_value_pairs},
    {"__eq", l_value_eq},
    {"__lt", l_value_lt},
    {"__le", l_value_le},
    {"__concat", l_value_concat},
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
    /* 将 JSValue 关联到 Context，防止 Context 提前被 GC */
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

/* ================================================================
 * ctx:eval(script [, filename [, is_module]])
 * 执行 JS 脚本
 *   script   - JS 源代码字符串
 *   filename - 调试用文件名 (可选, 默认 "<eval>")
 *   is_module- true=ES模块模式, false/缺省=全局脚本模式
 * 返回: 最后一个表达式的值
 * ================================================================ */
static int l_ctx_eval(lua_State *L) {
    qjs_context_t *c = (qjs_context_t *)luaL_checkudata(L, 1, QJS_CONTEXT_MT);
    size_t len;
    const char *script = luaL_checklstring(L, 2, &len);
    const char *filename = luaL_optstring(L, 3, "<eval>");
    int is_module = lua_toboolean(L, 4);

    int flags = is_module ? JS_EVAL_TYPE_MODULE : JS_EVAL_TYPE_GLOBAL;
    JSValue val = JS_Eval(c->ctx, script, len, filename, flags);
    return js_to_lua(L, 1, c->ctx, val);
}

/* ================================================================
 * ctx:evalModule(script [, filename])
 * 一步执行 ES 模块 — 编译、解析、实例化并求值
 *   script   - ES 模块源代码 (可包含 import/export)
 *   filename - 调试用文件名 (可选, 默认 "<module>")
 * 返回: 模块命名空间 (JSValue userdata)，可通过 .导出名 访问
 * ================================================================ */
static int l_ctx_eval_module(lua_State *L) {
    qjs_context_t *c = (qjs_context_t *)luaL_checkudata(L, 1, QJS_CONTEXT_MT);
    size_t len;
    const char *script = luaL_checklstring(L, 2, &len);
    const char *filename = luaL_optstring(L, 3, "<module>");

    /* 编译为模块 (只编译不执行) */
    JSValue compiled = JS_Eval(c->ctx, script, len, filename,
                               JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(compiled)) {
        JSValue exc = JS_GetException(c->ctx);
        const char *err = JS_ToCString(c->ctx, exc);
        if (err) { lua_pushstring(L, err); JS_FreeCString(c->ctx, err); }
        else { lua_pushstring(L, "Unknown JS compile error"); }
        JS_FreeValue(c->ctx, exc);
        JS_FreeValue(c->ctx, compiled);
        return lua_error(L);
    }

    /* 保存 JSModuleDef 指针 (JS_EvalFunction 内部需要 compiled 引用) */
    JSModuleDef *md = JS_VALUE_GET_PTR(compiled);

    /* JS_EvalFunctionInternal 内部会 JS_FreeValue(fun_obj)，
       需要 refcount >= 2 (我们的引用 + 内部的引用) */
    JS_DupValue(c->ctx, compiled);

    /* 执行模块: js_create_module_function → js_link_module → js_evaluate_module */
    JSValue promise = JS_EvalFunction(c->ctx, compiled);
    if (JS_IsException(promise)) {
        JS_FreeValue(c->ctx, compiled);
        JSValue exc = JS_GetException(c->ctx);
        const char *err = JS_ToCString(c->ctx, exc);
        if (err) { lua_pushstring(L, err); JS_FreeCString(c->ctx, err); }
        else { lua_pushstring(L, "Unknown JS eval error"); }
        JS_FreeValue(c->ctx, exc);
        JS_FreeValue(c->ctx, promise);
        return lua_error(L);
    }

    JS_FreeValue(c->ctx, promise);
    JS_FreeValue(c->ctx, compiled);  /* 释放我们持有的额外引用 */

    /* 命名空间与 Promise 分离: 模块执行的 Promise 固定 resolve 为 undefined，
       真正的导出通过 JS_GetModuleNamespace 获取 */
    JSValue ns = JS_GetModuleNamespace(c->ctx, md);

    lua_pushvalue(L, 1);
    int ctx_idx = lua_gettop(L);
    int ret = js_to_lua(L, ctx_idx, c->ctx, ns);
    lua_remove(L, ctx_idx);
    return ret;
}

/* ================================================================
 * ctx:registerModule(name, code)
 * 预注册一个 JS 模块到运行时注册表，使其能被 import 语句找到
 *   name - 模块名 (如 "math", "./utils.js", "@scope/pkg")
 *   code - 模块源代码 (ES module 格式)
 * 无返回值
 * ================================================================ */
static int l_ctx_register_module(lua_State *L) {
    qjs_context_t *c = (qjs_context_t *)luaL_checkudata(L, 1, QJS_CONTEXT_MT);
    const char *name = luaL_checkstring(L, 2);
    size_t code_len;
    const char *code = luaL_checklstring(L, 3, &code_len);

    if (!c->rt) {
        return luaL_error(L, "context has no associated runtime");
    }

    /* 创建新的模块条目 */
    ModuleEntry *entry = (ModuleEntry *)malloc(sizeof(ModuleEntry));
    entry->name = strdup(name);
    entry->source = (char *)malloc(code_len + 1);
    memcpy(entry->source, code, code_len);
    entry->source[code_len] = '\0';
    entry->source_len = code_len;

    /* 首次注册模块时设置模块加载器 */
    if (!c->rt->modules) {
        JS_SetModuleLoaderFunc(c->rt->rt, lqjs_module_normalize, lqjs_module_loader, c->rt);
    }

    /* 插入链表头部 */
    entry->next = c->rt->modules;
    c->rt->modules = entry;

    return 0;
}

/* ================================================================
 * ctx:dumpModuleRegistry()
 * 调试用: 打印当前 runtime 中所有已注册的模块名
 * ================================================================ */
static int l_ctx_dump_modules(lua_State *L) {
    qjs_context_t *c = (qjs_context_t *)luaL_checkudata(L, 1, QJS_CONTEXT_MT);
    if (!c->rt) return 0;

    lua_newtable(L);
    int i = 1;
    ModuleEntry *entry = c->rt->modules;
    while (entry) {
        lua_pushstring(L, entry->name);
        lua_rawseti(L, -2, i++);
        entry = entry->next;
    }
    return 1;
}

/* ================================================================
 * ctx:loop()
 * 驱动 QuickJS 事件循环 — setTimeout/setInterval 的回调依赖此调用触发
 * 在非阻塞模式下，应定期调用此函数或在独立线程中运行
 * ================================================================ */
static int l_ctx_loop(lua_State *L) {
    qjs_context_t *c = (qjs_context_t *)luaL_checkudata(L, 1, QJS_CONTEXT_MT);
    js_std_loop(c->ctx);
    return 0;
}

static int l_ctx_global(lua_State *L) {
    qjs_context_t *c = (qjs_context_t *)luaL_checkudata(L, 1, QJS_CONTEXT_MT);
    JSValue js_global = JS_GetGlobalObject(c->ctx);
    return push_js_value(L, 1, c->ctx, js_global);
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

    if (JS_ResolveModule(c->ctx, obj) < 0) {
        JS_FreeValue(c->ctx, obj);
        return luaL_error(L, "Failed to resolve JS module");
    }
    
    /* 保存 JSModuleDef 指针 */
    JSModuleDef *md = JS_VALUE_GET_PTR(obj);

    /* JS_EvalFunctionInternal 内部会 JS_FreeValue，需要额外引用 */
    JS_DupValue(c->ctx, obj);

    /* 执行模块 */
    JSValue promise = JS_EvalFunction(c->ctx, obj);
    if (JS_IsException(promise)) {
        JS_FreeValue(c->ctx, obj);
        JSValue exc = JS_GetException(c->ctx);
        const char *err = JS_ToCString(c->ctx, exc);
        if (err) { lua_pushstring(L, err); JS_FreeCString(c->ctx, err); }
        else { lua_pushstring(L, "Unknown JS eval error"); }
        JS_FreeValue(c->ctx, exc);
        JS_FreeValue(c->ctx, promise);
        return lua_error(L);
    }

    JS_FreeValue(c->ctx, promise);
    JS_FreeValue(c->ctx, obj);  /* 释放我们的额外引用 */

    /* 命名空间通过 JS_GetModuleNamespace 获取 */
    JSValue ns = JS_GetModuleNamespace(c->ctx, md);
    
    lua_pushvalue(L, 1);
    int ctx_idx = lua_gettop(L);
    int ret = js_to_lua(L, ctx_idx, c->ctx, ns);
    lua_remove(L, ctx_idx);
    
    return ret;
}

static int l_ctx_index(lua_State *L) {
    if (lua_type(L, 2) == LUA_TSTRING) {
        const char *key = lua_tostring(L, 2);
        if (strcmp(key, "eval") == 0) {
            lua_pushcfunction(L, l_ctx_eval);
            return 1;
        } else if (strcmp(key, "evalModule") == 0) {
            lua_pushcfunction(L, l_ctx_eval_module);
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
        } else if (strcmp(key, "registerModule") == 0) {
            lua_pushcfunction(L, l_ctx_register_module);
            return 1;
        } else if (strcmp(key, "dumpModules") == 0) {
            lua_pushcfunction(L, l_ctx_dump_modules);
            return 1;
        } else if (strcmp(key, "loop") == 0) {
            lua_pushcfunction(L, l_ctx_loop);
            return 1;
        }
    }
    return 0;
}

static const luaL_Reg qjs_context_methods[] = {
    {"__gc", l_ctx_gc},
    {"__index", l_ctx_index},
    {"eval", l_ctx_eval},
    {"evalModule", l_ctx_eval_module},
    {"compile", l_ctx_compile},
    {"evalBinary", l_ctx_eval_binary},
    {"getGlobal", l_ctx_global},
    {"registerModule", l_ctx_register_module},
    {"dumpModules", l_ctx_dump_modules},
    {"loop", l_ctx_loop},
    {NULL, NULL}
};

/* --- JSRuntime wrapper methods --- */

static int l_rt_gc(lua_State *L) {
    qjs_runtime_t *r = (qjs_runtime_t *)luaL_checkudata(L, 1, QJS_RUNTIME_MT);
    if (r->rt) {
        /* 释放模块注册表中的所有条目 */
        ModuleEntry *entry = r->modules;
        while (entry) {
            ModuleEntry *next = entry->next;
            free(entry->name);
            free(entry->source);
            free(entry);
            entry = next;
        }
        r->modules = NULL;

        js_std_free_handlers(r->rt);
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

    /* === 初始化 QuickJS 标准库 === */

    /* 1. 注册系统模块 (quickjs-libc) */
    JSModuleDef *std_m = js_init_module_std(ctx, "std");
    JSModuleDef *os_m  = js_init_module_os(ctx, "os");
    (void)std_m;  /* 仅注册即可，其内部初始化由模块加载触发 */

    /* 2. 注入基础 helpers (console, print, performance, scriptArgs, __loadScript) */
    js_std_add_helpers(ctx, 0, NULL);

    /* 3. 将 os 模块的导出复制到全局作用域（setTimeout, sleepAsync 等）。
     *
     * C 模块（JS_NewCModule）在创建后 export_entries 中的 var_ref 为 NULL，
     * 必须通过完整的模块初始化流程才能安全获取命名空间：
     *   js_create_module_function → 为每个 export entry 创建 var_ref
     *   js_link_module            → 解析模块依赖，状态切换为 LINKED
     *   js_evaluate_module        → 调用 init_func (js_os_init) 设置导出值
     *
     * JS_EvalFunction 对 JS_TAG_MODULE 类型的值会自动执行上述三步。
     * 注意：JS_EvalFunction 内部会 JS_FreeValue 掉传入的 module 值，
     * 所以需要先 JS_DupValue 保证 refcount >= 2，防止模块被意外释放。 */
    if (os_m) {
        JSModuleDef *os_mod = os_m;
        JSValue mod_val = JS_MKPTR(JS_TAG_MODULE, os_mod);
        JS_DupValue(ctx, mod_val);  /* refcount: 1→2, 防止内部 FreeValue 释放 */
        JSValue eval_result = JS_EvalFunction(ctx, mod_val);
        JS_FreeValue(ctx, eval_result);  /* 忽略 eval 返回值 (Promise) */

        /* 初始化完成，现在可以安全获取命名空间并复制导出到全局 */
        JSValue global_obj = JS_GetGlobalObject(ctx);
        if (!JS_IsException(global_obj)) {
            JSValue ns = JS_GetModuleNamespace(ctx, os_mod);
            if (!JS_IsException(ns)) {
                JSPropertyEnum *tab = NULL;
                uint32_t tab_len = 0;
                if (JS_GetOwnPropertyNames(ctx, &tab, &tab_len, ns,
                                           JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) >= 0
                    && tab) {
                    for (uint32_t i = 0; i < tab_len; i++) {
                        const char *key = JS_AtomToCString(ctx, tab[i].atom);
                        if (key) {
                            JSValue val = JS_GetProperty(ctx, ns, tab[i].atom);
                            JS_SetPropertyStr(ctx, global_obj, key, val);
                            JS_FreeCString(ctx, key);
                        }
                    }
                    js_free(ctx, tab);
                }
            }
            JS_FreeValue(ctx, ns);
            JS_FreeValue(ctx, global_obj);
        }
    }

    /* 模块加载器延迟到首次需要 import 时才设置，避免干扰基础模块求值 */
    /* JS_SetModuleLoaderFunc 会在 registerModule 被调用时设置 */

    qjs_context_t *c = (qjs_context_t *)lua_newuserdatauv(L, sizeof(qjs_context_t), 1);
    c->ctx = ctx;
    c->rt = r;  /* 反向引用 runtime */
    luaL_getmetatable(L, QJS_CONTEXT_MT);
    lua_setmetatable(L, -2);
    /* 关联 context 到 runtime，防止 runtime 提前被 GC */
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

/* ================================================================
 * quickjs.newRuntime()
 * 创建一个新的 QuickJS 运行时
 * 返回: JSRuntime userdata，可调用 :newContext() 创建执行上下文
 * ================================================================ */
static int l_new_runtime(lua_State *L) {
    JSRuntime *rt = JS_NewRuntime();
    if (!rt) {
        return luaL_error(L, "Failed to create JS runtime");
    }

    /* 初始化 QuickJS 标准库 handler (setTimeout 等依赖此调用) */
    js_std_init_handlers(rt);

    qjs_runtime_t *r = (qjs_runtime_t *)lua_newuserdatauv(L, sizeof(qjs_runtime_t), 1);
    r->rt = rt;
    r->modules = NULL;   /* 初始化模块注册表为空 */
    luaL_getmetatable(L, QJS_RUNTIME_MT);
    lua_setmetatable(L, -2);
    return 1;
}

static const luaL_Reg qjs_funcs[] = {
    {"newRuntime", l_new_runtime},
    {NULL, NULL}
};

/* 创建 metatable 并填充方法 */
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