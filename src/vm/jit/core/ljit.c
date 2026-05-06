#include "lprefix.h"
#include "ljit.h"
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

int XCLUA_JIT_ENABLED = 1;

void luaJIT_init (lua_State *L) {
    if (!G(L)->jit_ctx) {
        G(L)->jit_ctx = sljit_create_compiler(NULL);
    }
}

void luaJIT_free (lua_State *L) {
    if (G(L)->jit_ctx) {
        sljit_free_compiler((struct sljit_compiler*)G(L)->jit_ctx);
        G(L)->jit_ctx = NULL;
    }
}

int luaJIT_compile (lua_State *L, Proto *p) {
    if (!XCLUA_JIT_ENABLED) return 0;
    if (p->jit_trace) return 1;
    
    void *ctx = ljit_context_create(L, p);
    if (!ctx) return 0;

    ljit_analyze(ctx);
    ljit_translate(ctx);
    ljit_optimize(ctx);
    ljit_regalloc(ctx);

    void *code = ljit_codegen(ctx);
    
    ljit_context_destroy(ctx);

    if (code) {
        p->jit_trace = code;
        return 1;
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
}

void luaJIT_disable (void) {
    XCLUA_JIT_ENABLED = 0;
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

static const luaL_Reg ljit_funcs[] = {
    {"on", ljit_enable},
    {"off", ljit_disable},
    {"status", ljit_status},
    {NULL, NULL}
};

LUAMOD_API int luaopen_jit (lua_State *L) {
    luaL_newlib(L, ljit_funcs);
    return 1;
}
