#include "lprefix.h"
#include "ljit.h"
#include "ljit_debug.h"
#include "../ir/ljit_ir.h"
#include "../frontend/ljit_analyze.h"
#include "../optimize/ljit_opt.h"
#include "../regalloc/ljit_regalloc.h"
#include "../codegen/ljit_codegen.h"
#include "../../../jit/sljitLir.h"
#include "../../../core/lobject.h"
#include "../../../core/lstate.h"
#include "../../../core/lcode.h"
#include "../../../core/lopcodes.h"
#include "../../../core/lauxlib.h"
#include "../../../core/lualib.h"

int XCLUA_JIT_ENABLED = 0;
int XCLUA_REGEX_JIT_ENABLED = 0;  /* PCRE2 正则 JIT，独立于代码 JIT */
int XCLUA_PCRE2_ENABLED = 0;  /* 是否启用 PCRE2 正则引擎 */

/* JIT 热点阈值: 函数被调用多少次后触发JIT编译 (默认56, 与LuaJIT一致) */
int XCLUA_JIT_HOTCOUNT = 56;

/* JIT 统计计数器 */
static int jit_compile_ok = 0;   /* 成功编译次数 */
static int jit_compile_fail = 0; /* 编译失败次数 */
static int jit_fallback_count = 0; /* JIT 代码回退到解释器次数 */

void luaJIT_init (lua_State *L) {
    /* No persistent sljit compiler needed globally */
}

void luaJIT_free (lua_State *L) {
    /* No persistent sljit compiler needed globally */
}

int luaJIT_compile (lua_State *L, Proto *p) {
    if (p->jit_trace || p->jit_failed) {
        JIT_DBG(MOD_CORE, "skip: trace=%p, failed=%d", p->jit_trace, p->jit_failed);
        return 1;
    }
    
    JIT_DBG(MOD_CORE, "compiling, sizecode=%d, maxstacksize=%d", p->sizecode, p->maxstacksize);
    void *ctx = ljit_context_create(L, p);
    if (!ctx) { JIT_DBG(MOD_CORE, "context_create failed"); return 0; }

    JIT_DBG(MOD_CORE, "analyze...");
    ljit_analyze(ctx);
    JIT_DBG(MOD_CORE, "translate...");
    ljit_translate(ctx);
    JIT_DBG(MOD_CORE, "optimize...");
    ljit_optimize(ctx);
    JIT_DBG(MOD_CORE, "regalloc...");
    ljit_regalloc(ctx);
    JIT_DBG(MOD_CORE, "codegen...");

    void *code = ljit_codegen(ctx);
    
    JIT_DBG(MOD_CORE, "codegen done, code=%p", code);
    ljit_context_destroy(ctx);

    if (code) {
        p->jit_trace = code;
        jit_compile_ok++;
        JIT_DBG(MOD_CORE, "compile OK, code=%p, total_ok=%d", code, jit_compile_ok);
        return 1;
    } else {
        p->jit_failed = 1;
        jit_compile_fail++;
        JIT_DBG(MOD_CORE, "compile FAILED, total_fail=%d", jit_compile_fail);
    }
    return 0;
}

void luaJIT_free_trace (lua_State *L, void *trace) {
    (void)L;
    if (trace) {
        sljit_free_code(trace, NULL);
    }
}

void luaJIT_enable (void) {
    XCLUA_JIT_ENABLED = 1;
    JIT_DBG(MOD_CTL, "JIT enabled, XCLUA_JIT_ENABLED=%d", XCLUA_JIT_ENABLED);
}

void luaJIT_disable (void) {
    XCLUA_JIT_ENABLED = 0;
    JIT_DBG(MOD_CTL, "JIT disabled, XCLUA_JIT_ENABLED=%d", XCLUA_JIT_ENABLED);
}

void luaJIT_record_fallback (void) {
    jit_fallback_count++;
}

static int ljit_enable (lua_State *L) {
    luaJIT_enable();
    return 0;
}

static int ljit_disable (lua_State *L) {
    luaJIT_disable();
    return 0;
}

static int ljit_status (lua_State *L) {
    lua_pushboolean(L, XCLUA_JIT_ENABLED);
    return 1;
}

static int ljit_stats (lua_State *L) {
    extern int ljit_self_call_count;
    lua_createtable(L, 0, 5);
    lua_pushinteger(L, jit_compile_ok);
    lua_setfield(L, -2, "compiled");
    lua_pushinteger(L, jit_compile_fail);
    lua_setfield(L, -2, "failed");
    lua_pushinteger(L, jit_fallback_count);
    lua_setfield(L, -2, "fallback");
    lua_pushinteger(L, ljit_self_call_count);
    lua_setfield(L, -2, "self_calls");
    lua_pushinteger(L, XCLUA_JIT_HOTCOUNT);
    lua_setfield(L, -2, "hotcount");
    return 1;
}

/* jit.threshold([n]): 获取或设置JIT编译触发阈值 */
static int ljit_threshold (lua_State *L) {
    int nargs = lua_gettop(L);
    if (nargs >= 1) {
        int new_threshold = (int)luaL_checkinteger(L, 1);
        if (new_threshold < 0) new_threshold = 0;
        XCLUA_JIT_HOTCOUNT = new_threshold;
        JIT_DBG(MOD_CTL, "JIT hotcount threshold set to %d", new_threshold);
        return 0;
    }
    lua_pushinteger(L, XCLUA_JIT_HOTCOUNT);
    return 1;
}

/* jit.hotcount([func]): 获取指定函数的热点计数, 不传参返回当前函数 */
static int ljit_hotcount (lua_State *L) {
    Proto *p = NULL;
    if (lua_gettop(L) >= 1) {
        luaL_checktype(L, 1, LUA_TFUNCTION);
        StkId func = L->ci->func.p;  /* 简化: 暂不支持参数传递的函数查询 */
        if (ttisLclosure(s2v(func))) {
            p = clLvalue(s2v(func))->p;
        }
    } else {
        CallInfo *ci = L->ci;
        if (ci && ttisLclosure(s2v(ci->func.p))) {
            p = clLvalue(s2v(ci->func.p))->p;
        }
    }
    if (p) {
        lua_pushinteger(L, p->jit_hotcount);
    } else {
        lua_pushinteger(L, 0);
    }
    return 1;
}

static const luaL_Reg ljit_funcs[] = {
    {"on", ljit_enable},
    {"off", ljit_disable},
    {"status", ljit_status},
    {"stats", ljit_stats},
    {"threshold", ljit_threshold},
    {"hotcount", ljit_hotcount},
    {NULL, NULL}
};

/* -- jit.regex 子模块：PCRE2 正则 JIT 控制 -- */

static int ljit_regex_enable (lua_State *L) {
    XCLUA_REGEX_JIT_ENABLED = 1;
    return 0;
}

static int ljit_regex_disable (lua_State *L) {
    XCLUA_REGEX_JIT_ENABLED = 0;
    return 0;
}

static int ljit_regex_status (lua_State *L) {
    lua_pushboolean(L, XCLUA_REGEX_JIT_ENABLED);
    return 1;
}

static const luaL_Reg ljit_regex_funcs[] = {
    {"on", ljit_regex_enable},
    {"off", ljit_regex_disable},
    {"status", ljit_regex_status},
    {NULL, NULL}
};

/* -- jit.regex.pcre2 子模块：PCRE2 引擎开关 -- */

static int ljit_pcre2_enable (lua_State *L) {
    XCLUA_PCRE2_ENABLED = 1;
    return 0;
}

static int ljit_pcre2_disable (lua_State *L) {
    XCLUA_PCRE2_ENABLED = 0;
    return 0;
}

static int ljit_pcre2_status (lua_State *L) {
    lua_pushboolean(L, XCLUA_PCRE2_ENABLED);
    return 1;
}

static const luaL_Reg ljit_pcre2_funcs[] = {
    {"on", ljit_pcre2_enable},
    {"off", ljit_pcre2_disable},
    {"status", ljit_pcre2_status},
    {NULL, NULL}
};

LUAMOD_API int luaopen_jit (lua_State *L) {
    luaL_newlib(L, ljit_funcs);
    /* 注册 jit.regex 子表 */
    luaL_newlib(L, ljit_regex_funcs);
    /* 注册 jit.regex.pcre2 子表 */
    luaL_newlib(L, ljit_pcre2_funcs);
    lua_setfield(L, -2, "pcre2");
    lua_setfield(L, -2, "regex");
    return 1;
}
