#include "lprefix.h"
#include "ljit.h"
#include "../../jit/sljitLir.h"
#include "../../core/lobject.h"
#include "../../core/lstate.h"
#include "../../core/lcode.h"
#include "../../core/lopcodes.h"
#include "../../core/lauxlib.h"
#include "../../core/lualib.h"

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
    
    struct sljit_compiler *compiler = sljit_create_compiler(NULL);
    if (!compiler) return 0;
    
    // We emit enter for a standard function taking (lua_State*, CallInfo*) as context.
    sljit_emit_enter(compiler, 0, SLJIT_ARGS2V(W, W), 3, 3, 0);
    // Real implementation would translate opcodes here
    // For now it is just an empty stub
    sljit_emit_return_void(compiler);
    
    void *code = sljit_generate_code(compiler, 0, NULL);
    sljit_free_compiler(compiler);
    
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
