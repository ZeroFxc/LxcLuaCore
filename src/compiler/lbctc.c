#define lbctc_c
#define LUA_LIB

#include "lprefix.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>

#include "lua.h"
#include "lauxlib.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lstate.h"
#include "lundump.h"
#include "lbctc.h"
#include "lopnames.h"
#include "lobfuscate.h"
#include "lmap.h"
#include "lclass.h"
#include "ltable.h"
#include "lstring.h"
#include "ldebug.h"
#include "lpromise.h"

/*
** tcc support functions (Library API)
*/

/**
 * @brief TCC 转译代码的函数 prologue：构造变长参数表
 *
 * 函数调用时栈布局：
 *   L[1]          = 函数值本身
 *   L[2]          = 第 1 个固定参数
 *   ...
 *   L[nparams+1]  = 第 nparams 个固定参数
 *   L[nparams+2]  = 第 1 个变长参数
 *   ...
 *   L[nargs]      = 最后 1 个变长参数
 *
 * @param L Lua state
 * @param nparams 固定参数数量
 * @param maxstack 函数最大栈深度（变长参数表将存在 maxstack+1）
 */
LUA_API void lua_tcc_prologue(lua_State *L, int nparams, int maxstack) {
    int nargs = lua_gettop(L);
    /* 变长参数 = 总参数 - 固定参数（固定参数从1开始，之后全是变长） */
    int nvarargs = (nargs > nparams) ? nargs - nparams : 0;
    lua_createtable(L, nvarargs, 0);
    if (nvarargs > 0) {
        /* vararg 起始位置: nparams + 1 (固定参数之后的第一个) */
        for (int i = 0; i < nvarargs; i++) {
            lua_pushvalue(L, nparams + 1 + i);
            lua_rawseti(L, -2, i + 1);
        }
    }
    int table_pos = lua_gettop(L);
    int target = maxstack + 1;
    if (table_pos > target) {
        /* table 在栈上超过 target 位置：弹出并放到 target，截断栈 */
        lua_replace(L, target);
        lua_settop(L, target);
    } else if (table_pos < target) {
        /* table 在 target 之前：扩展栈，把 table 搬移到 target，原位置填 nil */
        lua_settop(L, target);
        lua_pushvalue(L, table_pos);
        lua_replace(L, target);
        lua_pushnil(L);
        lua_replace(L, table_pos);
    }
    /* 否则 table_pos == target：已经在正确位置，什么也不做 */
}

LUA_API void lua_tcc_gettabup(lua_State *L, int upval, const char *k, int dest) {
    lua_getfield(L, lua_upvalueindex(upval), k);
    lua_replace(L, dest);
}

LUA_API void lua_tcc_settabup(lua_State *L, int upval, const char *k, int val_idx) {
    /* 功能描述：将值写入环境表（upvalue 指向的 table）的指定字段
     * 参数：upval - upvalue 索引；k - 字段名；val_idx - 值在栈上的位置（允许负数伪索引）
     * 返回：无
     * 修复：移除多余的 lua_pop。lua_setfield 本身会消费栈顶值，
     *       多一次 lua_pop 会导致栈下溢，引发堆损坏（STATUS_HEAP_CORRUPTION）。
     *       流程：pushvalue(+1) -> setfield(-1) -> 栈高度不变。
     */
    lua_pushvalue(L, val_idx);
    lua_setfield(L, lua_upvalueindex(upval), k);
}

LUA_API void lua_tcc_loadk_str(lua_State *L, int dest, const char *s) {
    lua_pushstring(L, s);
    lua_replace(L, dest);
}

LUA_API void lua_tcc_loadk_int(lua_State *L, int dest, lua_Integer v) {
    lua_pushinteger(L, v);
    lua_replace(L, dest);
}

LUA_API void lua_tcc_loadk_flt(lua_State *L, int dest, lua_Number v) {
    lua_pushnumber(L, v);
    lua_replace(L, dest);
}

LUA_API int lua_tcc_in(lua_State *L, int val_idx, int container_idx) {
    int res = 0;
    if (lua_type(L, container_idx) == LUA_TTABLE) {
        lua_pushvalue(L, val_idx);
        lua_gettable(L, container_idx);
        if (!lua_isnil(L, -1)) res = 1;
        lua_pop(L, 1);
    } else if (lua_isstring(L, container_idx) && lua_isstring(L, val_idx)) {
        const char *s = lua_tostring(L, container_idx);
        const char *sub = lua_tostring(L, val_idx);
        if (strstr(s, sub)) res = 1;
    }
    return res;
}

LUA_API void lua_tcc_push_args(lua_State *L, int start_reg, int count) {
    lua_checkstack(L, count);
    for (int i = 0; i < count; i++) {
        lua_pushvalue(L, start_reg + i);
    }
}

LUA_API void lua_tcc_store_results(lua_State *L, int start_reg, int count) {
    for (int i = count - 1; i >= 0; i--) {
        lua_replace(L, start_reg + i);
    }
}

/*
** Map 容器操作包装函数
*/

LUA_API void lua_tcc_newmap(lua_State *L) {
    Map *m = luaM_newmap(L);
    setmapvalue2s(L, L->top.p, m);
    L->top.p++;
}

LUA_API void lua_tcc_mapget(lua_State *L) {
    /* 栈: ... map key → 弹出，压入结果 */
    const TValue *key = s2v(L->top.p - 1);
    const TValue *map_val = s2v(L->top.p - 2);
    if (!ttismap(map_val)) {
        luaG_typeerror(L, map_val, "map");
    }
    const TValue *val = luaM_getval(mapvalue(map_val), key);
    L->top.p -= 2;
    if (val != NULL) {
        setobj2s(L, L->top.p, val);
    } else {
        setnilvalue(s2v(L->top.p));
    }
    L->top.p++;
}

LUA_API void lua_tcc_mapset(lua_State *L) {
    /* 栈: ... map key value → 弹出 */
    const TValue *val = s2v(L->top.p - 1);
    const TValue *key = s2v(L->top.p - 2);
    const TValue *map_val = s2v(L->top.p - 3);
    if (!ttismap(map_val)) {
        luaG_typeerror(L, map_val, "map");
    }
    luaM_setval(L, mapvalue(map_val), key, val);
    L->top.p -= 3;
}

/*
** 表/Map 通用索引包装函数
** 用于 LBCTC 转译生成的 C 代码，支持通用 t[k]/t[k]=v 语法：
**   - 目标是原生 Map → 走 Map 查找/插入（luaM_getval / luaM_setval）
**   - 目标是普通 Table → 走标准 Lua C API（lua_gettable 等）
*/

LUA_API int lua_tcc_gettable(lua_State *L, int idx) {
    /* 入栈状态: ... t ... k (k 栈顶, idx 指向 t) */
    lua_pushvalue(L, idx); /* -> ... k t_copy (顶) */
    if (ttismap(s2v(L->top.p - 1))) {
        Map *m = mapvalue(s2v(L->top.p - 1));
        const TValue *key = s2v(L->top.p - 2); /* k */
        const TValue *val = luaM_getval(m, key);
        L->top.p -= 2; /* pop k + t_copy */
        if (val != NULL) {
            setobj2s(L, L->top.p, val);
        } else {
            setnilvalue(s2v(L->top.p));
        }
        L->top.p++;
        return ttype(s2v(L->top.p - 1));
    }
    lua_pop(L, 1);
    return lua_gettable(L, idx);
}

LUA_API void lua_tcc_settable(lua_State *L, int idx) {
    /* 入栈状态: ... t ... k v (v 栈顶, idx 指向 t) */
    lua_pushvalue(L, idx); /* -> ... k v t_copy (顶) */
    if (ttismap(s2v(L->top.p - 1))) {
        Map *m = mapvalue(s2v(L->top.p - 1));
        const TValue *key = s2v(L->top.p - 3); /* k */
        const TValue *val = s2v(L->top.p - 2); /* v */
        luaM_setval(L, m, key, val);
        L->top.p -= 3; /* pop k + v + t_copy */
        return;
    }
    lua_pop(L, 1);
    lua_settable(L, idx);
}

LUA_API int lua_tcc_getfield(lua_State *L, int idx, const char *k) {
    /* 入栈状态: ... t ... (idx 指向 t); 函数要压入 t[k] */
    lua_pushvalue(L, idx); /* -> ... t_copy (顶) */
    if (ttismap(s2v(L->top.p - 1))) {
        Map *m = mapvalue(s2v(L->top.p - 1));
        TValue key;
        setsvalue(L, &key, luaS_new(L, k));
        const TValue *val = luaM_getval(m, &key);
        L->top.p--; /* pop t_copy */
        if (val != NULL) {
            setobj2s(L, L->top.p, val);
        } else {
            setnilvalue(s2v(L->top.p));
        }
        L->top.p++;
        return ttype(s2v(L->top.p - 1));
    }
    lua_pop(L, 1);
    return lua_getfield(L, idx, k);
}

LUA_API void lua_tcc_setfield(lua_State *L, int idx, const char *k) {
    /* 入栈状态: ... t ... v (v 栈顶, idx 指向 t) */
    lua_pushvalue(L, idx); /* -> ... v t_copy (顶) */
    if (ttismap(s2v(L->top.p - 1))) {
        Map *m = mapvalue(s2v(L->top.p - 1));
        const TValue *val = s2v(L->top.p - 2); /* v */
        TValue key;
        setsvalue(L, &key, luaS_new(L, k));
        luaM_setval(L, m, &key, val);
        L->top.p -= 2; /* pop v + t_copy */
        return;
    }
    lua_pop(L, 1);
    lua_setfield(L, idx, k);
}

LUA_API int lua_tcc_geti(lua_State *L, int idx, lua_Integer n) {
    /* 入栈状态: ... t ... (idx 指向 t); 函数要压入 t[n] */
    lua_pushvalue(L, idx); /* -> ... t_copy (顶) */
    if (ttismap(s2v(L->top.p - 1))) {
        Map *m = mapvalue(s2v(L->top.p - 1));
        TValue key;
        setivalue(&key, n);
        const TValue *val = luaM_getval(m, &key);
        L->top.p--; /* pop t_copy */
        if (val != NULL) {
            setobj2s(L, L->top.p, val);
        } else {
            setnilvalue(s2v(L->top.p));
        }
        L->top.p++;
        return ttype(s2v(L->top.p - 1));
    }
    lua_pop(L, 1);
    return lua_geti(L, idx, n);
}

LUA_API void lua_tcc_seti(lua_State *L, int idx, lua_Integer n) {
    /* 入栈状态: ... t ... v (v 栈顶, idx 指向 t) */
    lua_pushvalue(L, idx); /* -> ... v t_copy (顶) */
    if (ttismap(s2v(L->top.p - 1))) {
        Map *m = mapvalue(s2v(L->top.p - 1));
        const TValue *val = s2v(L->top.p - 2); /* v */
        TValue key;
        setivalue(&key, n);
        luaM_setval(L, m, &key, val);
        L->top.p -= 2; /* pop v + t_copy */
        return;
    }
    lua_pop(L, 1);
    lua_seti(L, idx, n);
}

/*
** Trait 操作包装函数
*/

LUA_API void lua_tcc_settraitflag(lua_State *L, int idx) {
    lua_pushvalue(L, idx);
    luaC_settraitflag(L, -1);
    lua_pop(L, 1);
}

LUA_API void lua_tcc_settraitrequire(lua_State *L, int idx, const char *name, int nparams) {
    lua_pushvalue(L, idx);
    TString *ts = luaS_new(L, name);
    luaC_settraitrequire(L, -1, ts, nparams);
    lua_pop(L, 1);
}

LUA_API void lua_tcc_usetrait(lua_State *L, int class_idx, int trait_idx) {
    lua_pushvalue(L, class_idx);
    lua_pushvalue(L, trait_idx);
    luaC_usetrait(L, -2, -1);
    lua_pop(L, 2);
}

/*
** 静态构造函数包装函数
*/
LUA_API void lua_tcc_staticinit(lua_State *L, int class_idx) {
    /* 查找 class.__statics.init 并调用 */
    lua_pushvalue(L, class_idx);
    lua_pushstring(L, "__statics");
    lua_rawget(L, -2);
    if (lua_istable(L, -1)) {
        lua_pushstring(L, "init");
        lua_rawget(L, -2);
        if (lua_isfunction(L, -1)) {
            lua_call(L, 0, 0);
        } else {
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 2);
}

/*
** 表合并操作包装函数
*/

LUA_API void lua_tcc_merge(lua_State *L) {
    /* 栈: ... t1 t2 → 弹出，压入合并结果 */
    if (!lua_istable(L, -2) || !lua_istable(L, -1)) {
        luaL_error(L, "attempt to merge non-table values");
    }
    lua_newtable(L);  /* 创建结果表 */
    lua_insert(L, -3);  /* 移到 t1 下面 */
    int result_idx = lua_gettop(L) - 2;
    /* 复制 t1 */
    lua_pushnil(L);
    while (lua_next(L, result_idx + 1) != 0) {
        lua_pushvalue(L, -2);  /* 复制 key */
        lua_pushvalue(L, -2);  /* 复制 value */
        lua_rawset(L, result_idx);
        lua_pop(L, 1);  /* 弹出 value，保留 key */
    }
    /* 复制 t2（覆盖同名键） */
    lua_pushnil(L);
    while (lua_next(L, result_idx + 2) != 0) {
        lua_pushvalue(L, -2);
        lua_pushvalue(L, -2);
        lua_rawset(L, result_idx);
        lua_pop(L, 1);
    }
    lua_pop(L, 2);  /* 弹出 t1 和 t2 */
    /* 结果表留在栈顶 */
}

/*
** 正则字面量操作包装函数
*/

LUA_API void lua_tcc_regex(lua_State *L, const char *data) {
    /* data 格式: "pattern\0flags" */
    size_t patlen = strlen(data);
    const char *flags = data + patlen + 1;
    lua_newtable(L);
    lua_pushlstring(L, data, patlen);
    lua_setfield(L, -2, "pattern");
    lua_pushstring(L, flags);
    lua_setfield(L, -2, "flags");
    /* 结果表留在栈顶 */
}

/*
** Async/Await 操作包装函数
*/

LUA_API void lua_tcc_await(lua_State *L, int val_idx, int dest) {
    lua_pushvalue(L, val_idx);
    /* 检查是否为 Promise userdata */
    if (lua_type(L, -1) == LUA_TUSERDATA) {
        if (lua_getmetatable(L, -1)) {
            lua_getfield(L, -1, "__name");
            if (lua_isstring(L, -1)) {
                const char *name = lua_tostring(L, -1);
                if (name && strcmp(name, "Promise") == 0) {
                    lua_pop(L, 2);  /* 弹出 __name 和元表 */
                    promise **pp = (promise **)lua_touserdata(L, -1);
                    if (pp && *pp) {
                        promise *p = *pp;
                        /* 同步等待 Promise 完成 */
                        if (promise_await_sync(p, L, -1) == 0) {
                            /* 结果在栈顶，移到 dest */
                            lua_replace(L, dest);
                            return;
                        }
                    }
                    /* Promise 失败或超时，压入 nil */
                    lua_pop(L, 1);
                    lua_pushnil(L);
                    lua_replace(L, dest);
                    return;
                }
            }
            lua_pop(L, 2);  /* 弹出 __name 和元表 */
        }
    }
    /* 普通值：直接复制到 dest */
    lua_replace(L, dest);
}

/*
** 自定义 Opcode 操作包装函数
*/

LUA_API int lua_tcc_custom(lua_State *L, int opcode) {
    global_State *g = G(L);
    if (opcode < OP_CUSTOM_COUNT && g->custom_op_handlers[opcode] != NULL) {
        return g->custom_op_handlers[opcode](L);
    }
    return 0;
}

/*
** Interface Obfuscation Support
*/

/* Helper X-macros to generate API lists */
#define X(name, ret, args) name,
static const void *tcc_api_funcs[] = {
#include "lbctc_api_list.h"
};
#undef X

#define X(name, ret, args) #name,
static const char *tcc_api_names[] = {
#include "lbctc_api_list.h"
};
#undef X

static const int tcc_api_count = sizeof(tcc_api_funcs) / sizeof(tcc_api_funcs[0]);

/* Simple LCG for deterministic shuffling */
static int my_rand(unsigned int *seed) {
    *seed = (*seed * 1103515245 + 12345) & 0x7fffffff;
    return *seed;
}

/* Helper to generate a random C-safe name based on seed */
static void get_random_name(char *out, size_t len, unsigned int *seed) {
    static const char chars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    for (size_t i = 0; i < len - 1; i++) {
        out[i] = chars[my_rand(seed) % (sizeof(chars) - 1)];
    }
    out[len - 1] = '\0';
}

/* Helper to format string and add to buffer */
static void add_fmt(luaL_Buffer *B, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buffer[4096];
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    luaL_addstring(B, buffer);
}

/* Helper to get an obfuscated integer expression as a string */
static const char *obf_int(int val, unsigned int *seed, int obfuscate) {
    static char bufs[32][128];
    static int idx = 0;
    char *buf = bufs[idx];
    idx = (idx + 1) % 32;
    if (!obfuscate) {
        snprintf(buf, 128, "%d", val);
    } else {
        int r = my_rand(seed) % 0x7FFF;
        int op = my_rand(seed) % 4;
        switch (op) {
            case 0: snprintf(buf, 128, "((%d + %d) - %d)", val, r, r); break;
            case 1: snprintf(buf, 128, "((%d - %d) + %d)", val, r, r); break;
            case 2: snprintf(buf, 128, "((%d ^ %d) ^ %d)", val, r, r); break;
            case 3: snprintf(buf, 128, "((%d * 2) - %d)", val, val); break;
        }
    }
    return buf;
}

/* Helper to emit harmless junk code */
static void emit_junk_code(luaL_Buffer *B, unsigned int *seed) {
    int op = my_rand(seed) % 5;
    char name[10];
    get_random_name(name, sizeof(name), seed);
    switch (op) {
        case 0: add_fmt(B, "    int %s = %d;\n", name, my_rand(seed) % 100); break;
        case 1: add_fmt(B, "    if (%d == %d) { /* dummy */ }\n", my_rand(seed) % 10, my_rand(seed) % 10); break;
        case 2: add_fmt(B, "    { int %s = %d; %s++; }\n", name, my_rand(seed) % 100, name); break;
        case 3: add_fmt(B, "    (void)%d;\n", my_rand(seed)); break;
        case 4: add_fmt(B, "    { int x = %d; int y = x * 2; (void)y; }\n", my_rand(seed) % 10); break;
    }
}

static void get_label_name(char *out, size_t len, int label_idx, unsigned int seed, int obfuscate) {
    if (obfuscate) {
        unsigned int label_seed = seed + label_idx + 1000000;
        get_random_name(out, len, &label_seed);
    } else {
        snprintf(out, len, "Label_%d", label_idx);
    }
}

LUA_API void *lua_tcc_get_interface(lua_State *L, int seed) {
    /* Allocate array of function pointers */
    void **iface = (void **)lua_newuserdatauv(L, tcc_api_count * sizeof(void *), 0);

    /* Create indices array */
    int *indices = (int *)malloc(tcc_api_count * sizeof(int));
    if (!indices) return NULL;
    for (int i = 0; i < tcc_api_count; i++) indices[i] = i;

    /* Shuffle indices using the seed */
    unsigned int useed = (unsigned int)seed;
    for (int i = tcc_api_count - 1; i > 0; i--) {
        int j = my_rand(&useed) % (i + 1);
        int temp = indices[i];
        indices[i] = indices[j];
        indices[j] = temp;
    }

    /* Populate interface */
    for (int i = 0; i < tcc_api_count; i++) {
        iface[indices[i]] = (void *)tcc_api_funcs[i];
    }

    free(indices);
    return iface;
}

/* Recursive function to collect all protos and assign IDs */
typedef struct ProtoInfo {
    Proto *p;
    int id;
    char name[16];
    int *boxed_slots;    /* 哪些局部变量槽位被装箱了（大小 = maxstacksize + 1） */
    int *boxed_upvals;   /* 哪些 upvalue 被装箱了（大小 = sizeupvalues） */
    int maxstack;        /* proto 的 maxstacksize */
} ProtoInfo;

static void collect_protos(Proto *p, int *count, ProtoInfo **list, int *capacity, unsigned int *seed, int obfuscate) {
    if (*count >= *capacity) {
        *capacity *= 2;
        *list = (ProtoInfo *)realloc(*list, *capacity * sizeof(ProtoInfo));
    }
    (*list)[*count].p = p;
    (*list)[*count].id = *count;
    if (obfuscate) {
        get_random_name((*list)[*count].name, sizeof((*list)[*count].name), seed);
    } else {
        snprintf((*list)[*count].name, sizeof((*list)[*count].name), "function_%d", *count);
    }
    (*count)++;

    for (int i = 0; i < p->sizep; i++) {
        collect_protos(p->p[i], count, list, capacity, seed, obfuscate);
    }
}

static int get_proto_id(Proto *p, ProtoInfo *list, int count) {
    for (int i = 0; i < count; i++) {
        if (list[i].p == p) return list[i].id;
    }
    return -1;
}

/* Helper to emit encrypted string push */
static void emit_encrypted_string_push(luaL_Buffer *B, const char *s, size_t len, int seed) {
    if (len == 0) {
        add_fmt(B, "    lua_pushlstring(L, \"\", 0);\n");
        return;
    }
    // Derive a timestamp/key deterministically
    unsigned int timestamp = (unsigned int)seed ^ (unsigned int)len ^ 0x5A5A5A5A;
    for(size_t i=0; i<len; i++) timestamp = (timestamp * 1664525) + (unsigned char)s[i] + 1013904223;

    // Encrypt
    unsigned char *cipher = (unsigned char *)malloc(len);
    for (size_t i = 0; i < len; i++) {
        cipher[i] = s[i] ^ ((timestamp + i) & 0xFF);
    }

    // Emit C code
    add_fmt(B, "    {\n");
    add_fmt(B, "        static const unsigned char cipher[] = {");
    for (size_t i = 0; i < len; i++) {
        add_fmt(B, "0x%02x,", cipher[i]);
    }
    add_fmt(B, "};\n");
    add_fmt(B, "        lua_tcc_decrypt_string(L, cipher, %llu, %u);\n", (unsigned long long)len, timestamp);
    add_fmt(B, "    }\n");

    free(cipher);
}

/* Helper to escape and emit a string literal */
static void emit_quoted_string(luaL_Buffer *B, const char *s, size_t len) {
    add_fmt(B, "\"");
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"' || c == '\\' || c == '\n' || c == '\r' || c == '\t') {
            add_fmt(B, "\\%c", c == '\n' ? 'n' : (c == '\r' ? 'r' : (c == '\t' ? 't' : c)));
        } else if (c < 32 || c > 126) {
            add_fmt(B, "\\x%02x", c);
        } else {
            luaL_addchar(B, c);
        }
    }
    add_fmt(B, "\"");
}

/* Emit code to push a constant */
static void emit_loadk(luaL_Buffer *B, Proto *p, int k_index, int str_encrypt, int seed, int obfuscate) {
    /* 边界检查：防止 k_index 越界访问常量表 */
    if (k_index < 0 || k_index >= p->sizek) {
        add_fmt(B, "    lua_pushnil(L); /* INVALID CONSTANT INDEX %d */\n", k_index);
        return;
    }
    TValue *k = &p->k[k_index];
    unsigned int obf_seed = seed + k_index;
    switch (ttype(k)) {
        case LUA_TNIL:
            add_fmt(B, "    lua_pushnil(L);\n");
            break;
        case LUA_TBOOLEAN:
            add_fmt(B, "    lua_pushboolean(L, %s);\n", obf_int(!l_isfalse(k), &obf_seed, obfuscate));
            break;
        case LUA_TNUMBER:
            if (ttisinteger(k)) {
                add_fmt(B, "    lua_pushinteger(L, %s);\n", obf_int((int)ivalue(k), &obf_seed, obfuscate));
            } else {
                add_fmt(B, "    lua_pushnumber(L, %f);\n", fltvalue(k));
            }
            break;
        case LUA_TSTRING: {
            TString *ts = tsvalue(k);
            if (str_encrypt) {
                emit_encrypted_string_push(B, getstr(ts), tsslen(ts), seed);
            } else {
                add_fmt(B, "    lua_pushlstring(L, ");
                emit_quoted_string(B, getstr(ts), tsslen(ts));
                add_fmt(B, ", %s);\n", obf_int((int)tsslen(ts), &obf_seed, obfuscate));
            }
            break;
        }
        default:
            add_fmt(B, "    lua_pushnil(L); /* UNKNOWN CONSTANT TYPE */\n");
            break;
    }
}

/* 检查子 Proto 中指定 upvalue 是否被 SETUPVAL 修改（递归检查所有孙辈闭包）
** 递归逻辑：
**   1. 当前 proto 存在 SETUPVAL B=upval_idx → 修改
**   2. 当前 proto 的子 proto child（通过 CLOSURE 生成）
**      - 若 child 的 upvalue j 指向当前 proto 的 upval_idx（即 !instack && uv.idx == upval_idx）
**        且 is_upval_modified(child, j) 为真 → 修改
**   3. 若 child 的 upvalue j 指向当前 proto 的局部栈槽位（instack=true && uv.idx=slot）
**        且 is_upval_modified(child, j) 为真 → 不影响 upvalue（但局部slot需要boxed，已有逻辑处理）
*/
static int is_upval_modified(Proto *p, int upval_idx) {
    for (int i = 0; i < p->sizecode; i++) {
        if (GET_OPCODE(p->code[i]) == OP_SETUPVAL && GETARG_B(p->code[i]) == upval_idx) {
            return 1;
        }
    }
    /* 递归检查子 proto：如果子 proto 间接修改本 proto 的 upvalue */
    for (int i = 0; i < p->sizep; i++) {
        Proto *child = p->p[i];
        for (int j = 0; j < child->sizeupvalues; j++) {
            Upvaldesc *uv = &child->upvalues[j];
            if (!uv->instack && uv->idx == upval_idx) {
                /* child 的 upvalue j 对应本 proto 的 upvalue upval_idx */
                if (is_upval_modified(child, j)) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

/* 读取局部变量（自动处理 boxed 变量） */
static void emit_read_local(luaL_Buffer *B, int slot, int *boxed_slots, unsigned int *obf_seed, int obfuscate) {
    if (boxed_slots && boxed_slots[slot]) {
        add_fmt(B, "    lua_getfield(L, %s, \"_v\");\n", obf_int(slot, obf_seed, obfuscate));
    } else {
        add_fmt(B, "    lua_pushvalue(L, %s);\n", obf_int(slot, obf_seed, obfuscate));
    }
}

/* 写入局部变量（自动处理 boxed 变量） */
static void emit_write_local(luaL_Buffer *B, int slot, int *boxed_slots, unsigned int *obf_seed, int obfuscate) {
    if (boxed_slots && boxed_slots[slot]) {
        add_fmt(B, "    lua_setfield(L, %s, \"_v\");\n", obf_int(slot, obf_seed, obfuscate));
    } else {
        add_fmt(B, "    lua_replace(L, %s);\n", obf_int(slot, obf_seed, obfuscate));
    }
}

static void emit_instruction(luaL_Buffer *B, Proto *p, int pc, Instruction i, ProtoInfo *protos, int proto_count, int use_pure_c, int str_encrypt, int seed, int obfuscate, int *boxed_slots, int *boxed_upvals) {
    OpCode op = GET_OPCODE(i);
    int a = GETARG_A(i);

    char label_name[16];
    get_label_name(label_name, sizeof(label_name), pc + 1, seed, obfuscate);
    add_fmt(B, "    %s: /* %s */\n", label_name, opnames[op]);

    unsigned int obf_seed = (unsigned int)seed + pc;

    switch (op) {
        case OP_MOVE: {
            int b = GETARG_B(i);
            emit_read_local(B, b + 1, boxed_slots, &obf_seed, obfuscate);
            emit_write_local(B, a + 1, boxed_slots, &obf_seed, obfuscate);
            break;
        }
        case OP_LOADK: {
            int bx = GETARG_Bx(i);
            TValue *k = &p->k[bx];
            /* 修复：boxedslot 标记为真时，不能使用 lua_tcc_loadk_xxx helper（它们内部是 lua_replace，
             * 会覆盖 box table 本身）。统一先 push 值到栈顶，再用 emit_write_local（写 _v 字段）。 */
            if (boxed_slots && boxed_slots[a + 1]) {
                if (ttisstring(k)) {
                     if (str_encrypt) {
                         emit_encrypted_string_push(B, getstr(tsvalue(k)), tsslen(tsvalue(k)), seed);
                     } else {
                         add_fmt(B, "    lua_pushlstring(L, ");
                         emit_quoted_string(B, getstr(tsvalue(k)), tsslen(tsvalue(k)));
                         add_fmt(B, ", %lld);\n", (long long)tsslen(tsvalue(k)));
                     }
                } else if (ttisinteger(k)) {
                     add_fmt(B, "    lua_pushinteger(L, %lld);\n", (long long)ivalue(k));
                } else if (ttisnumber(k)) {
                     add_fmt(B, "    lua_pushnumber(L, %f);\n", fltvalue(k));
                } else {
                     emit_loadk(B, p, bx, str_encrypt, seed, obfuscate);
                }
                emit_write_local(B, a + 1, boxed_slots, &obf_seed, obfuscate);
            } else {
                if (ttisstring(k)) {
                     if (str_encrypt) {
                         emit_encrypted_string_push(B, getstr(tsvalue(k)), tsslen(tsvalue(k)), seed);
                         add_fmt(B, "    lua_replace(L, %s);\n", obf_int(a + 1, &obf_seed, obfuscate));
                     } else {
                         add_fmt(B, "    lua_tcc_loadk_str(L, %s, ", obf_int(a + 1, &obf_seed, obfuscate));
                         emit_quoted_string(B, getstr(tsvalue(k)), tsslen(tsvalue(k)));
                         add_fmt(B, ");\n");
                     }
                } else if (ttisinteger(k)) {
                     add_fmt(B, "    lua_tcc_loadk_int(L, %s, %lld);\n", obf_int(a + 1, &obf_seed, obfuscate), (long long)ivalue(k));
                } else if (ttisnumber(k)) {
                     add_fmt(B, "    lua_tcc_loadk_flt(L, %s, %f);\n", obf_int(a + 1, &obf_seed, obfuscate), fltvalue(k));
                } else {
                     emit_loadk(B, p, bx, str_encrypt, seed, obfuscate);
                     add_fmt(B, "    lua_replace(L, %s);\n", obf_int(a + 1, &obf_seed, obfuscate));
                }
            }
            break;
        }
        case OP_LOADI: {
            int sbx = GETARG_sBx(i);
            add_fmt(B, "    lua_pushinteger(L, %d);\n", sbx);
            emit_write_local(B, a + 1, boxed_slots, &obf_seed, obfuscate);
            break;
        }
         case OP_LOADF: {
            int sbx = GETARG_sBx(i);
            add_fmt(B, "    lua_pushnumber(L, (lua_Number)%d);\n", sbx);
            emit_write_local(B, a + 1, boxed_slots, &obf_seed, obfuscate);
            break;
        }
        case OP_LOADNIL: {
            int b = GETARG_B(i);
            /* 修复：循环内若存在 boxed slot，不能统一用 lua_replace。
             * 检查范围内 slot：全部未 boxed → 原高效 for 循环；有任一 boxed → 逐条展开。 */
            int has_boxed = 0;
            if (boxed_slots) {
                for (int _i = 0; _i <= b; _i++) {
                    if (boxed_slots[a + 1 + _i]) { has_boxed = 1; break; }
                }
            }
            if (has_boxed) {
                for (int _i = 0; _i <= b; _i++) {
                    add_fmt(B, "    lua_pushnil(L);\n");
                    emit_write_local(B, a + 1 + _i, boxed_slots, &obf_seed, obfuscate);
                }
            } else {
                add_fmt(B, "    for (int i = 0; i <= %s; i++) {\n", obf_int(b, &obf_seed, obfuscate));
                add_fmt(B, "        lua_pushnil(L);\n");
                add_fmt(B, "        lua_replace(L, %s + i);\n", obf_int(a + 1, &obf_seed, obfuscate));
                add_fmt(B, "    }\n");
            }
            break;
        }
        case OP_LOADFALSE:
            add_fmt(B, "    lua_pushboolean(L, 0);\n");
            /* 修复：boxed slot 写 _v */
            emit_write_local(B, a + 1, boxed_slots, &obf_seed, obfuscate);
            break;
        case OP_LFALSESKIP: {
            /*
             * LFALSESKIP: 无条件把 R[A] 设为 false，并跳过下一条指令
             * 对应 lvm.c: setbfvalue(s2v(ra)); pc++;
             * 下一条通常是 LOADTRUE，skip 之后保留 false 值
             * 跳转目标 = pc + 2 (跳过下一条 opcode)，传 get_label_name = (pc+2) + 1 = pc + 3
             */
            char target_label[16];
            get_label_name(target_label, sizeof(target_label), pc + 3, seed, obfuscate);
            add_fmt(B, "    lua_pushboolean(L, 0);\n");
            /* 修复：boxed slot 写 _v */
            emit_write_local(B, a + 1, boxed_slots, &obf_seed, obfuscate);
            add_fmt(B, "    goto %s;\n", target_label);
            break;
        }
        case OP_LOADTRUE:
            add_fmt(B, "    lua_pushboolean(L, 1);\n");
            /* 修复：boxed slot 写 _v */
            emit_write_local(B, a + 1, boxed_slots, &obf_seed, obfuscate);
            break;

        case OP_GETUPVAL: {
            int b = GETARG_B(i);
            /* 修复：源 boxed_upval 解 _v（已有逻辑）；
             * 目标 slot a+1 若 boxed，走 emit_write_local，否则 lua_replace。
             * 否则若 a+1 是 boxed slot，lua_replace 直接覆盖 box table，
             * 导致其他引用该 box 的闭包观察不到值更新。 */
            if (boxed_upvals && boxed_upvals[b]) {
                /* boxed upvalue：读取 box table 的 _v 字段 */
                add_fmt(B, "    lua_getfield(L, lua_upvalueindex(%s), \"_v\");\n", obf_int(b + 1, &obf_seed, obfuscate));
            } else {
                add_fmt(B, "    lua_pushvalue(L, lua_upvalueindex(%s));\n", obf_int(b + 1, &obf_seed, obfuscate));
            }
            emit_write_local(B, a + 1, boxed_slots, &obf_seed, obfuscate);
            break;
        }
        case OP_LOADKX: {
            if (pc + 1 < p->sizecode && GET_OPCODE(p->code[pc+1]) == OP_EXTRAARG) {
                int ax = GETARG_Ax(p->code[pc+1]);
                TValue *k = &p->k[ax];
                /* 修复：boxedslot 走 lua_push + emit_write_local（写 _v），不走 helper/lua_replace */
                if (boxed_slots && boxed_slots[a + 1]) {
                    if (ttisstring(k)) {
                         if (str_encrypt) emit_encrypted_string_push(B, getstr(tsvalue(k)), tsslen(tsvalue(k)), seed);
                         else {
                             add_fmt(B, "    lua_pushlstring(L, ");
                             emit_quoted_string(B, getstr(tsvalue(k)), tsslen(tsvalue(k)));
                             add_fmt(B, ", %lld);\n", (long long)tsslen(tsvalue(k)));
                         }
                    } else if (ttisinteger(k)) {
                         add_fmt(B, "    lua_pushinteger(L, %lld);\n", (long long)ivalue(k));
                    } else {
                         emit_loadk(B, p, ax, str_encrypt, seed, obfuscate);
                    }
                    emit_write_local(B, a + 1, boxed_slots, &obf_seed, obfuscate);
                } else {
                    if (ttisstring(k)) {
                         if (str_encrypt) {
                             emit_encrypted_string_push(B, getstr(tsvalue(k)), tsslen(tsvalue(k)), seed);
                             add_fmt(B, "    lua_replace(L, %s);\n", obf_int(a + 1, &obf_seed, obfuscate));
                         } else {
                             add_fmt(B, "    lua_tcc_loadk_str(L, %s, ", obf_int(a + 1, &obf_seed, obfuscate));
                             emit_quoted_string(B, getstr(tsvalue(k)), tsslen(tsvalue(k)));
                             add_fmt(B, ");\n");
                         }
                    } else if (ttisinteger(k)) {
                         add_fmt(B, "    lua_tcc_loadk_int(L, %s, %lld);\n", obf_int(a + 1, &obf_seed, obfuscate), (long long)ivalue(k));
                    } else {
                         emit_loadk(B, p, ax, str_encrypt, seed, obfuscate);
                         add_fmt(B, "    lua_replace(L, %s);\n", obf_int(a + 1, &obf_seed, obfuscate));
                    }
                }
            }
            break;
        }
        case OP_SETUPVAL: {
            int b = GETARG_B(i);
            if (boxed_upvals && boxed_upvals[b]) {
                /* boxed upvalue：设置 box table 的 _v 字段 */
                add_fmt(B, "    lua_pushvalue(L, %s);\n", obf_int(a + 1, &obf_seed, obfuscate));
                add_fmt(B, "    lua_setfield(L, lua_upvalueindex(%s), \"_v\");\n", obf_int(b + 1, &obf_seed, obfuscate));
            } else {
                add_fmt(B, "    lua_pushvalue(L, %s);\n", obf_int(a + 1, &obf_seed, obfuscate));
                add_fmt(B, "    lua_replace(L, lua_upvalueindex(%s));\n", obf_int(b + 1, &obf_seed, obfuscate));
            }
            break;
        }
        case OP_GETTABUP: { // R[A] := UpValue[B][K[C]]
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            TValue *k = &p->k[c];
            /* 修复：目标 slot a+1 若 boxed，不得使用 lua_tcc_gettabup helper / lua_replace
             * （两者都会直接 lua_replace 覆盖 box table）。
             * 改为：把结果取到栈顶后统一走 emit_write_local(a+1)。
             * 非字符串 key 的分支同样修复。 */
            if (ttisstring(k)) {
                 if (str_encrypt) {
                     emit_encrypted_string_push(B, getstr(tsvalue(k)), tsslen(tsvalue(k)), seed);
                     add_fmt(B, "    lua_getfield(L, lua_upvalueindex(%s), lua_tostring(L, %s));\n", obf_int(b + 1, &obf_seed, obfuscate), obf_int(-1, &obf_seed, obfuscate));
                     /* 结果在 -2；先把它移到栈顶，再 emit_write_local(a+1)，然后 pop 加密 key */
                     add_fmt(B, "    lua_insert(L, %s);\n", obf_int(-2, &obf_seed, obfuscate)); /* 把结果挪到 -2 (key 在 -1) */
                     add_fmt(B, "    lua_pop(L, %s);\n", obf_int(1, &obf_seed, obfuscate)); /* 现在 -1 是结果，pop key */
                     emit_write_local(B, a + 1, boxed_slots, &obf_seed, obfuscate);
                 } else {
                     if (boxed_slots && boxed_slots[a + 1]) {
                         /* dest boxed：内联实现不走 lua_tcc_gettabup（它内部 lua_replace 会覆盖 box） */
                         add_fmt(B, "    lua_getfield(L, lua_upvalueindex(%s), ", obf_int(b + 1, &obf_seed, obfuscate));
                         emit_quoted_string(B, getstr(tsvalue(k)), tsslen(tsvalue(k)));
                         add_fmt(B, ");\n"); /* 结果在栈顶 */
                         emit_write_local(B, a + 1, boxed_slots, &obf_seed, obfuscate);
                     } else {
                         add_fmt(B, "    lua_tcc_gettabup(L, %s, ", obf_int(b + 1, &obf_seed, obfuscate));
                         emit_quoted_string(B, getstr(tsvalue(k)), tsslen(tsvalue(k)));
                         add_fmt(B, ", %s);\n", obf_int(a + 1, &obf_seed, obfuscate));
                     }
                 }
            } else {
                 add_fmt(B, "    lua_pushvalue(L, lua_upvalueindex(%s));\n", obf_int(b + 1, &obf_seed, obfuscate)); // table
                 emit_loadk(B, p, c, str_encrypt, seed, obfuscate); // key
                 add_fmt(B, "    lua_gettable(L, %s);\n", obf_int(-2, &obf_seed, obfuscate));
                 /* 此时栈：table (底) -> key -> result (顶)；需要：table -> result (写回 a+1) -> pop table */
                 add_fmt(B, "    lua_remove(L, %s);\n", obf_int(-2, &obf_seed, obfuscate)); /* 删 key，顶=result */
                 emit_write_local(B, a + 1, boxed_slots, &obf_seed, obfuscate); /* 写 _v / replace */
                 add_fmt(B, "    lua_pop(L, %s);\n", obf_int(1, &obf_seed, obfuscate)); // pop table
            }
            break;
        }
        case OP_SETTABUP: { // UpValue[A][K[B]] := RK(C)
            // A is upval index
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            TValue *k = &p->k[b];
            /* 修复：value 来自本地寄存器 R[C]（TESTARG_k==false）时，若 C+1 slot boxed，
             * 必须先解 _v 再 push（走 emit_read_local）。否则把 box table 作为值写入 _ENV 字段，
             * 后续读回变成 table，造成 concat/arithmetic 类型错误或崩溃。 */
            if (ttisstring(k)) {
                if (str_encrypt) {
                    emit_encrypted_string_push(B, getstr(tsvalue(k)), tsslen(tsvalue(k)), seed);
                    // RK(C)
                    if (TESTARG_k(i)) {
                        emit_loadk(B, p, c, str_encrypt, seed, obfuscate);
                    } else {
                        emit_read_local(B, c + 1, boxed_slots, &obf_seed, obfuscate);
                    }
                    add_fmt(B, "    lua_setfield(L, lua_upvalueindex(%s), lua_tostring(L, %s));\n", obf_int(a + 1, &obf_seed, obfuscate), obf_int(-2, &obf_seed, obfuscate));
                    add_fmt(B, "    lua_pop(L, %s);\n", obf_int(1, &obf_seed, obfuscate)); // pop decrypted key
                } else {
                    // RK(C)
                    if (TESTARG_k(i)) {
                        emit_loadk(B, p, c, str_encrypt, seed, obfuscate);
                    } else {
                        emit_read_local(B, c + 1, boxed_slots, &obf_seed, obfuscate);
                    }
                    add_fmt(B, "    lua_tcc_settabup(L, %s, ", obf_int(a + 1, &obf_seed, obfuscate));
                    emit_quoted_string(B, getstr(tsvalue(k)), tsslen(tsvalue(k)));
                    add_fmt(B, ", %s);\n", obf_int(-1, &obf_seed, obfuscate));
                    /* 修复：emit_loadk/emit_read_local 将 value 推到栈顶 (+1)；
                     * lua_tcc_settabup 内部 pushvalue(val_idx)+setfield 消费复制版，
                     * 栈顶仍保留原 value，必须 pop 1 清理，
                     * 否则每次 SETTABUP 净栈 +1 泄漏，累积引发 STATUS_HEAP_CORRUPTION 堆损坏。 */
                    add_fmt(B, "    lua_pop(L, %s);\n", obf_int(1, &obf_seed, obfuscate));
                }
            } else {
                add_fmt(B, "    lua_pushvalue(L, lua_upvalueindex(%s));\n", obf_int(a + 1, &obf_seed, obfuscate)); // table
                emit_loadk(B, p, b, str_encrypt, seed, obfuscate); // key
                // RK(C)
                if (TESTARG_k(i)) {
                    emit_loadk(B, p, c, str_encrypt, seed, obfuscate);
                } else {
                    emit_read_local(B, c + 1, boxed_slots, &obf_seed, obfuscate);
                }
                add_fmt(B, "    lua_settable(L, %s);\n", obf_int(-3, &obf_seed, obfuscate));
                add_fmt(B, "    lua_pop(L, %s);\n", obf_int(1, &obf_seed, obfuscate)); // pop table
            }
            break;
        }

        // Arithmetic
        case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_IDIV:
        case OP_MOD: case OP_POW: case OP_BAND: case OP_BOR: case OP_BXOR:
        case OP_SHL: case OP_SHR: {
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            /* 修复：源操作数 R[B]/R[C] 任一 boxed 时，必须先解 _v 再参与计算；
             * 目标 R[A] boxed 时必须写 _v 不直接 replace。
             * 统一策略：用 emit_read_local 把 b、c 入栈（自动解 _v），
             * 纯 C 路径从栈顶 -1/-2 取数（而非寄存器编号），避免 boxed 寄存器传错。 */
            if (use_pure_c) {
                const char *op_str = NULL;
                int is_int = 0;
                int is_pow = 0;
                if (op == OP_ADD) op_str = "+";
                else if (op == OP_SUB) op_str = "-";
                else if (op == OP_MUL) op_str = "*";
                else if (op == OP_DIV) op_str = "/";
                else if (op == OP_IDIV) { op_str = "/"; is_int = 1; }
                else if (op == OP_MOD) { op_str = "%"; is_int = 1; }
                else if (op == OP_POW) is_pow = 1;
                else if (op == OP_BAND) { op_str = "&"; is_int = 1; }
                else if (op == OP_BOR) { op_str = "|"; is_int = 1; }
                else if (op == OP_BXOR) { op_str = "^"; is_int = 1; }
                else if (op == OP_SHL) { op_str = "<<"; is_int = 1; }
                else if (op == OP_SHR) { op_str = ">>"; is_int = 1; }

                /* 先把 b 和 c 通过 emit_read_local 推到栈顶（自动解 _v），栈顶 -2=b，-1=c */
                emit_read_local(B, b + 1, boxed_slots, &obf_seed, obfuscate);
                emit_read_local(B, c + 1, boxed_slots, &obf_seed, obfuscate);
                if (is_pow) {
                    add_fmt(B, "    lua_pushnumber(L, pow(lua_tonumber(L, %s), lua_tonumber(L, %s)));\n", obf_int(-2, &obf_seed, obfuscate), obf_int(-1, &obf_seed, obfuscate));
                } else if (is_int) {
                    add_fmt(B, "    lua_pushinteger(L, (lua_Integer)lua_tointeger(L, %s) %s (lua_Integer)lua_tointeger(L, %s));\n", obf_int(-2, &obf_seed, obfuscate), op_str, obf_int(-1, &obf_seed, obfuscate));
                } else {
                    add_fmt(B, "    lua_pushnumber(L, (lua_Number)lua_tonumber(L, %s) %s (lua_Number)lua_tonumber(L, %s));\n", obf_int(-2, &obf_seed, obfuscate), op_str, obf_int(-1, &obf_seed, obfuscate));
                }
                /* 弹出 b、c 两个临时值，栈顶是结果 */
                add_fmt(B, "    lua_insert(L, %s);\n", obf_int(-3, &obf_seed, obfuscate));
                add_fmt(B, "    lua_pop(L, %s);\n", obf_int(2, &obf_seed, obfuscate));
                emit_write_local(B, a + 1, boxed_slots, &obf_seed, obfuscate);
            } else {
                emit_read_local(B, b + 1, boxed_slots, &obf_seed, obfuscate);
                emit_read_local(B, c + 1, boxed_slots, &obf_seed, obfuscate);
                int op_enum = -1;
                if (op == OP_ADD) op_enum = LUA_OPADD;
                else if (op == OP_SUB) op_enum = LUA_OPSUB;
                else if (op == OP_MUL) op_enum = LUA_OPMUL;
                else if (op == OP_DIV) op_enum = LUA_OPDIV;
                else if (op == OP_IDIV) op_enum = LUA_OPIDIV;
                else if (op == OP_MOD) op_enum = LUA_OPMOD;
                else if (op == OP_POW) op_enum = LUA_OPPOW;
                else if (op == OP_BAND) op_enum = LUA_OPBAND;
                else if (op == OP_BOR) op_enum = LUA_OPBOR;
                else if (op == OP_BXOR) op_enum = LUA_OPBXOR;
                else if (op == OP_SHL) op_enum = LUA_OPSHL;
                else if (op == OP_SHR) op_enum = LUA_OPSHR;

                add_fmt(B, "    lua_arith(L, %s);\n", obf_int(op_enum, &obf_seed, obfuscate));
                emit_write_local(B, a + 1, boxed_slots, &obf_seed, obfuscate);
            }
            break;
        }

        case OP_ADDK: case OP_SUBK: case OP_MULK: case OP_MODK:
        case OP_POWK: case OP_DIVK: case OP_IDIVK:
        case OP_BANDK: case OP_BORK: case OP_BXORK: {
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            /* 修复：源 R[B] boxed 时纯 C 路径直接 lua_tointeger(L, B+1) 会取到 box table 不是值；
             * 统一先 emit_read_local(B) 推到栈顶，纯 C 路径从 -1 取数，lua_arith 路径也一样；
             * 结果写回用 emit_write_local(A)。 */
            if (use_pure_c) {
                const char *op_str = NULL;
                int is_int = 0;
                int is_pow = 0;
                if (op == OP_ADDK) op_str = "+";
                else if (op == OP_SUBK) op_str = "-";
                else if (op == OP_MULK) op_str = "*";
                else if (op == OP_DIVK) op_str = "/";
                else if (op == OP_IDIVK) { op_str = "/"; is_int = 1; }
                else if (op == OP_MODK) { op_str = "%"; is_int = 1; }
                else if (op == OP_POWK) is_pow = 1;
                else if (op == OP_BANDK) { op_str = "&"; is_int = 1; }
                else if (op == OP_BORK) { op_str = "|"; is_int = 1; }
                else if (op == OP_BXORK) { op_str = "^"; is_int = 1; }

                TValue *k = &p->k[c];
                char k_str[64];
                if (ttisinteger(k)) snprintf(k_str, sizeof(k_str), "%lld", (long long)ivalue(k));
                else if (ttisnumber(k)) snprintf(k_str, sizeof(k_str), "%f", fltvalue(k));
                else snprintf(k_str, sizeof(k_str), "0");

                /* 先推 B 到栈顶（解 _v） */
                emit_read_local(B, b + 1, boxed_slots, &obf_seed, obfuscate);
                if (is_pow) {
                     add_fmt(B, "    lua_pushnumber(L, pow(lua_tonumber(L, %s), %s));\n", obf_int(-1, &obf_seed, obfuscate), k_str);
                } else if (is_int) {
                     add_fmt(B, "    lua_pushinteger(L, (lua_Integer)lua_tointeger(L, %s) %s (lua_Integer)%s);\n", obf_int(-1, &obf_seed, obfuscate), op_str, k_str);
                } else {
                     add_fmt(B, "    lua_pushnumber(L, (lua_Number)lua_tonumber(L, %s) %s (lua_Number)%s);\n", obf_int(-1, &obf_seed, obfuscate), op_str, k_str);
                }
                /* 弹出 B（-2 位置），留结果在栈顶 */
                add_fmt(B, "    lua_remove(L, %s);\n", obf_int(-2, &obf_seed, obfuscate));
                emit_write_local(B, a + 1, boxed_slots, &obf_seed, obfuscate);
            } else {
                emit_read_local(B, b + 1, boxed_slots, &obf_seed, obfuscate);
                emit_loadk(B, p, c, str_encrypt, seed, obfuscate);
                int op_enum = -1;
                if (op == OP_ADDK) op_enum = LUA_OPADD;
                else if (op == OP_SUBK) op_enum = LUA_OPSUB;
                else if (op == OP_MULK) op_enum = LUA_OPMUL;
                else if (op == OP_MODK) op_enum = LUA_OPMOD;
                else if (op == OP_POWK) op_enum = LUA_OPPOW;
                else if (op == OP_DIVK) op_enum = LUA_OPDIV;
                else if (op == OP_IDIVK) op_enum = LUA_OPIDIV;
                else if (op == OP_BANDK) op_enum = LUA_OPBAND;
                else if (op == OP_BORK) op_enum = LUA_OPBOR;
                else if (op == OP_BXORK) op_enum = LUA_OPBXOR;

                add_fmt(B, "    lua_arith(L, %s);\n", obf_int(op_enum, &obf_seed, obfuscate));
                emit_write_local(B, a + 1, boxed_slots, &obf_seed, obfuscate);
            }
            break;
        }

        case OP_SELF: {
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            /* 修复：self 语义 R[A+1]=R[B]; R[A]=R[B][K[C]]
             * R[B] 若 boxed，必须先解 _v 再作为对象和 self 参数。
             * R[A] 写回用 emit_write_local；R[A+1] 同样需要 emit_write_local（若 boxed）。 */
            emit_read_local(B, b + 1, boxed_slots, &obf_seed, obfuscate); /* R[B] 解 _v 入栈 */
            add_fmt(B, "    lua_pushvalue(L, %s);\n", obf_int(-1, &obf_seed, obfuscate)); /* 复制一份给 A+1 */
            emit_write_local(B, a + 2, boxed_slots, &obf_seed, obfuscate); /* R[A+1] = R[B] */
            if (TESTARG_k(i)) {
                 TValue *k = &p->k[c];
                 if (ttisstring(k)) {
                      if (str_encrypt) {
                          emit_encrypted_string_push(B, getstr(tsvalue(k)), tsslen(tsvalue(k)), seed);
                          add_fmt(B, "    lua_gettable(L, %s);\n", obf_int(-2, &obf_seed, obfuscate));
                      } else {
                          add_fmt(B, "    lua_getfield(L, %s, ", obf_int(-1, &obf_seed, obfuscate));
                          emit_quoted_string(B, getstr(tsvalue(k)), tsslen(tsvalue(k)));
                          add_fmt(B, ");\n");
                      }
                      /* 此时栈顺序：obj key method → lua_remove 删除 key (obj,method) */
                      add_fmt(B, "    lua_remove(L, %s);\n", obf_int(-2, &obf_seed, obfuscate));
                      emit_write_local(B, a + 1, boxed_slots, &obf_seed, obfuscate);
                 } else {
                      emit_loadk(B, p, c, str_encrypt, seed, obfuscate);
                      add_fmt(B, "    lua_gettable(L, %s);\n", obf_int(-2, &obf_seed, obfuscate));
                      add_fmt(B, "    lua_remove(L, %s);\n", obf_int(-2, &obf_seed, obfuscate)); /* 删 obj 留 method */
                      emit_write_local(B, a + 1, boxed_slots, &obf_seed, obfuscate);
                 }
            } else {
                emit_read_local(B, c + 1, boxed_slots, &obf_seed, obfuscate); /* key 也可能 boxed */
                add_fmt(B, "    lua_gettable(L, %s);\n", obf_int(-2, &obf_seed, obfuscate));
                add_fmt(B, "    lua_remove(L, %s);\n", obf_int(-2, &obf_seed, obfuscate));
                emit_write_local(B, a + 1, boxed_slots, &obf_seed, obfuscate);
            }
            break;
        }

        case OP_ADDI: { // R[A] := R[B] + sC
             int b = GETARG_B(i);
             int sc = GETARG_sC(i);
             /* 修复：R[B] boxed → 解 _v；结果 R[A] boxed → 写 _v */
             if (use_pure_c) {
                 emit_read_local(B, b + 1, boxed_slots, &obf_seed, obfuscate);
                 add_fmt(B, "    lua_pushinteger(L, (lua_Integer)lua_tointeger(L, %s) + %d);\n", obf_int(-1, &obf_seed, obfuscate), sc);
                 add_fmt(B, "    lua_remove(L, %s);\n", obf_int(-2, &obf_seed, obfuscate));
                 emit_write_local(B, a + 1, boxed_slots, &obf_seed, obfuscate);
             } else {
                 emit_read_local(B, b + 1, boxed_slots, &obf_seed, obfuscate);
                 add_fmt(B, "    lua_pushinteger(L, %s);\n", obf_int(sc, &obf_seed, obfuscate));
                 add_fmt(B, "    lua_arith(L, %s);\n", obf_int(LUA_OPADD, &obf_seed, obfuscate));
                 emit_write_local(B, a + 1, boxed_slots, &obf_seed, obfuscate);
             }
             break;
        }

        case OP_SHLI: { // R[A] := sC << R[B]
             int b = GETARG_B(i);
             int sc = GETARG_sC(i);
             /* 修复：R[B] boxed → 解 _v；结果 R[A] boxed → 写 _v */
             if (use_pure_c) {
                 emit_read_local(B, b + 1, boxed_slots, &obf_seed, obfuscate);
                 add_fmt(B, "    lua_pushinteger(L, (lua_Integer)%s << (lua_Integer)lua_tointeger(L, %s));\n", obf_int(sc, &obf_seed, obfuscate), obf_int(-1, &obf_seed, obfuscate));
                 add_fmt(B, "    lua_remove(L, %s);\n", obf_int(-2, &obf_seed, obfuscate));
                 emit_write_local(B, a + 1, boxed_slots, &obf_seed, obfuscate);
             } else {
                 add_fmt(B, "    lua_pushinteger(L, %s);\n", obf_int(sc, &obf_seed, obfuscate));
                 emit_read_local(B, b + 1, boxed_slots, &obf_seed, obfuscate);
                 add_fmt(B, "    lua_arith(L, %s);\n", obf_int(LUA_OPSHL, &obf_seed, obfuscate));
                 emit_write_local(B, a + 1, boxed_slots, &obf_seed, obfuscate);
             }
             break;
        }

        case OP_SHRI: { // R[A] := R[B] >> sC
             int b = GETARG_B(i);
             int sc = GETARG_sC(i);
             /* 修复：R[B] boxed → 解 _v；结果 R[A] boxed → 写 _v */
             if (use_pure_c) {
                 emit_read_local(B, b + 1, boxed_slots, &obf_seed, obfuscate);
                 add_fmt(B, "    lua_pushinteger(L, (lua_Integer)lua_tointeger(L, %s) >> %s);\n", obf_int(-1, &obf_seed, obfuscate), obf_int(sc, &obf_seed, obfuscate));
                 add_fmt(B, "    lua_remove(L, %s);\n", obf_int(-2, &obf_seed, obfuscate));
                 emit_write_local(B, a + 1, boxed_slots, &obf_seed, obfuscate);
             } else {
                 emit_read_local(B, b + 1, boxed_slots, &obf_seed, obfuscate);
                 add_fmt(B, "    lua_pushinteger(L, %s);\n", obf_int(sc, &obf_seed, obfuscate));
                 add_fmt(B, "    lua_arith(L, %s);\n", obf_int(LUA_OPSHR, &obf_seed, obfuscate));
                 emit_write_local(B, a + 1, boxed_slots, &obf_seed, obfuscate);
             }
             break;
        }

        case OP_UNM: {
            int b = GETARG_B(i);
            /* 修复：源 R[B] boxed → 解 _v；目标 R[A] boxed → 写 _v */
            if (use_pure_c) {
                emit_read_local(B, b + 1, boxed_slots, &obf_seed, obfuscate);
                add_fmt(B, "    lua_pushnumber(L, -(lua_Number)lua_tonumber(L, %s));\n", obf_int(-1, &obf_seed, obfuscate));
                add_fmt(B, "    lua_remove(L, %s);\n", obf_int(-2, &obf_seed, obfuscate));
                emit_write_local(B, a + 1, boxed_slots, &obf_seed, obfuscate);
            } else {
                emit_read_local(B, b + 1, boxed_slots, &obf_seed, obfuscate);
                add_fmt(B, "    lua_arith(L, LUA_OPUNM);\n");
                emit_write_local(B, a + 1, boxed_slots, &obf_seed, obfuscate);
            }
            break;
        }

        case OP_BNOT: {
            int b = GETARG_B(i);
            /* 修复：源 R[B] boxed → 解 _v；目标 R[A] boxed → 写 _v */
            if (use_pure_c) {
                emit_read_local(B, b + 1, boxed_slots, &obf_seed, obfuscate);
                add_fmt(B, "    lua_pushinteger(L, ~(lua_Integer)lua_tointeger(L, %s));\n", obf_int(-1, &obf_seed, obfuscate));
                add_fmt(B, "    lua_remove(L, %s);\n", obf_int(-2, &obf_seed, obfuscate));
                emit_write_local(B, a + 1, boxed_slots, &obf_seed, obfuscate);
            } else {
                emit_read_local(B, b + 1, boxed_slots, &obf_seed, obfuscate);
                add_fmt(B, "    lua_arith(L, LUA_OPBNOT);\n");
                emit_write_local(B, a + 1, boxed_slots, &obf_seed, obfuscate);
            }
            break;
        }

        case OP_CALL: { // R[A], ... := R[A](R[A+1], ... ,R[A+B-1])
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            int nargs = (b == 0) ? -1 : (b - 1); // b=0 means top-A
            int nresults = (c == 0) ? -1 : (c - 1);

            add_fmt(B, "    {\n");
            if (b != 0) {
                if (c == 0) add_fmt(B, "    int s = lua_gettop(L);\n");
                add_fmt(B, "    lua_tcc_push_args(L, %s, %s); /* func + args */\n", obf_int(a + 1, &obf_seed, obfuscate), obf_int(nargs + 1, &obf_seed, obfuscate));
                add_fmt(B, "    lua_call(L, %s, %s);\n", obf_int(nargs, &obf_seed, obfuscate), obf_int(nresults, &obf_seed, obfuscate));
                if (c != 0) {
                     add_fmt(B, "    lua_tcc_store_results(L, %s, %s);\n", obf_int(a + 1, &obf_seed, obfuscate), obf_int(nresults, &obf_seed, obfuscate));
                } else {
                     add_fmt(B, "    {\n");
                     add_fmt(B, "        int nres = lua_gettop(L) - s;\n");
                     add_fmt(B, "        for (int k = 0; k < nres; k++) {\n");
                     add_fmt(B, "            lua_pushvalue(L, s + 1 + k);\n");
                     add_fmt(B, "            lua_replace(L, %s + k);\n", obf_int(a + 1, &obf_seed, obfuscate));
                     add_fmt(B, "        }\n");
                     add_fmt(B, "        lua_settop(L, %s + nres);\n", obf_int(a, &obf_seed, obfuscate));
                     add_fmt(B, "    }\n");
                }
            } else {
                 /* Variable number of arguments from stack (B=0) */
                 /* 先把寄存器 L[a+1..] 上的 func + 参数 push 到栈顶供 lua_call 消费
                  * 修复 nres 计算：在 push_args 之前记录 __stack_before，调用后 lua_gettop() - __stack_before = 真实返回值数目
                  * 旧算法 s = (a+1)-1 = a; nres = lua_gettop() - s 错误：
                  *   lua_gettop 是 L 栈绝对高度（跨调用帧），a 是字节码寄存器编号（相对当前函数），
                  *   两者单位不一致，导致 nres 虚高，把栈上无关内容（调用者栈帧）搬进寄存器，
                  *   引发 slot 错乱，最终 a.name/a.age 读成 nil。 */
                 add_fmt(B, "    int __nargs_call;\n");
                 add_fmt(B, "    int __stack_before;\n");
                 add_fmt(B, "    int __frame_handled = 0;\n");
                 if (p->is_vararg) {
                     add_fmt(B, "    if (vtab_idx == lua_gettop(L)) {\n");
                     /* 顺序关键：vtab 恰好位于栈顶，先把它暂存到注册表，再 push_args，避免参数拷贝覆盖 vtab */
                     add_fmt(B, "        int __vtab_ref = luaL_ref(L, LUA_REGISTRYINDEX); /* pop vtab from stack top */\n");
                     /* 此时栈顶 = 最后一个参数；func + args 个数 = lua_gettop - (a+1) + 1 = lua_gettop - a */
                     add_fmt(B, "        int __cnt = lua_gettop(L) - %s + 1;\n", obf_int(a + 1, &obf_seed, obfuscate));
                     add_fmt(B, "        __nargs_call = __cnt - 1;\n");
                     add_fmt(B, "        __stack_before = lua_gettop(L);\n");
                     add_fmt(B, "        lua_tcc_push_args(L, %s, __cnt); /* func + args to stack top */\n", obf_int(a + 1, &obf_seed, obfuscate));
                     add_fmt(B, "        lua_call(L, __nargs_call, %d);\n", nresults);
                     /* === 关键修复：先 store_results（此时栈顶正是 lua_call 返回值），再恢复 vtab === */
                     if (c != 0) {
                         add_fmt(B, "        lua_tcc_store_results(L, %s, %d);\n", obf_int(a + 1, &obf_seed, obfuscate), nresults);
                         /* restore stack frame: 先 settop(maxstacksize) 截断，再 rawgeti push vtab 正好到 maxstacksize+1 */
                         add_fmt(B, "        lua_settop(L, %s);\n", obf_int(p->maxstacksize, &obf_seed, obfuscate));
                         add_fmt(B, "        lua_rawgeti(L, LUA_REGISTRYINDEX, __vtab_ref);\n");
                         add_fmt(B, "        luaL_unref(L, LUA_REGISTRYINDEX, __vtab_ref);\n");
                         add_fmt(B, "        vtab_idx = %s;\n", obf_int(p->maxstacksize + 1, &obf_seed, obfuscate));
                     } else {
                         /* C==0 多返回值：nres = 当前栈顶 - push_args 之前栈顶（返回值恰好位于 __stack_before+1..top） */
                         add_fmt(B, "        { int nres = lua_gettop(L) - __stack_before;\n");
                         add_fmt(B, "            for (int __k = 0; __k < nres; __k++) {\n");
                         add_fmt(B, "                lua_pushvalue(L, __stack_before + 1 + __k);\n");
                         add_fmt(B, "                lua_replace(L, %s + __k);\n", obf_int(a + 1, &obf_seed, obfuscate));
                         add_fmt(B, "            }\n");
                         /* 修复：先 settop(maxstacksize+nres)，再 rawgeti push vtab，用 lua_insert 插入到 maxstacksize+1 */
                         add_fmt(B, "            lua_settop(L, %s + nres);\n", obf_int(p->maxstacksize, &obf_seed, obfuscate));
                         add_fmt(B, "            lua_rawgeti(L, LUA_REGISTRYINDEX, __vtab_ref);\n");
                         add_fmt(B, "            luaL_unref(L, LUA_REGISTRYINDEX, __vtab_ref);\n");
                         add_fmt(B, "            lua_insert(L, %s);\n", obf_int(p->maxstacksize + 1, &obf_seed, obfuscate));
                         add_fmt(B, "            vtab_idx = %s;\n", obf_int(p->maxstacksize + 1, &obf_seed, obfuscate));
                         add_fmt(B, "        }\n");
                     }
                     add_fmt(B, "        __frame_handled = 1;\n");
                     add_fmt(B, "    } else {\n");
                     /* vtab 不在栈顶；func+args 占 L[a+1..top]，共 top-(a+1)+1 = top-a 项 */
                     add_fmt(B, "        int __cnt = lua_gettop(L) - %s + 1;\n", obf_int(a + 1, &obf_seed, obfuscate));
                     add_fmt(B, "        __nargs_call = __cnt - 1;\n");
                     add_fmt(B, "        __stack_before = lua_gettop(L);\n");
                     add_fmt(B, "        lua_tcc_push_args(L, %s, __cnt);\n", obf_int(a + 1, &obf_seed, obfuscate));
                     add_fmt(B, "        lua_call(L, __nargs_call, %d);\n", nresults);
                     /* 普通分支（vtab 不在栈顶）：统一在块外处理 store_results + stack_restore */
                     add_fmt(B, "    }\n");
                 } else {
                     add_fmt(B, "    int __cnt = lua_gettop(L) - %s + 1;\n", obf_int(a + 1, &obf_seed, obfuscate));
                     add_fmt(B, "    __nargs_call = __cnt - 1;\n");
                     add_fmt(B, "    __stack_before = lua_gettop(L);\n");
                     add_fmt(B, "    lua_tcc_push_args(L, %s, __cnt);\n", obf_int(a + 1, &obf_seed, obfuscate));
                     add_fmt(B, "    lua_call(L, __nargs_call, %d);\n", nresults);
                 }
                 /* 以下统一处理：
                  *   - is_vararg 且 vtab==top 的分支：上面已经内联处理完 store_results + frame_restore，所以跳过
                  *   - 其他分支（vtab 不在栈顶 或 非 vararg）：在这里做 store_results + 可选 frame_restore
                  */
                 if (p->is_vararg) {
                     add_fmt(B, "    if (!__frame_handled && vtab_idx != lua_gettop(L)) {\n");
                 }
                 /* 把返回值从栈顶搬回寄存器 R[A..]，对齐固定结果数 */
                 if (c != 0) {
                     add_fmt(B, "    lua_tcc_store_results(L, %s, %s);\n", obf_int(a + 1, &obf_seed, obfuscate), obf_int(nresults, &obf_seed, obfuscate));
                 } else {
                     /* C==0 多返回值：nres = lua_gettop - __stack_before，返回值位于栈的 __stack_before+1..top */
                     add_fmt(B, "    { int nres = lua_gettop(L) - __stack_before;\n");
                     if (p->is_vararg) {
                         add_fmt(B, "        int __top_before = lua_gettop(L);\n");
                         add_fmt(B, "        if (vtab_idx == __top_before) {\n");
                         add_fmt(B, "            int __r = luaL_ref(L, LUA_REGISTRYINDEX); /* pop vtab */\n");
                         add_fmt(B, "            for (int __k = 0; __k < nres; __k++) {\n");
                         add_fmt(B, "                lua_pushvalue(L, __stack_before + 1 + __k);\n");
                         add_fmt(B, "                lua_replace(L, %s + __k);\n", obf_int(a + 1, &obf_seed, obfuscate));
                         add_fmt(B, "            }\n");
                         /* 修复：先 settop(maxstacksize) 截断，再用 __r 恢复 vtab 到 maxstacksize+1 = 栈顶，避免 lua_replace 越界 */
                         add_fmt(B, "            lua_settop(L, %s);\n", obf_int(p->maxstacksize, &obf_seed, obfuscate));
                         add_fmt(B, "            lua_rawgeti(L, LUA_REGISTRYINDEX, __r); /* vtab push 到栈顶 = maxstacksize+1 */\n");
                         add_fmt(B, "            luaL_unref(L, LUA_REGISTRYINDEX, __r);\n");
                         add_fmt(B, "            vtab_idx = %s;\n", obf_int(p->maxstacksize + 1, &obf_seed, obfuscate));
                         add_fmt(B, "        } else {\n");
                         add_fmt(B, "            for (int __k = 0; __k < nres; __k++) {\n");
                         add_fmt(B, "                lua_pushvalue(L, __stack_before + 1 + __k);\n");
                         add_fmt(B, "                lua_replace(L, %s + __k);\n", obf_int(a + 1, &obf_seed, obfuscate));
                         add_fmt(B, "            }\n");
                         add_fmt(B, "            lua_settop(L, %s + nres);\n", obf_int(a, &obf_seed, obfuscate));
                         add_fmt(B, "        }\n");
                     } else {
                         add_fmt(B, "        for (int __k = 0; __k < nres; __k++) {\n");
                         add_fmt(B, "            lua_pushvalue(L, __stack_before + 1 + __k);\n");
                         add_fmt(B, "            lua_replace(L, %s + __k);\n", obf_int(a + 1, &obf_seed, obfuscate));
                         add_fmt(B, "        }\n");
                         add_fmt(B, "        lua_settop(L, %s + nres);\n", obf_int(a, &obf_seed, obfuscate));
                     }
                     add_fmt(B, "    }\n");
                 }
                 /* If fixed results (C!=0), restore stack frame size if needed */
                 if (c != 0) {
                      if (p->is_vararg) {
                          /* 修复：vtab 可能在任意位置，先 ref 暂存 → settop → rawgeti 恢复到栈顶 maxstacksize+1
                           * 避免 lua_replace 目标索引大于当前栈顶的越界 bug */
                          add_fmt(B, "    {\n");
                          add_fmt(B, "        int __vtab_r;\n");
                          add_fmt(B, "        lua_pushvalue(L, vtab_idx); __vtab_r = luaL_ref(L, LUA_REGISTRYINDEX);\n");
                          add_fmt(B, "        lua_settop(L, %s);\n", obf_int(p->maxstacksize, &obf_seed, obfuscate));
                          add_fmt(B, "        lua_rawgeti(L, LUA_REGISTRYINDEX, __vtab_r);\n");
                          add_fmt(B, "        luaL_unref(L, LUA_REGISTRYINDEX, __vtab_r);\n");
                          add_fmt(B, "        vtab_idx = %s;\n", obf_int(p->maxstacksize + 1, &obf_seed, obfuscate));
                          add_fmt(B, "    }\n");
                      } else {
                          add_fmt(B, "    lua_settop(L, %s);\n", obf_int(p->maxstacksize, &obf_seed, obfuscate));
                      }
                 }
                 if (p->is_vararg) {
                     add_fmt(B, "    }\n"); /* close if (vtab_idx != lua_gettop(L)) */
                 }
            }
            add_fmt(B, "    }\n");
            break;
        }

        case OP_TAILCALL: { // return R[A](...)
             // Treat as regular CALL + RETURN
            int b = GETARG_B(i);
            int nargs = (b == 0) ? -1 : (b - 1);

            if (b != 0) {
                add_fmt(B, "    lua_tcc_push_args(L, %s, %s); /* func + args */\n", obf_int(a + 1, &obf_seed, obfuscate), obf_int(nargs + 1, &obf_seed, obfuscate));
                add_fmt(B, "    lua_call(L, %s, %s);\n", obf_int(nargs, &obf_seed, obfuscate), obf_int(LUA_MULTRET, &obf_seed, obfuscate));
                add_fmt(B, "    return lua_gettop(L) - %s;\n", obf_int(p->maxstacksize + (p->is_vararg ? 1 : 0), &obf_seed, obfuscate));
            } else {
                 /* Variable number of arguments from stack (B=0) */
                 /* 先把寄存器 L[a+1..] 上的 func + args push 到栈顶；注意 vtab 在栈顶时要先暂存 */
                 add_fmt(B, "    { int __cnt, __n;\n");
                 if (p->is_vararg) {
                     add_fmt(B, "        if (vtab_idx == lua_gettop(L)) {\n");
                     add_fmt(B, "            int __r = luaL_ref(L, LUA_REGISTRYINDEX); /* pop vtab */\n");
                     add_fmt(B, "            __cnt = lua_gettop(L) - %s + 1;\n", obf_int(a + 1, &obf_seed, obfuscate));
                     add_fmt(B, "            __n = __cnt - 1;\n");
                     add_fmt(B, "            lua_tcc_push_args(L, %s, __cnt);\n", obf_int(a + 1, &obf_seed, obfuscate));
                     add_fmt(B, "            lua_call(L, __n, LUA_MULTRET);\n");
                     add_fmt(B, "            lua_rawgeti(L, LUA_REGISTRYINDEX, __r);\n");
                     add_fmt(B, "            luaL_unref(L, LUA_REGISTRYINDEX, __r);\n");
                     add_fmt(B, "            (void)__r;\n");
                     add_fmt(B, "        } else {\n");
                     add_fmt(B, "            __cnt = lua_gettop(L) - %s + 1;\n", obf_int(a + 1, &obf_seed, obfuscate));
                     add_fmt(B, "            __n = __cnt - 1;\n");
                     add_fmt(B, "            lua_tcc_push_args(L, %s, __cnt);\n", obf_int(a + 1, &obf_seed, obfuscate));
                     add_fmt(B, "            lua_call(L, __n, LUA_MULTRET);\n");
                     add_fmt(B, "        }\n");
                 } else {
                     add_fmt(B, "        __cnt = lua_gettop(L) - %s + 1;\n", obf_int(a + 1, &obf_seed, obfuscate));
                     add_fmt(B, "        __n = __cnt - 1;\n");
                     add_fmt(B, "        lua_tcc_push_args(L, %s, __cnt);\n", obf_int(a + 1, &obf_seed, obfuscate));
                     add_fmt(B, "        lua_call(L, __n, LUA_MULTRET);\n");
                 }
                 add_fmt(B, "    }\n");
                 add_fmt(B, "    return lua_gettop(L) - %s;\n", obf_int(a, &obf_seed, obfuscate));
            }
            break;
        }

        case OP_RETURN: { // return R[A], ... ,R[A+B-2]
            int b = GETARG_B(i);
            int nret = (b == 0) ? -1 : (b - 1);

            if (nret > 0) {
                add_fmt(B, "    lua_tcc_push_args(L, %s, %s);\n", obf_int(a + 1, &obf_seed, obfuscate), obf_int(nret, &obf_seed, obfuscate));
                add_fmt(B, "    return %s;\n", obf_int(nret, &obf_seed, obfuscate));
            } else if (nret == 0) {
                add_fmt(B, "    return %s;\n", obf_int(0, &obf_seed, obfuscate));
            } else {
                 if (p->is_vararg) {
                     add_fmt(B, "    if (vtab_idx == lua_gettop(L)) lua_settop(L, lua_gettop(L) - %s);\n", obf_int(1, &obf_seed, obfuscate));
                 }
                 add_fmt(B, "    return lua_gettop(L) - %s;\n", obf_int(a, &obf_seed, obfuscate));
            }
            break;
        }

        case OP_RETURN0:
            add_fmt(B, "    return %s;\n", obf_int(0, &obf_seed, obfuscate));
            break;

        case OP_RETURN1:
            add_fmt(B, "    lua_pushvalue(L, %s);\n", obf_int(a + 1, &obf_seed, obfuscate));
            add_fmt(B, "    return %s;\n", obf_int(1, &obf_seed, obfuscate));
            break;

        case OP_CLOSURE: { // R[A] := closure(KPROTO[Bx])
            int bx = GETARG_Bx(i);
            Proto *child = p->p[bx];
            int child_id = get_proto_id(child, protos, proto_count);

            for (int k = 0; k < child->sizeupvalues; k++) {
                 Upvaldesc *uv = &child->upvalues[k];
                 if (uv->instack) {
                     int slot = uv->idx + 1;
                     if (is_upval_modified(child, k)) {
                         if (boxed_slots && boxed_slots[slot]) {
                             /* 已被其他闭包 boxed，直接推送 box table */
                             add_fmt(B, "    lua_pushvalue(L, %s); /* upval %d (local, already boxed) */\n", obf_int(slot, &obf_seed, obfuscate), k);
                         } else {
                             /* 创建 box table 实现共享引用 */
                             add_fmt(B, "    {\n");
                             add_fmt(B, "        lua_createtable(L, 0, 1);\n");
                             add_fmt(B, "        lua_pushvalue(L, %s);\n", obf_int(slot, &obf_seed, obfuscate));
                             add_fmt(B, "        lua_setfield(L, -2, \"_v\");\n");
                             add_fmt(B, "        lua_replace(L, %s);\n", obf_int(slot, &obf_seed, obfuscate));
                             add_fmt(B, "    }\n");
                             /* 推送 box table 作为闭包的 upvalue */
                             add_fmt(B, "    lua_pushvalue(L, %s); /* upval %d (local, boxed) */\n", obf_int(slot, &obf_seed, obfuscate), k);
                             /* 标记该 slot 已被 boxed */
                             if (boxed_slots) boxed_slots[slot] = 1;
                         }
                     } else {
                         add_fmt(B, "    lua_pushvalue(L, %s); /* upval %d (local) */\n", obf_int(slot, &obf_seed, obfuscate), k);
                     }
                 } else {
                     int upidx = uv->idx;
                     if (is_upval_modified(child, k)) {
                         /* 该 upvalue 在子函数中会被修改，需要通过 box table 共享引用 */
                         if (boxed_upvals && boxed_upvals[upidx]) {
                             /* 已在当前函数包装过 */
                             add_fmt(B, "    lua_pushvalue(L, lua_upvalueindex(%s)); /* upval %d (upval, already boxed) */\n", obf_int(upidx + 1, &obf_seed, obfuscate), k);
                         } else {
                             add_fmt(B, "    {\n");
                             add_fmt(B, "        lua_createtable(L, 0, 1);\n");
                             add_fmt(B, "        lua_pushvalue(L, lua_upvalueindex(%s));\n", obf_int(upidx + 1, &obf_seed, obfuscate));
                             add_fmt(B, "        lua_setfield(L, -2, \"_v\");\n");
                             /* 把 box table 替换回当前函数的 upvalue upidx+1 */
                             add_fmt(B, "        lua_setupvalue(L, 0, %s);\n", obf_int(upidx + 1, &obf_seed, obfuscate));
                             add_fmt(B, "    }\n");
                             /* 再次推送 boxed upvalue 给子闭包 */
                             add_fmt(B, "    lua_pushvalue(L, lua_upvalueindex(%s)); /* upval %d (upval, boxed) */\n", obf_int(upidx + 1, &obf_seed, obfuscate), k);
                             if (boxed_upvals) boxed_upvals[upidx] = 1;
                         }
                     } else {
                         add_fmt(B, "    lua_pushvalue(L, lua_upvalueindex(%s)); /* upval %d (upval) */\n", obf_int(upidx + 1, &obf_seed, obfuscate), k);
                     }
                 }
            }

            add_fmt(B, "    lua_pushcclosure(L, %s, %s);\n", protos[child_id].name, obf_int(child->sizeupvalues, &obf_seed, obfuscate));
            add_fmt(B, "    lua_replace(L, %s);\n", obf_int(a + 1, &obf_seed, obfuscate));

            /* 修复自引用 upvalue：闭包捕获自身所在的槽位 */
            for (int k = 0; k < child->sizeupvalues; k++) {
                Upvaldesc *uv = &child->upvalues[k];
                if (uv->instack && uv->idx == a) {
                    add_fmt(B, "    lua_pushvalue(L, %s); /* fix self-upval %d */\n", obf_int(a + 1, &obf_seed, obfuscate), k);
                    add_fmt(B, "    lua_setupvalue(L, %s, %s);\n", obf_int(a + 1, &obf_seed, obfuscate), obf_int(k + 1, &obf_seed, obfuscate));
                }
            }
            break;
        }

        case OP_NEWCONCEPT: {
            int bx = GETARG_Bx(i);
            Proto *child = p->p[bx];
            int child_id = get_proto_id(child, protos, proto_count);

            for (int k = 0; k < child->sizeupvalues; k++) {
                 Upvaldesc *uv = &child->upvalues[k];
                 if (uv->instack) {
                     add_fmt(B, "    lua_pushvalue(L, %s); /* upval %d (local) */\n", obf_int(uv->idx + 1, &obf_seed, obfuscate), k);
                 } else {
                     add_fmt(B, "    lua_pushvalue(L, lua_upvalueindex(%s)); /* upval %d (upval) */\n", obf_int(uv->idx + 1, &obf_seed, obfuscate), k);
                 }
            }

            add_fmt(B, "    lua_pushcclosure(L, %s, %s); /* concept */\n", protos[child_id].name, obf_int(child->sizeupvalues, &obf_seed, obfuscate));
            add_fmt(B, "    lua_replace(L, %s);\n", obf_int(a + 1, &obf_seed, obfuscate));

            /* 修复自引用 upvalue */
            for (int k = 0; k < child->sizeupvalues; k++) {
                Upvaldesc *uv = &child->upvalues[k];
                if (uv->instack && uv->idx == a) {
                    add_fmt(B, "    lua_pushvalue(L, %s); /* fix self-upval %d */\n", obf_int(a + 1, &obf_seed, obfuscate), k);
                    add_fmt(B, "    lua_setupvalue(L, %s, %s);\n", obf_int(a + 1, &obf_seed, obfuscate), obf_int(k + 1, &obf_seed, obfuscate));
                }
            }
            break;
        }

        case OP_JMP: {
            int sj = GETARG_sJ(i);
            char target_label[16];
            get_label_name(target_label, sizeof(target_label), pc + 1 + sj + 1, seed, obfuscate);
            add_fmt(B, "    goto %s;\n", target_label);
            break;
        }

        case OP_EQ: { // if ((R[A] == R[B]) ~= k) then pc++
            int b = GETARG_B(i);
            int k = GETARG_k(i);
            char target_label[16];
            get_label_name(target_label, sizeof(target_label), pc + 1 + 2, seed, obfuscate);
            add_fmt(B, "    {\n");
            add_fmt(B, "        lua_pushvalue(L, %s);\n", obf_int(a + 1, &obf_seed, obfuscate));
            add_fmt(B, "        lua_pushvalue(L, %s);\n", obf_int(b + 1, &obf_seed, obfuscate));
            add_fmt(B, "        int res = lua_compare(L, %s, %s, %s);\n", obf_int(-2, &obf_seed, obfuscate), obf_int(-1, &obf_seed, obfuscate), obf_int(LUA_OPEQ, &obf_seed, obfuscate));
            add_fmt(B, "        lua_pop(L, %s);\n", obf_int(2, &obf_seed, obfuscate));
            add_fmt(B, "        if (res != %s) goto %s;\n", obf_int(k, &obf_seed, obfuscate), target_label);
            add_fmt(B, "    }\n");
            break;
        }

        case OP_LT: {
            int b = GETARG_B(i);
            int k = GETARG_k(i);
            char target_label[16];
            get_label_name(target_label, sizeof(target_label), pc + 1 + 2, seed, obfuscate);
            add_fmt(B, "    {\n");
            add_fmt(B, "        lua_pushvalue(L, %s);\n", obf_int(a + 1, &obf_seed, obfuscate));
            add_fmt(B, "        lua_pushvalue(L, %s);\n", obf_int(b + 1, &obf_seed, obfuscate));
            add_fmt(B, "        int res = lua_compare(L, %s, %s, %s);\n", obf_int(-2, &obf_seed, obfuscate), obf_int(-1, &obf_seed, obfuscate), obf_int(LUA_OPLT, &obf_seed, obfuscate));
            add_fmt(B, "        lua_pop(L, %s);\n", obf_int(2, &obf_seed, obfuscate));
            add_fmt(B, "        if (res != %s) goto %s;\n", obf_int(k, &obf_seed, obfuscate), target_label);
            add_fmt(B, "    }\n");
            break;
        }

        case OP_LE: {
            int b = GETARG_B(i);
            int k = GETARG_k(i);
            char target_label[16];
            get_label_name(target_label, sizeof(target_label), pc + 1 + 2, seed, obfuscate);
            add_fmt(B, "    {\n");
            add_fmt(B, "        lua_pushvalue(L, %s);\n", obf_int(a + 1, &obf_seed, obfuscate));
            add_fmt(B, "        lua_pushvalue(L, %s);\n", obf_int(b + 1, &obf_seed, obfuscate));
            add_fmt(B, "        int res = lua_compare(L, %s, %s, %s);\n", obf_int(-2, &obf_seed, obfuscate), obf_int(-1, &obf_seed, obfuscate), obf_int(LUA_OPLE, &obf_seed, obfuscate));
            add_fmt(B, "        lua_pop(L, %s);\n", obf_int(2, &obf_seed, obfuscate));
            add_fmt(B, "        if (res != %s) goto %s;\n", obf_int(k, &obf_seed, obfuscate), target_label);
            add_fmt(B, "    }\n");
            break;
        }

        case OP_EQK: {
            int b = GETARG_B(i);
            int k = GETARG_k(i);
            char target_label[16];
            get_label_name(target_label, sizeof(target_label), pc + 1 + 2, seed, obfuscate);
            add_fmt(B, "    {\n");
            add_fmt(B, "        lua_pushvalue(L, %s);\n", obf_int(a + 1, &obf_seed, obfuscate));
            emit_loadk(B, p, b, str_encrypt, seed, obfuscate);
            add_fmt(B, "        int res = lua_compare(L, %s, %s, %s);\n", obf_int(-2, &obf_seed, obfuscate), obf_int(-1, &obf_seed, obfuscate), obf_int(LUA_OPEQ, &obf_seed, obfuscate));
            add_fmt(B, "        lua_pop(L, %s);\n", obf_int(2, &obf_seed, obfuscate));
            add_fmt(B, "        if (res != %s) goto %s;\n", obf_int(k, &obf_seed, obfuscate), target_label);
            add_fmt(B, "    }\n");
            break;
        }

        case OP_EQI: {
            int sb = GETARG_sB(i);
            int k = GETARG_k(i);
            char target_label[16];
            get_label_name(target_label, sizeof(target_label), pc + 1 + 2, seed, obfuscate);
            add_fmt(B, "    {\n");
            add_fmt(B, "        lua_pushvalue(L, %s);\n", obf_int(a + 1, &obf_seed, obfuscate));
            add_fmt(B, "        lua_pushinteger(L, %s);\n", obf_int(sb, &obf_seed, obfuscate));
            add_fmt(B, "        int res = lua_compare(L, %s, %s, %s);\n", obf_int(-2, &obf_seed, obfuscate), obf_int(-1, &obf_seed, obfuscate), obf_int(LUA_OPEQ, &obf_seed, obfuscate));
            add_fmt(B, "        lua_pop(L, %s);\n", obf_int(2, &obf_seed, obfuscate));
            add_fmt(B, "        if (res != %s) goto %s;\n", obf_int(k, &obf_seed, obfuscate), target_label);
            add_fmt(B, "    }\n");
            break;
        }

        case OP_LTI: {
            int sb = GETARG_sB(i);
            int k = GETARG_k(i);
            char target_label[16];
            get_label_name(target_label, sizeof(target_label), pc + 1 + 2, seed, obfuscate);
            add_fmt(B, "    {\n");
            add_fmt(B, "        lua_pushvalue(L, %s);\n", obf_int(a + 1, &obf_seed, obfuscate));
            add_fmt(B, "        lua_pushinteger(L, %s);\n", obf_int(sb, &obf_seed, obfuscate));
            add_fmt(B, "        int res = lua_compare(L, %s, %s, %s);\n", obf_int(-2, &obf_seed, obfuscate), obf_int(-1, &obf_seed, obfuscate), obf_int(LUA_OPLT, &obf_seed, obfuscate));
            add_fmt(B, "        lua_pop(L, %s);\n", obf_int(2, &obf_seed, obfuscate));
            add_fmt(B, "        if (res != %s) goto %s;\n", obf_int(k, &obf_seed, obfuscate), target_label);
            add_fmt(B, "    }\n");
            break;
        }

        case OP_LEI: {
            int sb = GETARG_sB(i);
            int k = GETARG_k(i);
            char target_label[16];
            get_label_name(target_label, sizeof(target_label), pc + 1 + 2, seed, obfuscate);
            add_fmt(B, "    {\n");
            add_fmt(B, "        lua_pushvalue(L, %s);\n", obf_int(a + 1, &obf_seed, obfuscate));
            add_fmt(B, "        lua_pushinteger(L, %s);\n", obf_int(sb, &obf_seed, obfuscate));
            add_fmt(B, "        int res = lua_compare(L, %s, %s, %s);\n", obf_int(-2, &obf_seed, obfuscate), obf_int(-1, &obf_seed, obfuscate), obf_int(LUA_OPLE, &obf_seed, obfuscate));
            add_fmt(B, "        lua_pop(L, %s);\n", obf_int(2, &obf_seed, obfuscate));
            add_fmt(B, "        if (res != %s) goto %s;\n", obf_int(k, &obf_seed, obfuscate), target_label);
            add_fmt(B, "    }\n");
            break;
        }

        case OP_GTI: {
            int sb = GETARG_sB(i);
            int k = GETARG_k(i);
            char target_label[16];
            get_label_name(target_label, sizeof(target_label), pc + 1 + 2, seed, obfuscate);
            add_fmt(B, "    {\n");
            add_fmt(B, "        lua_pushinteger(L, %s);\n", obf_int(sb, &obf_seed, obfuscate));
            add_fmt(B, "        lua_pushvalue(L, %s);\n", obf_int(a + 1, &obf_seed, obfuscate));
            add_fmt(B, "        int res = lua_compare(L, %s, %s, %s);\n", obf_int(-2, &obf_seed, obfuscate), obf_int(-1, &obf_seed, obfuscate), obf_int(LUA_OPLT, &obf_seed, obfuscate));
            add_fmt(B, "        lua_pop(L, %s);\n", obf_int(2, &obf_seed, obfuscate));
            add_fmt(B, "        if (res != %s) goto %s;\n", obf_int(k, &obf_seed, obfuscate), target_label);
            add_fmt(B, "    }\n");
            break;
        }

        case OP_GEI: {
            int sb = GETARG_sB(i);
            int k = GETARG_k(i);
            char target_label[16];
            get_label_name(target_label, sizeof(target_label), pc + 1 + 2, seed, obfuscate);
            add_fmt(B, "    {\n");
            add_fmt(B, "        lua_pushinteger(L, %s);\n", obf_int(sb, &obf_seed, obfuscate));
            add_fmt(B, "        lua_pushvalue(L, %s);\n", obf_int(a + 1, &obf_seed, obfuscate));
            add_fmt(B, "        int res = lua_compare(L, %s, %s, %s);\n", obf_int(-2, &obf_seed, obfuscate), obf_int(-1, &obf_seed, obfuscate), obf_int(LUA_OPLE, &obf_seed, obfuscate));
            add_fmt(B, "        lua_pop(L, %s);\n", obf_int(2, &obf_seed, obfuscate));
            add_fmt(B, "        if (res != %s) goto %s;\n", obf_int(k, &obf_seed, obfuscate), target_label);
            add_fmt(B, "    }\n");
            break;
        }

        case OP_VARARG: {
            int nneeded = GETARG_C(i) - 1;

            if (nneeded >= 0) {
                /* 修复：先把 vtab 从 vtab_idx 位置 ref 到注册表（先 pushvalue 到栈顶，再 ref） */
                add_fmt(B, "    {\n");
                add_fmt(B, "        int __need_top = %s + %s;\n", obf_int(a + 1, &obf_seed, obfuscate), obf_int(nneeded, &obf_seed, obfuscate));
                add_fmt(B, "        lua_pushvalue(L, vtab_idx);\n");
                add_fmt(B, "        int __vtab_ref = luaL_ref(L, LUA_REGISTRYINDEX);\n");
                add_fmt(B, "        if (__need_top >= vtab_idx) {\n");
                add_fmt(B, "            lua_settop(L, __need_top);\n");
                add_fmt(B, "            lua_rawgeti(L, LUA_REGISTRYINDEX, __vtab_ref);\n");
                add_fmt(B, "            lua_replace(L, __need_top);\n");
                add_fmt(B, "            vtab_idx = __need_top;\n");
                add_fmt(B, "        } else {\n");
                add_fmt(B, "            /* vtab_idx 位置不变，恢复它 */\n");
                add_fmt(B, "            lua_rawgeti(L, LUA_REGISTRYINDEX, __vtab_ref);\n");
                add_fmt(B, "            lua_replace(L, vtab_idx);\n");
                add_fmt(B, "        }\n");
                add_fmt(B, "        luaL_unref(L, LUA_REGISTRYINDEX, __vtab_ref);\n");
                add_fmt(B, "    }\n");
                add_fmt(B, "    for (int i=0; i<%s; i++) {\n", obf_int(nneeded, &obf_seed, obfuscate));
                add_fmt(B, "        lua_rawgeti(L, vtab_idx, i+%s);\n", obf_int(1, &obf_seed, obfuscate));
                add_fmt(B, "        lua_replace(L, %s + i);\n", obf_int(a + 1, &obf_seed, obfuscate));
                add_fmt(B, "    }\n");
            } else {
                add_fmt(B, "    {\n");
                add_fmt(B, "        int nvar = (int)lua_rawlen(L, vtab_idx);\n");
                /* 修复：先 push vtab 再 ref，避免被 setop 弹走 */
                add_fmt(B, "        lua_pushvalue(L, vtab_idx);\n");
                add_fmt(B, "        int __vtab_ref = luaL_ref(L, LUA_REGISTRYINDEX);\n");
                add_fmt(B, "        int __target_top = %s + nvar;\n", obf_int(a + 1, &obf_seed, obfuscate));
                add_fmt(B, "        lua_settop(L, __target_top);\n");
                /* 拿回 vtab 放到 A + nvar 位置作为新锚点 */
                add_fmt(B, "        lua_rawgeti(L, LUA_REGISTRYINDEX, __vtab_ref);\n");
                add_fmt(B, "        lua_replace(L, __target_top);\n");
                add_fmt(B, "        vtab_idx = __target_top;\n");
                add_fmt(B, "        luaL_unref(L, LUA_REGISTRYINDEX, __vtab_ref);\n");
                add_fmt(B, "        for (int i=1; i<=nvar; i++) {\n");
                add_fmt(B, "            lua_rawgeti(L, vtab_idx, i);\n");
                add_fmt(B, "            lua_replace(L, %s + i - %s);\n", obf_int(a + 1, &obf_seed, obfuscate), obf_int(1, &obf_seed, obfuscate));
                add_fmt(B, "        }\n");
                add_fmt(B, "    }\n");
            }
            break;
        }

        case OP_GETVARG: {
            int c = GETARG_C(i);
            add_fmt(B, "    lua_rawgeti(L, vtab_idx, lua_tointeger(L, %s));\n", obf_int(c + 1, &obf_seed, obfuscate));
            add_fmt(B, "    lua_replace(L, %s);\n", obf_int(a + 1, &obf_seed, obfuscate));
            break;
        }

        case OP_VARARGPREP:
            add_fmt(B, "    /* VARARGPREP: adjust varargs if needed */\n");
            break;

        case OP_MMBIN:
        case OP_MMBINI:
        case OP_MMBINK:
             add_fmt(B, "    /* MMBIN: ignored as lua_arith handles it */\n");
             break;

        case OP_NEWTABLE: {
            unsigned int b = GETARG_vB(i);
            unsigned int c = GETARG_vC(i);
            if (TESTARG_k(i)) {
                if (pc + 1 < p->sizecode && GET_OPCODE(p->code[pc+1]) == OP_EXTRAARG) {
                    int ax = GETARG_Ax(p->code[pc+1]);
                    c += ax * (MAXARG_C + 1);
                }
            }
            int nhash = (b > 0) ? (1 << (b - 1)) : 0;
            add_fmt(B, "    lua_createtable(L, %s, %s);\n", obf_int(c, &obf_seed, obfuscate), obf_int(nhash, &obf_seed, obfuscate));
            add_fmt(B, "    lua_replace(L, %s);\n", obf_int(a + 1, &obf_seed, obfuscate));
            break;
        }

        case OP_GETTABLE: {
            /* R[A] := R[B][R[C]]：table/key 若 boxed 先解 _v；结果 a+1 若 boxed 写 _v 字段
             * helper 栈消费：lua_tcc_gettable(-2) → 内部 pop key(+t_copy) push result；原来 emit_read_local 推的 table(-2 位置) 保留。
             * 执行后：栈[-2]=table, [-1]=result → 需要去掉 table，再把 result 写回 a+1 */
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            emit_read_local(B, b + 1, boxed_slots, &obf_seed, obfuscate); /* table (+1) */
            emit_read_local(B, c + 1, boxed_slots, &obf_seed, obfuscate); /* key (+1, 总+2) */
            add_fmt(B, "    lua_tcc_gettable(L, %s);\n", obf_int(-2, &obf_seed, obfuscate)); /* result→顶，table 在 -2 (净+1) */
            add_fmt(B, "    lua_remove(L, %s);\n", obf_int(-2, &obf_seed, obfuscate)); /* 移除原 table，栈顶=result (净0) */
            emit_write_local(B, a + 1, boxed_slots, &obf_seed, obfuscate); /* 写回 a+1：boxed→setfield消费1; unboxed→replace不变 */
            break;
        }

        case OP_SETTABLE: {
            /* R[A][R[B]] := RK(C)：table/key/value(R) 若 boxed 先解 _v
             * helper 栈消费：lua_tcc_settable(-3) → 消费 key+value；-3位置的 table 保留在栈顶 */
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            emit_read_local(B, a + 1, boxed_slots, &obf_seed, obfuscate); /* table (+1) */
            emit_read_local(B, b + 1, boxed_slots, &obf_seed, obfuscate); /* key (+1, 总+2) */
            if (TESTARG_k(i)) emit_loadk(B, p, c, str_encrypt, seed, obfuscate); /* value K (+1, 总+3) */
            else emit_read_local(B, c + 1, boxed_slots, &obf_seed, obfuscate); /* value R (+1, 总+3) */
            add_fmt(B, "    lua_tcc_settable(L, %s);\n", obf_int(-3, &obf_seed, obfuscate)); /* 消费 key+value，栈顶剩 table (净+1) */
            add_fmt(B, "    lua_pop(L, %s);\n", obf_int(1, &obf_seed, obfuscate)); /* 清 table，恢复平衡 */
            break;
        }

        case OP_GETFIELD: {
            /* R[A] := R[B][K[C]]：table 若 boxed 先解 _v；结果 a+1 若 boxed 写 _v
             * helper 栈消费：lua_tcc_getfield(-1, k) → push result；原 emit 推的 table(-2 位置) 保留
             * 执行后栈[-2]=table, [-1]=result */
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            emit_read_local(B, b + 1, boxed_slots, &obf_seed, obfuscate); /* table (+1) */
            TValue *k = &p->k[c];
            if (ttisstring(k)) {
                if (str_encrypt) {
                    emit_encrypted_string_push(B, getstr(tsvalue(k)), tsslen(tsvalue(k)), seed); /* key (+1, 总+2) */
                    add_fmt(B, "    lua_tcc_gettable(L, %s);\n", obf_int(-2, &obf_seed, obfuscate)); /* 消费2产1，净+1，table在-2 */
                } else {
                    add_fmt(B, "    lua_tcc_getfield(L, %s, ", obf_int(-1, &obf_seed, obfuscate));
                    emit_quoted_string(B, getstr(tsvalue(k)), tsslen(tsvalue(k)));
                    add_fmt(B, ");\n"); /* 消费1产1，净+1，table在-2 */
                }
            } else {
                add_fmt(B, "    lua_pushnil(L);\n"); /* Should not happen for GETFIELD */
            }
            add_fmt(B, "    lua_remove(L, %s);\n", obf_int(-2, &obf_seed, obfuscate)); /* 移除 table，栈顶=result (净0) */
            emit_write_local(B, a + 1, boxed_slots, &obf_seed, obfuscate); /* 写回 a+1 */
            break;
        }

        case OP_SETFIELD: {
            /* R[A][K[B]] := RK(C)：table/value(R) 若 boxed 先解 _v
             * helper 栈消费：lua_tcc_setfield(-2, k) → 消费 value；-2位置的 table 保留在 -2 位置（即栈顶如果 value 被消费）*/
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            emit_read_local(B, a + 1, boxed_slots, &obf_seed, obfuscate); /* table (+1) */
            if (TESTARG_k(i)) emit_loadk(B, p, c, str_encrypt, seed, obfuscate); /* value K (+1, 总+2) */
            else emit_read_local(B, c + 1, boxed_slots, &obf_seed, obfuscate); /* value R (+1, 总+2) */
            TValue *key = &p->k[b];
            if (ttisstring(key)) {
                if (str_encrypt) {
                    emit_encrypted_string_push(B, getstr(tsvalue(key)), tsslen(tsvalue(key)), seed); /* key (+1, 总+3) */
                    add_fmt(B, "    lua_insert(L, %s);\n", obf_int(-2, &obf_seed, obfuscate)); /* 顺序: table,key,value (-3,-2,-1) */
                    add_fmt(B, "    lua_tcc_settable(L, %s);\n", obf_int(-3, &obf_seed, obfuscate)); /* 消费 key+value，栈顶剩table(净+1) */
                } else {
                    add_fmt(B, "    lua_tcc_setfield(L, %s, ", obf_int(-2, &obf_seed, obfuscate));
                    emit_quoted_string(B, getstr(tsvalue(key)), tsslen(tsvalue(key)));
                    add_fmt(B, ");\n"); /* 消费 value，栈顶剩table(净+1) */
                }
            } else {
                add_fmt(B, "    lua_pop(L, %s);\n", obf_int(2, &obf_seed, obfuscate)); /* pop table+value */
            }
            add_fmt(B, "    lua_pop(L, %s);\n", obf_int(1, &obf_seed, obfuscate)); /* 清 table，恢复平衡 */
            break;
        }

        case OP_GETI: {
            /* R[A] := R[B][C]：table 若 boxed 先解 _v；结果若 boxed 写 _v
             * helper 栈消费：lua_tcc_geti → push result；原 table(-2位置)保留 */
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            emit_read_local(B, b + 1, boxed_slots, &obf_seed, obfuscate); /* table (+1) */
            add_fmt(B, "    lua_tcc_geti(L, %s, %d);\n", obf_int(-1, &obf_seed, obfuscate), c); /* 消费1产1，净+1，table在-2 */
            add_fmt(B, "    lua_remove(L, %s);\n", obf_int(-2, &obf_seed, obfuscate)); /* 移除 table，栈顶=result(净0) */
            emit_write_local(B, a + 1, boxed_slots, &obf_seed, obfuscate);
            break;
        }

        case OP_SETI: {
            /* R[A][I] := RK(C)：table/value(R) 若 boxed 先解 _v
             * helper 栈消费：lua_tcc_seti(-2, n) → 消费 value；table(-2位置)保留在栈上 */
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            emit_read_local(B, a + 1, boxed_slots, &obf_seed, obfuscate); /* table (+1) */
            if (TESTARG_k(i)) emit_loadk(B, p, c, str_encrypt, seed, obfuscate); /* value K (+1, 总+2) */
            else emit_read_local(B, c + 1, boxed_slots, &obf_seed, obfuscate); /* value R (+1, 总+2) */
            add_fmt(B, "    lua_tcc_seti(L, %s, %d);\n", obf_int(-2, &obf_seed, obfuscate), b); /* 消费 value，栈剩table(净+1) */
            add_fmt(B, "    lua_pop(L, %s);\n", obf_int(1, &obf_seed, obfuscate)); /* 清 table，恢复平衡 */
            break;
        }

        case OP_SETLIST: {
            int n = GETARG_vB(i);
            unsigned int c = GETARG_vC(i);
            if (TESTARG_k(i)) {
                if (pc + 1 < p->sizecode && GET_OPCODE(p->code[pc+1]) == OP_EXTRAARG) {
                    int ax = GETARG_Ax(p->code[pc+1]);
                    c += ax * (MAXARG_C + 1);
                }
            }

            add_fmt(B, "    {\n");
            add_fmt(B, "        int n = %s;\n", obf_int(n, &obf_seed, obfuscate));
            add_fmt(B, "        if (n == 0) {\n");
            if (p->is_vararg) {
                add_fmt(B, "            if (vtab_idx == lua_gettop(L)) {\n");
                add_fmt(B, "                n = lua_gettop(L) - %s - %s;\n", obf_int(a + 1, &obf_seed, obfuscate), obf_int(1, &obf_seed, obfuscate));
                add_fmt(B, "            } else {\n");
                add_fmt(B, "                n = lua_gettop(L) - %s;\n", obf_int(a + 1, &obf_seed, obfuscate));
                add_fmt(B, "            }\n");
            } else {
                add_fmt(B, "            n = lua_gettop(L) - %s;\n", obf_int(a + 1, &obf_seed, obfuscate));
            }
            add_fmt(B, "        }\n");
            add_fmt(B, "        lua_pushvalue(L, %s); /* table */\n", obf_int(a + 1, &obf_seed, obfuscate));
            add_fmt(B, "        for (int j = 1; j <= n; j++) {\n");
            add_fmt(B, "            lua_pushvalue(L, %s + j);\n", obf_int(a + 1, &obf_seed, obfuscate));
        add_fmt(B, "            lua_seti(L, %s, %d + j);\n", obf_int(-2, &obf_seed, obfuscate), c);
            add_fmt(B, "        }\n");
            add_fmt(B, "        lua_pop(L, %s);\n", obf_int(1, &obf_seed, obfuscate));
            if (n == 0) {
                 if (p->is_vararg) {
                      /* 修复：vtab 暂存 → settop(maxstacksize) → 恢复到栈顶 = maxstacksize+1
                       * 避免 lua_replace 目标索引越界 */
                      add_fmt(B, "    {\n");
                      add_fmt(B, "        int __vtab_r;\n");
                      add_fmt(B, "        lua_pushvalue(L, vtab_idx); __vtab_r = luaL_ref(L, LUA_REGISTRYINDEX);\n");
                      add_fmt(B, "        lua_settop(L, %s);\n", obf_int(p->maxstacksize, &obf_seed, obfuscate));
                      add_fmt(B, "        lua_rawgeti(L, LUA_REGISTRYINDEX, __vtab_r);\n");
                      add_fmt(B, "        luaL_unref(L, LUA_REGISTRYINDEX, __vtab_r);\n");
                      add_fmt(B, "        vtab_idx = %s;\n", obf_int(p->maxstacksize + 1, &obf_seed, obfuscate));
                      add_fmt(B, "    }\n");
                 } else {
                      add_fmt(B, "    lua_settop(L, %s);\n", obf_int(p->maxstacksize, &obf_seed, obfuscate));
                 }
            }
            add_fmt(B, "    }\n");
            break;
        }

        case OP_FORPREP: {
            int bx = GETARG_Bx(i);
            char target_label[16];
            get_label_name(target_label, sizeof(target_label), pc + 1 + bx + 1, seed, obfuscate);
            add_fmt(B, "    {\n");
            add_fmt(B, "        if (lua_isinteger(L, %s) && lua_isinteger(L, %s)) {\n", obf_int(a + 1, &obf_seed, obfuscate), obf_int(a + 3, &obf_seed, obfuscate));
            add_fmt(B, "            lua_Integer step = lua_tointeger(L, %s);\n", obf_int(a + 3, &obf_seed, obfuscate));
            add_fmt(B, "            lua_Integer init = lua_tointeger(L, %s);\n", obf_int(a + 1, &obf_seed, obfuscate));
            add_fmt(B, "            lua_pushinteger(L, init);\n");
            add_fmt(B, "            lua_replace(L, %s);\n", obf_int(a + 4, &obf_seed, obfuscate));
            add_fmt(B, "            lua_pushinteger(L, init);\n");
            add_fmt(B, "            lua_replace(L, %s);\n", obf_int(a + 1, &obf_seed, obfuscate));
            add_fmt(B, "            if (lua_isinteger(L, %s)) {\n", obf_int(a + 2, &obf_seed, obfuscate));
            add_fmt(B, "                lua_Integer limit = lua_tointeger(L, %s);\n", obf_int(a + 2, &obf_seed, obfuscate));
            add_fmt(B, "                if ((step > 0) ? (init > limit) : (init < limit)) goto %s;\n", target_label);
            add_fmt(B, "            } else {\n");
            add_fmt(B, "                lua_Number flim = lua_tonumber(L, %s);\n", obf_int(a + 2, &obf_seed, obfuscate));
            add_fmt(B, "                if (flim > 0 && step < 0) goto %s;\n", target_label);
            add_fmt(B, "                if (flim < 0 && step > 0) goto %s;\n", target_label);
            add_fmt(B, "                lua_Integer limit = (step > 0) ? LUA_MAXINTEGER : LUA_MININTEGER;\n");
            add_fmt(B, "                if ((step > 0) ? (init > limit) : (init < limit)) goto %s;\n", target_label);
            add_fmt(B, "            }\n");
            add_fmt(B, "        } else {\n");
            add_fmt(B, "            lua_Number step = lua_tonumber(L, %s);\n", obf_int(a + 3, &obf_seed, obfuscate));
            add_fmt(B, "            lua_Number init = lua_tonumber(L, %s);\n", obf_int(a + 1, &obf_seed, obfuscate));
            add_fmt(B, "            lua_Number limit = lua_tonumber(L, %s);\n", obf_int(a + 2, &obf_seed, obfuscate));
            add_fmt(B, "            lua_pushnumber(L, init);\n");
            add_fmt(B, "            lua_replace(L, %s);\n", obf_int(a + 4, &obf_seed, obfuscate));
            add_fmt(B, "            lua_pushnumber(L, init);\n");
            add_fmt(B, "            lua_replace(L, %s);\n", obf_int(a + 1, &obf_seed, obfuscate));
            add_fmt(B, "            if ((step > 0) ? (limit < init) : (init < limit)) goto %s;\n", target_label);
            add_fmt(B, "        }\n");
            add_fmt(B, "    }\n");
            break;
        }

        case OP_FORLOOP: {
            int bx = GETARG_Bx(i);
            char target_label[16];
            get_label_name(target_label, sizeof(target_label), pc + 2 - bx, seed, obfuscate);
            add_fmt(B, "    {\n");
            add_fmt(B, "        if (lua_isinteger(L, %s)) {\n", obf_int(a + 3, &obf_seed, obfuscate));
            add_fmt(B, "            lua_Integer step = lua_tointeger(L, %s);\n", obf_int(a + 3, &obf_seed, obfuscate));
            add_fmt(B, "            lua_Integer limit = lua_tointeger(L, %s);\n", obf_int(a + 2, &obf_seed, obfuscate));
            add_fmt(B, "            lua_Integer idx = lua_tointeger(L, %s) + step;\n", obf_int(a + 1, &obf_seed, obfuscate));
            add_fmt(B, "            lua_pushinteger(L, idx);\n");
            add_fmt(B, "            lua_replace(L, %s);\n", obf_int(a + 1, &obf_seed, obfuscate));
            add_fmt(B, "            if ((step > 0) ? (idx <= limit) : (idx >= limit)) {\n");
            add_fmt(B, "                lua_pushinteger(L, idx);\n");
            add_fmt(B, "                lua_replace(L, %s);\n", obf_int(a + 4, &obf_seed, obfuscate));
            add_fmt(B, "                goto %s;\n", target_label);
            add_fmt(B, "            }\n");
            add_fmt(B, "        } else {\n");
            add_fmt(B, "            lua_Number step = lua_tonumber(L, %s);\n", obf_int(a + 3, &obf_seed, obfuscate));
            add_fmt(B, "            lua_Number limit = lua_tonumber(L, %s);\n", obf_int(a + 2, &obf_seed, obfuscate));
            add_fmt(B, "            lua_Number idx = lua_tonumber(L, %s) + step;\n", obf_int(a + 1, &obf_seed, obfuscate));
            add_fmt(B, "            lua_pushnumber(L, idx);\n");
            add_fmt(B, "            lua_replace(L, %s);\n", obf_int(a + 1, &obf_seed, obfuscate));
            add_fmt(B, "            if ((step > 0) ? (idx <= limit) : (idx >= limit)) {\n");
            add_fmt(B, "                lua_pushnumber(L, idx);\n");
            add_fmt(B, "                lua_replace(L, %s);\n", obf_int(a + 4, &obf_seed, obfuscate));
            add_fmt(B, "                goto %s;\n", target_label);
            add_fmt(B, "            }\n");
            add_fmt(B, "        }\n");
            add_fmt(B, "    }\n");
            break;
        }

        case OP_TFORPREP: {
            int bx = GETARG_Bx(i);
            char target_label[16];
            get_label_name(target_label, sizeof(target_label), pc + 1 + bx + 1, seed, obfuscate);
            add_fmt(B, "    lua_toclose(L, %s);\n", obf_int(a + 3 + 1, &obf_seed, obfuscate));
            add_fmt(B, "    goto %s;\n", target_label);
            break;
        }

        case OP_TFORCALL: {
            int c = GETARG_C(i);
            add_fmt(B, "    lua_pushvalue(L, %s);\n", obf_int(a + 1, &obf_seed, obfuscate));
            add_fmt(B, "    lua_pushvalue(L, %s);\n", obf_int(a + 2, &obf_seed, obfuscate));
            add_fmt(B, "    lua_pushvalue(L, %s);\n", obf_int(a + 3, &obf_seed, obfuscate));
            add_fmt(B, "    lua_call(L, %s, %s);\n", obf_int(2, &obf_seed, obfuscate), obf_int(c, &obf_seed, obfuscate));
            for (int k = c; k >= 1; k--) {
                add_fmt(B, "    lua_replace(L, %s);\n", obf_int(a + 4 + k, &obf_seed, obfuscate));
            }
            break;
        }

        case OP_TFORLOOP: {
            int bx = GETARG_Bx(i);
            char target_label[16];
            get_label_name(target_label, sizeof(target_label), pc + 2 - bx, seed, obfuscate);
            add_fmt(B, "    if (!lua_isnil(L, %s)) {\n", obf_int(a + 5, &obf_seed, obfuscate));
            add_fmt(B, "        lua_pushvalue(L, %s);\n", obf_int(a + 5, &obf_seed, obfuscate));
            add_fmt(B, "        lua_replace(L, %s);\n", obf_int(a + 3, &obf_seed, obfuscate));
            add_fmt(B, "        goto %s;\n", target_label);
            add_fmt(B, "    }\n");
            break;
        }

        case OP_TEST: {
            int k = GETARG_k(i);
            char target_label[16];
            get_label_name(target_label, sizeof(target_label), pc + 1 + 2, seed, obfuscate);
            /* 修复：boxed slot 先解 _v 再判断 boolean，否则 table 永远为 true */
            if (boxed_slots && boxed_slots[a + 1]) {
                add_fmt(B, "    lua_getfield(L, %s, \"_v\");\n", obf_int(a + 1, &obf_seed, obfuscate));
                add_fmt(B, "    if (lua_toboolean(L, -1) != %d) goto %s;\n", k, target_label);
                add_fmt(B, "    lua_pop(L, 1);\n");
            } else {
                add_fmt(B, "    if (lua_toboolean(L, %s) != %d) goto %s;\n", obf_int(a + 1, &obf_seed, obfuscate), k, target_label);
            }
            break;
        }

        case OP_TESTSET: {
            int b = GETARG_B(i);
            int k = GETARG_k(i);
            char target_label[16];
            get_label_name(target_label, sizeof(target_label), pc + 1 + 2, seed, obfuscate);
            /* 修复：
             * 1) 判断 boolean：boxed slot 先解 _v 再 lua_toboolean
             * 2) 取值：boxed slot 解 _v；写槽：boxed slot 写 _v */
            if (boxed_slots && boxed_slots[b + 1]) {
                add_fmt(B, "    lua_getfield(L, %s, \"_v\");\n", obf_int(b + 1, &obf_seed, obfuscate));
                add_fmt(B, "    if (lua_toboolean(L, -1) != %d) { lua_pop(L, 1); goto %s; }\n", k, target_label);
                add_fmt(B, "    lua_pop(L, 1);\n");
            } else {
                add_fmt(B, "    if (lua_toboolean(L, %s) != %d) goto %s;\n", obf_int(b + 1, &obf_seed, obfuscate), k, target_label);
            }
            emit_read_local(B, b + 1, boxed_slots, &obf_seed, obfuscate);
            emit_write_local(B, a + 1, boxed_slots, &obf_seed, obfuscate);
            break;
        }

        case OP_TESTNIL: {
            int b = GETARG_B(i);
            int k = GETARG_k(i);
            char target_label[16];
            get_label_name(target_label, sizeof(target_label), pc + 1 + 2, seed, obfuscate);
            /* 修复：TESTNIL 语义 if (R[B] is nil) != k then pc++（跳过 JMP 执行后续）
             * 即 isnil != k 时跳 field_label（pc+2），否则 fallthrough 到 JMP（跳短路）
             * 之前写反了 == k，导致 ?. 可选链 nil 时反而去取 field 崩溃
             * boxed slot：需要解 _v 后再 isnil 判断 */
            if (boxed_slots && boxed_slots[b + 1]) {
                add_fmt(B, "    lua_getfield(L, %s, \"_v\");\n", obf_int(b + 1, &obf_seed, obfuscate));
                add_fmt(B, "    if (lua_isnil(L, -1) != %d) { lua_pop(L, 1); goto %s; }\n", k, target_label);
                add_fmt(B, "    lua_pop(L, 1);\n");
            } else {
                add_fmt(B, "    if (lua_isnil(L, %s) != %d) goto %s;\n", obf_int(b + 1, &obf_seed, obfuscate), k, target_label);
            }
            int a = GETARG_A(i);
            if (a != MAXARG_A) {
                 emit_read_local(B, b + 1, boxed_slots, &obf_seed, obfuscate);
                 emit_write_local(B, a + 1, boxed_slots, &obf_seed, obfuscate);
            }
            break;
        }

        case OP_NEWCLASS: {
            int bx = GETARG_Bx(i);
            emit_loadk(B, p, bx, str_encrypt, seed, obfuscate);
            add_fmt(B, "    lua_newclass(L, lua_tostring(L, %s));\n", obf_int(-1, &obf_seed, obfuscate));
            add_fmt(B, "    lua_replace(L, %s);\n", obf_int(a + 1, &obf_seed, obfuscate));
            add_fmt(B, "    lua_pop(L, %s);\n", obf_int(1, &obf_seed, obfuscate));
            break;
        }

        case OP_INHERIT: {
            int b = GETARG_B(i);
            add_fmt(B, "    lua_inherit(L, %s, %s);\n", obf_int(a + 1, &obf_seed, obfuscate), obf_int(b + 1, &obf_seed, obfuscate));
            break;
        }

        case OP_MULTIINHERIT: {
            int b = GETARG_B(i);
            /* 遍历父类数组表，逐个调用 lua_inherit */
            add_fmt(B, "    lua_pushnil(L);\n");
            add_fmt(B, "    while (lua_next(L, %s) != 0) {\n", obf_int(b + 1, &obf_seed, obfuscate));
            add_fmt(B, "        lua_pushvalue(L, %s);\n", obf_int(a + 1, &obf_seed, obfuscate));
            add_fmt(B, "        lua_pushvalue(L, -3);\n");
            add_fmt(B, "        lua_inherit(L, -2, -1);\n");
            add_fmt(B, "        lua_pop(L, %s);\n", obf_int(3, &obf_seed, obfuscate));
            add_fmt(B, "    }\n");
            break;
        }

        case OP_SETMETHOD: {
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            emit_loadk(B, p, b, str_encrypt, seed, obfuscate);
            add_fmt(B, "    lua_setmethod(L, %s, lua_tostring(L, %s), %s);\n", obf_int(a + 1, &obf_seed, obfuscate), obf_int(-1, &obf_seed, obfuscate), obf_int(c + 1, &obf_seed, obfuscate));
            add_fmt(B, "    lua_pop(L, %s);\n", obf_int(1, &obf_seed, obfuscate));
            break;
        }

        case OP_CHECKOVERRIDE: {
            int b = GETARG_B(i);
            emit_loadk(B, p, b, str_encrypt, seed, obfuscate);
            add_fmt(B, "    lua_pushvalue(L, %s);\n", obf_int(a + 1, &obf_seed, obfuscate));
            add_fmt(B, "    lua_checkoverride(L, %s, lua_tostring(L, %s));\n", obf_int(-1, &obf_seed, obfuscate), obf_int(-1, &obf_seed, obfuscate));
            add_fmt(B, "    lua_pop(L, %s);\n", obf_int(1, &obf_seed, obfuscate));
            break;
        }

        case OP_SETSTATIC: {
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            emit_loadk(B, p, b, str_encrypt, seed, obfuscate);
            add_fmt(B, "    lua_setstatic(L, %s, lua_tostring(L, %s), %s);\n", obf_int(a + 1, &obf_seed, obfuscate), obf_int(-1, &obf_seed, obfuscate), obf_int(c + 1, &obf_seed, obfuscate));
            add_fmt(B, "    lua_pop(L, %s);\n", obf_int(1, &obf_seed, obfuscate));
            break;
        }

        case OP_GETSUPER: {
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            add_fmt(B, "    lua_pushvalue(L, %s);\n", obf_int(b + 1, &obf_seed, obfuscate));
            emit_loadk(B, p, c, str_encrypt, seed, obfuscate);
            add_fmt(B, "    lua_getsuper(L, %s, lua_tostring(L, %s));\n", obf_int(-2, &obf_seed, obfuscate), obf_int(-1, &obf_seed, obfuscate));
            add_fmt(B, "    lua_replace(L, %s);\n", obf_int(a + 1, &obf_seed, obfuscate));
            add_fmt(B, "    lua_pop(L, %s);\n", obf_int(2, &obf_seed, obfuscate));
            break;
        }

        case OP_NEWOBJ: {
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            int nargs = c - 1;
            add_fmt(B, "    lua_pushvalue(L, %s);\n", obf_int(b + 1, &obf_seed, obfuscate));
            for (int k = 0; k < nargs; k++) {
                add_fmt(B, "    lua_pushvalue(L, %s + k);\n", obf_int(a + 1, &obf_seed, obfuscate));
            }
            add_fmt(B, "    lua_newobject(L, -%s, %d);\n", obf_int(nargs + 1, &obf_seed, obfuscate), nargs);
            add_fmt(B, "    lua_replace(L, %s);\n", obf_int(a + 1, &obf_seed, obfuscate));
            add_fmt(B, "    lua_pop(L, %s);\n", obf_int(1, &obf_seed, obfuscate));
            break;
        }

        case OP_GETPROP: {
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            add_fmt(B, "    lua_pushvalue(L, %s);\n", obf_int(b + 1, &obf_seed, obfuscate));
            emit_loadk(B, p, c, str_encrypt, seed, obfuscate);
            add_fmt(B, "    lua_getprop(L, %s, lua_tostring(L, %s));\n", obf_int(-2, &obf_seed, obfuscate), obf_int(-1, &obf_seed, obfuscate));
            add_fmt(B, "    lua_replace(L, %s);\n", obf_int(a + 1, &obf_seed, obfuscate));
            add_fmt(B, "    lua_pop(L, %s);\n", obf_int(2, &obf_seed, obfuscate));
            break;
        }

        case OP_SETPROP: {
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            add_fmt(B, "    lua_pushvalue(L, %s);\n", obf_int(a + 1, &obf_seed, obfuscate));
            emit_loadk(B, p, b, str_encrypt, seed, obfuscate);
            if (TESTARG_k(i)) emit_loadk(B, p, c, str_encrypt, seed, obfuscate);
            else add_fmt(B, "    lua_pushvalue(L, %s);\n", obf_int(c + 1, &obf_seed, obfuscate));
            add_fmt(B, "    lua_setprop(L, %s, lua_tostring(L, %s), %s);\n", obf_int(-3, &obf_seed, obfuscate), obf_int(-2, &obf_seed, obfuscate), obf_int(-1, &obf_seed, obfuscate));
            add_fmt(B, "    lua_pop(L, %s);\n", obf_int(3, &obf_seed, obfuscate));
            break;
        }

        case OP_INSTANCEOF: {
            int b = GETARG_B(i);
            int k = GETARG_k(i);
            char target_label[16];
            get_label_name(target_label, sizeof(target_label), pc + 1 + 2, seed, obfuscate);
            add_fmt(B, "    lua_pushvalue(L, %s);\n", obf_int(a + 1, &obf_seed, obfuscate));
            add_fmt(B, "    lua_pushvalue(L, %s);\n", obf_int(b + 1, &obf_seed, obfuscate));
            add_fmt(B, "    if (lua_instanceof(L, %s, %s) != %d) goto %s;\n", obf_int(-2, &obf_seed, obfuscate), obf_int(-1, &obf_seed, obfuscate), k, target_label);
            add_fmt(B, "    lua_pop(L, %s);\n", obf_int(2, &obf_seed, obfuscate));
            break;
        }

        case OP_ASCLASS: {
            /* 安全类型转换：R[A] = (R[B] instanceof R[C]) ? R[B] : nil */
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            add_fmt(B, "    lua_pushvalue(L, %s);\n", obf_int(b + 1, &obf_seed, obfuscate));
            add_fmt(B, "    lua_pushvalue(L, %s);\n", obf_int(c + 1, &obf_seed, obfuscate));
            add_fmt(B, "    if (lua_instanceof(L, %s, %s)) {\n", obf_int(-2, &obf_seed, obfuscate), obf_int(-1, &obf_seed, obfuscate));
            add_fmt(B, "        lua_pop(L, %s);\n", obf_int(2, &obf_seed, obfuscate));
            add_fmt(B, "        lua_pushvalue(L, %s);\n", obf_int(b + 1, &obf_seed, obfuscate));
            add_fmt(B, "        lua_replace(L, %s);\n", obf_int(a + 1, &obf_seed, obfuscate));
            add_fmt(B, "    } else {\n");
            add_fmt(B, "        lua_pop(L, %s);\n", obf_int(2, &obf_seed, obfuscate));
            add_fmt(B, "        lua_pushnil(L);\n");
            add_fmt(B, "        lua_replace(L, %s);\n", obf_int(a + 1, &obf_seed, obfuscate));
            add_fmt(B, "    }\n");
            break;
        }

        case OP_IMPLEMENT: {
            int b = GETARG_B(i);
            add_fmt(B, "    lua_implement(L, %s, %s);\n", obf_int(a + 1, &obf_seed, obfuscate), obf_int(b + 1, &obf_seed, obfuscate));
            break;
        }

        case OP_ASYNCWRAP: {
            /*
             * 纯语法级 async 标记：PF_ASYNC 标志在字节码加载时由 VM 自动设置。
             * 字节码转C编译器不需要额外处理，因为生成的 C 代码中函数原型
             * 已包含 PF_ASYNC 标志。
             */
            add_fmt(B, "    /* OP_ASYNCWRAP: async marker (PF_ASYNC set in proto) */\n");
            break;
        }

        case OP_GENERICWRAP: {
            int b = GETARG_B(i);
            add_fmt(B, "    lua_getglobal(L, \"__generic_wrap\");\n");
            add_fmt(B, "    if (lua_isfunction(L, %s)) {\n", obf_int(-1, &obf_seed, obfuscate));
            add_fmt(B, "        lua_pushvalue(L, %s);\n", obf_int(b + 1, &obf_seed, obfuscate));
            add_fmt(B, "        lua_pushvalue(L, %s);\n", obf_int(b + 2, &obf_seed, obfuscate));
            add_fmt(B, "        lua_pushvalue(L, %s);\n", obf_int(b + 3, &obf_seed, obfuscate));
            add_fmt(B, "        lua_call(L, %s, %s);\n", obf_int(3, &obf_seed, obfuscate), obf_int(1, &obf_seed, obfuscate));
            add_fmt(B, "        lua_replace(L, %s);\n", obf_int(a + 1, &obf_seed, obfuscate));
            add_fmt(B, "    } else {\n");
            add_fmt(B, "        lua_pop(L, %s);\n", obf_int(1, &obf_seed, obfuscate));
            add_fmt(B, "    }\n");
            break;
        }

        case OP_CHECKTYPE: {
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            add_fmt(B, "    lua_pushvalue(L, %s);\n", obf_int(b + 1, &obf_seed, obfuscate));
            emit_loadk(B, p, c, str_encrypt, seed, obfuscate); /* name */
            add_fmt(B, "    lua_checktype(L, %s, lua_tostring(L, %s));\n", obf_int(a + 1, &obf_seed, obfuscate), obf_int(-1, &obf_seed, obfuscate));
            add_fmt(B, "    lua_pop(L, %s);\n", obf_int(2, &obf_seed, obfuscate));
            break;
        }

        case OP_SPACESHIP: {
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            add_fmt(B, "    lua_pushinteger(L, lua_spaceship(L, %s, %s));\n", obf_int(b + 1, &obf_seed, obfuscate), obf_int(c + 1, &obf_seed, obfuscate));
            add_fmt(B, "    lua_replace(L, %s);\n", obf_int(a + 1, &obf_seed, obfuscate));
            break;
        }

        case OP_IS: {
            int b = GETARG_B(i);
            int k = GETARG_k(i);
            char target_label[16];
            get_label_name(target_label, sizeof(target_label), pc + 1 + 2, seed, obfuscate);
            add_fmt(B, "    {\n");
            emit_loadk(B, p, b, str_encrypt, seed, obfuscate); // Push type name K[B]
            add_fmt(B, "        int res = lua_is(L, %s, lua_tostring(L, %s));\n", obf_int(a + 1, &obf_seed, obfuscate), obf_int(-1, &obf_seed, obfuscate));
            add_fmt(B, "        lua_pop(L, %s);\n", obf_int(1, &obf_seed, obfuscate));
            add_fmt(B, "        if (res != %s) goto %s;\n", obf_int(k, &obf_seed, obfuscate), target_label);
            add_fmt(B, "    }\n");
            break;
        }

        case OP_NEWNAMESPACE: {
            int bx = GETARG_Bx(i);
            emit_loadk(B, p, bx, str_encrypt, seed, obfuscate);
            add_fmt(B, "    lua_newnamespace(L, lua_tostring(L, %s));\n", obf_int(-1, &obf_seed, obfuscate));
            add_fmt(B, "    lua_replace(L, %s);\n", obf_int(a + 1, &obf_seed, obfuscate));
            add_fmt(B, "    lua_pop(L, %s);\n", obf_int(1, &obf_seed, obfuscate));
            break;
        }

        case OP_LINKNAMESPACE: {
            int b = GETARG_B(i);
            add_fmt(B, "    lua_linknamespace(L, %s, %s);\n", obf_int(a + 1, &obf_seed, obfuscate), obf_int(b + 1, &obf_seed, obfuscate));
            break;
        }

        case OP_NEWSUPER: {
            int bx = GETARG_Bx(i);
            emit_loadk(B, p, bx, str_encrypt, seed, obfuscate);
            add_fmt(B, "    lua_newsuperstruct(L, lua_tostring(L, %s));\n", obf_int(-1, &obf_seed, obfuscate));
            add_fmt(B, "    lua_replace(L, %s);\n", obf_int(a + 1, &obf_seed, obfuscate));
            add_fmt(B, "    lua_pop(L, %s);\n", obf_int(1, &obf_seed, obfuscate));
            break;
        }

        case OP_SETSUPER: {
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            add_fmt(B, "    lua_setsuper(L, %s, %s, %s);\n", obf_int(a + 1, &obf_seed, obfuscate), obf_int(b + 1, &obf_seed, obfuscate), obf_int(c + 1, &obf_seed, obfuscate));
            break;
        }

        case OP_SLICE: {
            int b = GETARG_B(i);
            add_fmt(B, "    lua_slice(L, %s, %s, %s, %s);\n", obf_int(b + 1, &obf_seed, obfuscate), obf_int(b + 2, &obf_seed, obfuscate), obf_int(b + 3, &obf_seed, obfuscate), obf_int(b + 4, &obf_seed, obfuscate));
            add_fmt(B, "    lua_replace(L, %s);\n", obf_int(a + 1, &obf_seed, obfuscate));
            break;
        }

        case OP_SETIFACEFLAG: {
            add_fmt(B, "    lua_setifaceflag(L, %s);\n", obf_int(a + 1, &obf_seed, obfuscate));
            break;
        }

        case OP_ADDMETHOD: {
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            emit_loadk(B, p, b, str_encrypt, seed, obfuscate); // method name
            add_fmt(B, "    lua_addmethod(L, %s, lua_tostring(L, %s), %s);\n", obf_int(a + 1, &obf_seed, obfuscate), obf_int(-1, &obf_seed, obfuscate), obf_int(c, &obf_seed, obfuscate));
            add_fmt(B, "    lua_pop(L, %s);\n", obf_int(1, &obf_seed, obfuscate));
            break;
        }

        case OP_EXTENDIFACE: {
            int b = GETARG_B(i);
            add_fmt(B, "    lua_extendiface(L, %s, %s);\n", obf_int(a + 1, &obf_seed, obfuscate), obf_int(b + 1, &obf_seed, obfuscate));
            break;
        }

        case OP_GETCMDS: {
            add_fmt(B, "    lua_getcmds(L);\n");
            add_fmt(B, "    lua_replace(L, %s);\n", obf_int(a + 1, &obf_seed, obfuscate));
            break;
        }

        case OP_GETOPS: {
            add_fmt(B, "    lua_getops(L);\n");
            add_fmt(B, "    lua_replace(L, %s);\n", obf_int(a + 1, &obf_seed, obfuscate));
            break;
        }

        case OP_ERRNNIL: {
            int bx = GETARG_Bx(i);
            if (bx > 0) {
                emit_loadk(B, p, bx - 1, str_encrypt, seed, obfuscate);
                add_fmt(B, "    lua_errnnil(L, %s, lua_tostring(L, %s));\n", obf_int(a + 1, &obf_seed, obfuscate), obf_int(-1, &obf_seed, obfuscate));
                add_fmt(B, "    lua_pop(L, %s);\n", obf_int(1, &obf_seed, obfuscate));
            } else {
                add_fmt(B, "    /* ERRNNIL: invalid bx=%d, skipping */\n", bx);
            }
            break;
        }

        case OP_TBC: {    }

        case OP_CASE: {
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            add_fmt(B, "    lua_createtable(L, 2, 0);\n");
            add_fmt(B, "    lua_pushvalue(L, %s);\n", obf_int(b + 1, &obf_seed, obfuscate));
            add_fmt(B, "    lua_rawseti(L, %s, 1);\n", obf_int(-2, &obf_seed, obfuscate));
            add_fmt(B, "    lua_pushvalue(L, %s);\n", obf_int(c + 1, &obf_seed, obfuscate));
            add_fmt(B, "    lua_rawseti(L, %s, 2);\n", obf_int(-2, &obf_seed, obfuscate));
            add_fmt(B, "    lua_replace(L, %s);\n", obf_int(a + 1, &obf_seed, obfuscate));
            break;
        }

        case OP_IN: {
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            add_fmt(B, "    lua_pushinteger(L, lua_tcc_in(L, %s, %s));\n", obf_int(b + 1, &obf_seed, obfuscate), obf_int(c + 1, &obf_seed, obfuscate));
            add_fmt(B, "    lua_replace(L, %s);\n", obf_int(a + 1, &obf_seed, obfuscate));
            break;
        }

        case OP_NOT: {
            /* R[A] := not R[B]：源 slot 若 boxed 先解 _v，结果若 boxed 写 _v 字段 */
            int b = GETARG_B(i);
            if (boxed_slots && boxed_slots[b + 1]) {
                add_fmt(B, "    lua_getfield(L, %s, \"_v\");\n", obf_int(b + 1, &obf_seed, obfuscate));
                add_fmt(B, "    lua_pushboolean(L, !lua_toboolean(L, %s));\n", obf_int(-1, &obf_seed, obfuscate));
                add_fmt(B, "    lua_remove(L, %s);\n", obf_int(-2, &obf_seed, obfuscate)); /* 移除解包前的值 */
            } else {
                add_fmt(B, "    lua_pushboolean(L, !lua_toboolean(L, %s));\n", obf_int(b + 1, &obf_seed, obfuscate));
            }
            emit_write_local(B, a + 1, boxed_slots, &obf_seed, obfuscate);
            break;
        }

        case OP_LEN: {
            /* R[A] := length of R[B]：源 slot 若 boxed 先解 _v，结果若 boxed 写 _v 字段 */
            int b = GETARG_B(i);
            emit_read_local(B, b + 1, boxed_slots, &obf_seed, obfuscate); /* 压入解包后的值到栈顶 */
            add_fmt(B, "    lua_len(L, %s);\n", obf_int(-1, &obf_seed, obfuscate));
            /* 现在栈顶是长度值，原压入的值在 -2，需要移除后再写回 */
            add_fmt(B, "    lua_remove(L, %s);\n", obf_int(-2, &obf_seed, obfuscate));
            emit_write_local(B, a + 1, boxed_slots, &obf_seed, obfuscate);
            break;
        }

        case OP_CONCAT: {
            /* R[A] := R[A].. ... ..R[A+B-1]：每个输入 slot 若 boxed 都要解 _v，结果写 _v */
            int b = GETARG_B(i);
            for (int k = 0; k < b; k++) {
                emit_read_local(B, a + 1 + k, boxed_slots, &obf_seed, obfuscate);
            }
            add_fmt(B, "    lua_concat(L, %s);\n", obf_int(b, &obf_seed, obfuscate));
            emit_write_local(B, a + 1, boxed_slots, &obf_seed, obfuscate);
            break;
        }

        case OP_CLOSE: {
            add_fmt(B, "    lua_closeslot(L, %s);\n", obf_int(a + 1, &obf_seed, obfuscate));
            break;
        }

        case OP_EXTRAARG:
            add_fmt(B, "    /* EXTRAARG */\n");
            break;

        case OP_NOP:
            if (!use_pure_c) add_fmt(B, "    __asm__ volatile (\"nop\");\n");
            else add_fmt(B, "    /* NOP */\n");
            break;

        /* === Map 容器操作 === */

        case OP_NEWMAP: {
            /* R[A] := [] (创建map容器)：目标 slot 若 boxed，写 _v 字段 */
            add_fmt(B, "    lua_tcc_newmap(L);\n");
            emit_write_local(B, a + 1, boxed_slots, &obf_seed, obfuscate);
            break;
        }

        case OP_MAPGET: {
            /* R[A] := R[B][R[C]] (map下标读取)：map/key 若 boxed 先解 _v，结果若 boxed 写 _v */
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            emit_read_local(B, b + 1, boxed_slots, &obf_seed, obfuscate); /* map */
            emit_read_local(B, c + 1, boxed_slots, &obf_seed, obfuscate); /* key */
            add_fmt(B, "    lua_tcc_mapget(L);\n");
            emit_write_local(B, a + 1, boxed_slots, &obf_seed, obfuscate);
            break;
        }

        case OP_MAPSET: {
            /* R[A][R[B]] := RK(C) (map下标赋值)：map/key/value 来自寄存器时，若 boxed 先解 _v */
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            emit_read_local(B, a + 1, boxed_slots, &obf_seed, obfuscate); /* map */
            emit_read_local(B, b + 1, boxed_slots, &obf_seed, obfuscate); /* key */
            if (TESTARG_k(i))
                emit_loadk(B, p, c, str_encrypt, seed, obfuscate);
            else
                emit_read_local(B, c + 1, boxed_slots, &obf_seed, obfuscate); /* value R */
            add_fmt(B, "    lua_tcc_mapset(L);\n");
            break;
        }

        /* === Trait 操作 === */

        case OP_SETTRAITFLAG: {
            /* 设置R[A]为Trait */
            add_fmt(B, "    lua_tcc_settraitflag(L, %s);\n", obf_int(a + 1, &obf_seed, obfuscate));
            break;
        }

        case OP_SETTRAITREQUIRE: {
            /* R[A].__trait_requires[K[B]] := C (注册trait所需方法) */
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            TValue *k = &p->k[b];
            if (ttisstring(k)) {
                if (str_encrypt) {
                    emit_encrypted_string_push(B, getstr(tsvalue(k)), tsslen(tsvalue(k)), seed);
                    add_fmt(B, "    lua_tcc_settraitrequire(L, %s, lua_tostring(L, %s), %s);\n",
                        obf_int(a + 1, &obf_seed, obfuscate), obf_int(-1, &obf_seed, obfuscate), obf_int(c, &obf_seed, obfuscate));
                    add_fmt(B, "    lua_pop(L, %s);\n", obf_int(1, &obf_seed, obfuscate));
                } else {
                    add_fmt(B, "    lua_tcc_settraitrequire(L, %s, ", obf_int(a + 1, &obf_seed, obfuscate));
                    emit_quoted_string(B, getstr(tsvalue(k)), tsslen(tsvalue(k)));
                    add_fmt(B, ", %s);\n", obf_int(c, &obf_seed, obfuscate));
                }
            }
            break;
        }

        case OP_USETRAIT: {
            /* R[A] use R[B] (将trait方法复制到类中) */
            int b = GETARG_B(i);
            add_fmt(B, "    lua_tcc_usetrait(L, %s, %s);\n", obf_int(a + 1, &obf_seed, obfuscate), obf_int(b + 1, &obf_seed, obfuscate));
            break;
        }
        case OP_STATICINIT: {
            /* 静态构造函数：调用类的 __statics.init */
            add_fmt(B, "    lua_tcc_staticinit(L, %s);\n", obf_int(a + 1, &obf_seed, obfuscate));
            break;
        }

        /* === 表合并操作 === */

        case OP_MERGE: {
            /* R[A] := merge(R[B], R[C]) (表合并) */
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            add_fmt(B, "    lua_pushvalue(L, %s);\n", obf_int(b + 1, &obf_seed, obfuscate));
            add_fmt(B, "    lua_pushvalue(L, %s);\n", obf_int(c + 1, &obf_seed, obfuscate));
            add_fmt(B, "    lua_tcc_merge(L);\n");
            add_fmt(B, "    lua_replace(L, %s);\n", obf_int(a + 1, &obf_seed, obfuscate));
            break;
        }

        /* === 正则字面量 === */

        case OP_REGEX: {
            /* R[A] := regex(K[Bx]) (正则字面量) */
            int bx = GETARG_Bx(i);
            TValue *k = &p->k[bx];
            if (ttisstring(k)) {
                const char *data = getstr(tsvalue(k));
                if (str_encrypt) {
                    emit_encrypted_string_push(B, data, strlen(data), seed);
                    add_fmt(B, "    lua_tcc_regex(L, lua_tostring(L, %s));\n", obf_int(-1, &obf_seed, obfuscate));
                    add_fmt(B, "    lua_pop(L, %s);\n", obf_int(1, &obf_seed, obfuscate));
                } else {
                    add_fmt(B, "    lua_tcc_regex(L, ");
                    emit_quoted_string(B, data, strlen(data));
                    add_fmt(B, ");\n");
                }
                add_fmt(B, "    lua_replace(L, %s);\n", obf_int(a + 1, &obf_seed, obfuscate));
            }
            break;
        }

        /* === 不支持在编译C代码中使用的操作 === */

        case OP_AWAIT: {
            /* R[A] := await(R[B]) */
            int b = GETARG_B(i);
            add_fmt(B, "    lua_tcc_await(L, %s, %s);\n",
                obf_int(b + 1, &obf_seed, obfuscate), obf_int(a + 1, &obf_seed, obfuscate));
            break;
        }

        case OP_CUSTOM: {
            /* 自定义opcode：调用运行时注册的处理器 */
            lua_Unsigned user_op = GETARG_Ax(i);
            add_fmt(B, "    {\n");
            add_fmt(B, "        int nres = lua_tcc_custom(L, %llu);\n", user_op);
            add_fmt(B, "        if (nres > 0) {\n");
            add_fmt(B, "            lua_tcc_store_results(L, %s, nres);\n", obf_int(a + 1, &obf_seed, obfuscate));
            add_fmt(B, "        }\n");
            add_fmt(B, "    }\n");
            break;
        }

        default:
            add_fmt(B, "    /* Unimplemented opcode: %s */\n", opnames[op]);
            break;
    }
}

/**
 * @brief 扫描字节码，计算实际使用的最大寄存器索引
 * 用于修正 obfuscate 后 maxstacksize 可能不准确的问题
 * @param p Proto 指针
 * @return 实际最大寄存器索引
 */
static int compute_max_reg(Proto *p) {
    int max_reg = 0;
    for (int i = 0; i < p->sizecode; i++) {
        Instruction instr = p->code[i];
        OpCode op = GET_OPCODE(instr);
        int a = GETARG_A(instr);
        int b = GETARG_B(instr);
        int c = GETARG_C(instr);
        int bx = GETARG_Bx(instr);
        int sbx = GETARG_sBx(instr);

        /* A 寄存器 */
        if (a > max_reg) max_reg = a;

        switch (op) {
            /* 使用 A+B 的指令 */
            case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_IDIV:
            case OP_BAND: case OP_BOR: case OP_BXOR: case OP_SHL: case OP_SHR:
            case OP_MOD: case OP_POW: case OP_CONCAT:
                if (b > max_reg) max_reg = b;
                if (c > max_reg) max_reg = c;
                break;
            /* 使用 A+B 但 B 是寄存器 */
            case OP_MOVE: case OP_NOT: case OP_UNM: case OP_LEN: case OP_BNOT:
            case OP_LOADFALSE: case OP_LOADTRUE: case OP_LFALSESKIP:
            case OP_GETTABLE: case OP_GETTABUP: case OP_GETI:
            case OP_GETFIELD: case OP_SELF:
            case OP_SETTABLE: case OP_SETTABUP: case OP_SETI:
            case OP_SETFIELD:
            case OP_GETSUPER: case OP_INHERIT: case OP_MULTIINHERIT: case OP_CHECKOVERRIDE:
            case OP_ASCLASS:
                if (b > max_reg) max_reg = b;
                if (c > max_reg) max_reg = c;
                break;
            /* NEWTABLE 只有 A */
            case OP_NEWTABLE:
                break;
            /* LOADK/LOADKX: Bx 是常量表索引，不是寄存器，A 已在上方追踪 */
            /* 比较指令 */
            case OP_EQ: case OP_LT: case OP_LE:
                if (b > max_reg) max_reg = b;
                break;
            /* CALL 指令 */
            case OP_CALL: case OP_TAILCALL:
                if (a + c > max_reg) max_reg = a + c;
                break;
            /* VARARG */
            case OP_VARARG:
                if (a + c > max_reg) max_reg = a + c;
                break;
            /* RETURN */
            case OP_RETURN: case OP_RETURN0: case OP_RETURN1:
                if (a + b > max_reg) max_reg = a + b;
                break;
            /* SETLIST */
            case OP_SETLIST:
                if (a + c > max_reg) max_reg = a + c;
                break;
            /* TFORCALL, TFORLOOP */
            case OP_TFORCALL:
                if (a + 3 > max_reg) max_reg = a + 3;
                break;
            case OP_TFORLOOP:
                if (a + 2 > max_reg) max_reg = a + 2;
                break;
            default:
                /* 大多数指令只需要 A */
                break;
        }
    }
    return max_reg;
}

static void process_proto(luaL_Buffer *B, Proto *p, int id, ProtoInfo *protos, int proto_count, int use_pure_c, int str_encrypt, int seed, int obfuscate, int inline_opt, int *boxed_slots, int *boxed_upvals) {
    char L_name[16] = "L";
    char vtab_name[16] = "vtab_idx";
    unsigned int obf_seed = (unsigned int)seed + id + 1000000;  /* 偏移避免与 proto 名冲突 */

    if (obfuscate) {
        get_random_name(L_name, sizeof(L_name), &obf_seed);
        get_random_name(vtab_name, sizeof(vtab_name), &obf_seed);
    }

    add_fmt(B, "\n/* Proto %d */\n", id);
    if (inline_opt) {
        add_fmt(B, "static inline int %s(lua_State *%s) {\n", protos[id].name, L_name);
    } else {
        add_fmt(B, "static int %s(lua_State *%s) {\n", protos[id].name, L_name);
    }

    if (obfuscate) {
        add_fmt(B, "#define L %s\n", L_name);
        add_fmt(B, "#define vtab_idx %s\n", vtab_name);
    }

    /* 使用 p->maxstacksize（luaO_flatten 已更新） */
    int effective_maxstack = p->maxstacksize;

    /* 分配 boxed_slots 用于跟踪装箱的局部变量 */
    if (boxed_slots == NULL) {
        boxed_slots = (int *)calloc(effective_maxstack + 1, sizeof(int));
        protos[id].boxed_slots = boxed_slots;  /* 记录以便后续释放 */
    }

    if (p->is_vararg) {
        add_fmt(B, "    int %s = %s;\n", vtab_name, obf_int(effective_maxstack + 1, &obf_seed, obfuscate));
        add_fmt(B, "    lua_tcc_prologue(%s, %s, %s);\n", L_name, obf_int(p->numparams, &obf_seed, obfuscate), obf_int(effective_maxstack, &obf_seed, obfuscate));
    } else {
        add_fmt(B, "    lua_settop(%s, %s); /* Max Stack Size */\n", L_name, obf_int(effective_maxstack, &obf_seed, obfuscate));
    }

    // Iterate instructions
    for (int i = 0; i < p->sizecode; i++) {
        if (obfuscate && (my_rand(&obf_seed) % 4 == 0)) emit_junk_code(B, &obf_seed);
        emit_instruction(B, p, i, p->code[i], protos, proto_count, use_pure_c, str_encrypt, seed, obfuscate, boxed_slots, boxed_upvals);
    }

    if (obfuscate) {
        add_fmt(B, "#undef L\n");
        add_fmt(B, "#undef vtab_idx\n");
    }

    // Fallback return if no return op
    if (p->sizecode == 0 || (GET_OPCODE(p->code[p->sizecode-1]) != OP_RETURN && GET_OPCODE(p->code[p->sizecode-1]) != OP_RETURN0 && GET_OPCODE(p->code[p->sizecode-1]) != OP_RETURN1)) {
        add_fmt(B, "    return %s;\n", obf_int(0, &obf_seed, obfuscate));
    }
    add_fmt(B, "}\n");
}


static int tcc_compute_flags(lua_State *L) {
    if (lua_type(L, 1) != LUA_TTABLE) {
        lua_pushinteger(L, 0);
        return 1;
    }
    int flags = 0;

    struct { const char *name; int flag; } options[] = {
        {"flatten", OBFUSCATE_CFF},
        {"block_shuffle", OBFUSCATE_BLOCK_SHUFFLE},
        {"bogus_blocks", OBFUSCATE_BOGUS_BLOCKS},
        {"state_encode", OBFUSCATE_STATE_ENCODE},
        {"nested_dispatcher", OBFUSCATE_NESTED_DISPATCHER},
        {"opaque_predicates", OBFUSCATE_OPAQUE_PREDICATES},
        {"func_interleave", OBFUSCATE_FUNC_INTERLEAVE},
        {"vm_protect", OBFUSCATE_VM_PROTECT},
        {"binary_dispatcher", OBFUSCATE_BINARY_DISPATCHER},
        {"random_nop", OBFUSCATE_RANDOM_NOP},
        {"string_encryption", OBFUSCATE_STR_ENCRYPT},
        {NULL, 0}
    };

    for (int i = 0; options[i].name; i++) {
        lua_getfield(L, 1, options[i].name);
        if (lua_toboolean(L, -1)) {
            flags |= options[i].flag;
        }
        lua_pop(L, 1);
    }

    lua_pushinteger(L, flags);
    return 1;
}

static int tcc_compile(lua_State *L) {
    size_t len;
    const char *code = luaL_checklstring(L, 1, &len);
    const char *modname = "module";
    int use_pure_c = 0;
    int obfuscate = 0;
    int flatten = 0;
    int str_encrypt = 0;
    int seed = 0;
    int provided_flags = 0;
    int inline_opt = 0;

    if (lua_gettop(L) >= 2) {
        if (lua_type(L, 2) == LUA_TTABLE) {
             /* Parse table options */
             lua_getfield(L, 2, "use_pure_c");
             if (!lua_isnil(L, -1)) use_pure_c = lua_toboolean(L, -1);
             lua_pop(L, 1);

             lua_getfield(L, 2, "obfuscate");
             if (!lua_isnil(L, -1)) obfuscate = lua_toboolean(L, -1);
             lua_pop(L, 1);

             lua_getfield(L, 2, "flatten");
             if (!lua_isnil(L, -1)) flatten = lua_toboolean(L, -1);
             lua_pop(L, 1);

             lua_getfield(L, 2, "string_encryption");
             if (!lua_isnil(L, -1)) str_encrypt = lua_toboolean(L, -1);
             lua_pop(L, 1);

             lua_getfield(L, 2, "flags");
             if (!lua_isnil(L, -1)) provided_flags = (int)lua_tointeger(L, -1);
             lua_pop(L, 1);

             lua_getfield(L, 2, "inline");
             if (!lua_isnil(L, -1)) inline_opt = lua_toboolean(L, -1);
             lua_pop(L, 1);

             /* Parse boolean flags from table and merge into provided_flags */
             struct { const char *name; int flag; } bool_opts[] = {
                 {"block_shuffle", OBFUSCATE_BLOCK_SHUFFLE},
                 {"bogus_blocks", OBFUSCATE_BOGUS_BLOCKS},
                 {"state_encode", OBFUSCATE_STATE_ENCODE},
                 {"nested_dispatcher", OBFUSCATE_NESTED_DISPATCHER},
                 {"opaque_predicates", OBFUSCATE_OPAQUE_PREDICATES},
                 {"func_interleave", OBFUSCATE_FUNC_INTERLEAVE},
                 {"vm_protect", OBFUSCATE_VM_PROTECT},
                 {"binary_dispatcher", OBFUSCATE_BINARY_DISPATCHER},
                 {"random_nop", OBFUSCATE_RANDOM_NOP},
                 {NULL, 0}
             };
             for (int i = 0; bool_opts[i].name; i++) {
                 lua_getfield(L, 2, bool_opts[i].name);
                 if (lua_toboolean(L, -1)) {
                     provided_flags |= bool_opts[i].flag;
                 }
                 lua_pop(L, 1);
             }

             lua_getfield(L, 2, "seed");
             if (!lua_isnil(L, -1)) seed = (int)lua_tointeger(L, -1);
             else seed = (int)time(NULL);
             lua_pop(L, 1);

             if (lua_gettop(L) >= 3) {
                 modname = luaL_checkstring(L, 3);
             }
        } else if (lua_type(L, 2) == LUA_TBOOLEAN) {
            use_pure_c = lua_toboolean(L, 2);
        } else {
            modname = luaL_checkstring(L, 2);
            if (lua_gettop(L) >= 3) {
                 if (lua_type(L, 3) == LUA_TTABLE) {
                     lua_getfield(L, 3, "use_pure_c");
                     if (!lua_isnil(L, -1)) use_pure_c = lua_toboolean(L, -1);
                     lua_pop(L, 1);

                     lua_getfield(L, 3, "obfuscate");
                     if (!lua_isnil(L, -1)) obfuscate = lua_toboolean(L, -1);
                     lua_pop(L, 1);

                     lua_getfield(L, 3, "flatten");
                     if (!lua_isnil(L, -1)) flatten = lua_toboolean(L, -1);
                     lua_pop(L, 1);

                     lua_getfield(L, 3, "string_encryption");
                     if (!lua_isnil(L, -1)) str_encrypt = lua_toboolean(L, -1);
                     lua_pop(L, 1);

                     lua_getfield(L, 3, "flags");
                     if (!lua_isnil(L, -1)) provided_flags = (int)lua_tointeger(L, -1);
                     lua_pop(L, 1);

                     lua_getfield(L, 3, "inline");
                     if (!lua_isnil(L, -1)) inline_opt = lua_toboolean(L, -1);
                     lua_pop(L, 1);

                     /* Parse boolean flags from table (arg 3) and merge into provided_flags */
                     struct { const char *name; int flag; } bool_opts[] = {
                         {"block_shuffle", OBFUSCATE_BLOCK_SHUFFLE},
                         {"bogus_blocks", OBFUSCATE_BOGUS_BLOCKS},
                         {"state_encode", OBFUSCATE_STATE_ENCODE},
                         {"nested_dispatcher", OBFUSCATE_NESTED_DISPATCHER},
                         {"opaque_predicates", OBFUSCATE_OPAQUE_PREDICATES},
                         {"func_interleave", OBFUSCATE_FUNC_INTERLEAVE},
                         {"vm_protect", OBFUSCATE_VM_PROTECT},
                         {"binary_dispatcher", OBFUSCATE_BINARY_DISPATCHER},
                         {"random_nop", OBFUSCATE_RANDOM_NOP},
                         {NULL, 0}
                     };
                     for (int i = 0; bool_opts[i].name; i++) {
                         lua_getfield(L, 3, bool_opts[i].name);
                         if (lua_toboolean(L, -1)) {
                             provided_flags |= bool_opts[i].flag;
                         }
                         lua_pop(L, 1);
                     }

                     lua_getfield(L, 3, "seed");
                     if (!lua_isnil(L, -1)) seed = (int)lua_tointeger(L, -1);
                     else seed = (int)time(NULL);
                     lua_pop(L, 1);
                 } else {
                     use_pure_c = lua_toboolean(L, 3);
                 }
            }
        }
    }

    // Compile Lua code to Bytecode
    if (luaL_loadbuffer(L, code, len, modname) != LUA_OK) {
        return lua_error(L);
    }

    // Get Proto
    const LClosure *cl = (const LClosure *)lua_topointer(L, -1);
    if (!cl || !isLfunction(s2v(L->top.p-1))) {
        lua_pushnil(L);
        lua_pushstring(L, "Failed to load closure");
        return 2;
    }
    Proto *p = cl->p;

    unsigned int obf_seed = (unsigned int)seed;

    // Collect all protos
    int capacity = 16;
    int count = 0;
    ProtoInfo *protos = (ProtoInfo *)malloc(capacity * sizeof(ProtoInfo));
    collect_protos(p, &count, &protos, &capacity, &obf_seed, obfuscate);

    /* 预计算每个 proto 的 boxed_upvals：哪些 upvalue 被修改了需要装箱 */
    for (int i = 0; i < count; i++) {
        Proto *pp = protos[i].p;
        protos[i].maxstack = pp->maxstacksize;
        if (pp->sizeupvalues > 0) {
            protos[i].boxed_upvals = (int *)calloc(pp->sizeupvalues, sizeof(int));
            for (int k = 0; k < pp->sizeupvalues; k++) {
                if (is_upval_modified(pp, k)) {
                    protos[i].boxed_upvals[k] = 1;
                }
            }
        } else {
            protos[i].boxed_upvals = NULL;
        }
        /* boxed_slots 在 process_proto 中动态分配 */
        protos[i].boxed_slots = NULL;
    }

    // Apply obfuscation if requested
    int obfuscate_flags = provided_flags;
    if (flatten) obfuscate_flags |= OBFUSCATE_CFF;
    // Note: We do NOT enable OBFUSCATE_STR_ENCRYPT here for luaO_flatten if str_encrypt is set,
    // because we handle string encryption explicitly during C code generation in lbctc.c.
    // if (str_encrypt) obfuscate_flags |= OBFUSCATE_STR_ENCRYPT;

    if (obfuscate_flags != 0) {
        for (int i = 0; i < count; i++) {
             /* Use different seed for each proto to vary obfuscation */
             if (luaO_flatten(L, protos[i].p, obfuscate_flags, seed + protos[i].id, NULL) != 0) {
                 for (int j = 0; j < count; j++) {
                     free(protos[j].boxed_upvals);
                     free(protos[j].boxed_slots);
                 }
                 free(protos);
                 return luaL_error(L, "Failed to obfuscate proto %d", protos[i].id);
             }
        }
    }

    // Start generating C code
    luaL_Buffer B;
    luaL_buffinit(L, &B);

    add_fmt(&B, "#include \"lua.h\"\n");
    add_fmt(&B, "#include \"lauxlib.h\"\n");
    add_fmt(&B, "#include \"lbctc.h\"\n");
    add_fmt(&B, "#include <string.h>\n");
    if (use_pure_c) {
        add_fmt(&B, "#include <math.h>\n");
    }
    add_fmt(&B, "\n");

    if (obfuscate) {
        add_fmt(&B, "/* Obfuscated Interface */\n");
        add_fmt(&B, "typedef struct TCC_Interface {\n");
        add_fmt(&B, "    void *f[%d];\n", tcc_api_count);
        add_fmt(&B, "} TCC_Interface;\n");
        add_fmt(&B, "static const TCC_Interface *api;\n\n");

        /* Generate shuffled indices matching lua_tcc_get_interface logic */
        int *indices = (int *)malloc(tcc_api_count * sizeof(int));
        for (int i = 0; i < tcc_api_count; i++) indices[i] = i;

        unsigned int useed = (unsigned int)seed;
        for (int i = tcc_api_count - 1; i > 0; i--) {
            int j = my_rand(&useed) % (i + 1);
            int temp = indices[i];
            indices[i] = indices[j];
            indices[j] = temp;
        }

        /* Emit macros mapping original names to shuffled locations */
        int counter = 0;
        char obf_name[16];
        #define X(name, ret, args) \
            get_random_name(obf_name, sizeof(obf_name), &useed); \
            add_fmt(&B, "#undef %s\n", #name); \
            add_fmt(&B, "#define %s(...) ((%s (*) %s)api->f[%d])(__VA_ARGS__)\n", obf_name, #ret, #args, indices[counter]); \
            add_fmt(&B, "#define %s %s\n", #name, obf_name); \
            counter++;

        #include "lbctc_api_list.h"
        #undef X

        free(indices);
        add_fmt(&B, "\n");

        add_fmt(&B, "extern void *lua_tcc_get_interface(lua_State *L, int seed);\n");
    }

    // Helpers (now provided by lapi.c/lbctc.c via LUA_API)

    // Forward declarations
    for (int i = 0; i < count; i++) {
        if (inline_opt) {
            add_fmt(&B, "static inline int %s(lua_State *L);\n", protos[i].name);
        } else {
            add_fmt(&B, "static int %s(lua_State *L);\n", protos[i].name);
        }
    }

    // Implementations
    for (int i = 0; i < count; i++) {
        process_proto(&B, protos[i].p, protos[i].id, protos, count, use_pure_c, str_encrypt, seed, obfuscate, inline_opt, protos[i].boxed_slots, protos[i].boxed_upvals);
    }

    // Main entry point
    add_fmt(&B, "\nint luaopen_%s(lua_State *L) {\n", modname);

    if (obfuscate) {
        add_fmt(&B, "    api = (const TCC_Interface *)lua_tcc_get_interface(L, %d);\n", seed);
        add_fmt(&B, "    luaL_ref(L, LUA_REGISTRYINDEX); /* Anchor interface to prevent GC */\n");
    }

    if (p->sizeupvalues > 0) {
         add_fmt(&B, "    lua_pushglobaltable(L);\n"); // Upvalue 1
         for (int k = 1; k < p->sizeupvalues; k++) {
             add_fmt(&B, "    lua_pushnil(L);\n");
         }
         add_fmt(&B, "    lua_pushcclosure(L, %s, %d);\n", protos[0].name, p->sizeupvalues);
    } else {
         add_fmt(&B, "    lua_pushcfunction(L, %s);\n", protos[0].name);
    }

    add_fmt(&B, "    lua_call(L, 0, 1);\n");
    add_fmt(&B, "    return 1;\n");
    add_fmt(&B, "}\n");

    luaL_pushresult(&B);
    for (int i = 0; i < count; i++) {
        free(protos[i].boxed_upvals);
        free(protos[i].boxed_slots);
    }
    free(protos);
    return 1;
}

static const luaL_Reg tcc_lib[] = {
    {"compile", tcc_compile},
    {"compute_flags", tcc_compute_flags},
    {NULL, NULL}
};

int luaopen_tcc(lua_State *L) {
    luaL_newlib(L, tcc_lib);
    return 1;
}
