#ifndef ljit_h
#define ljit_h

#include "lua.h"
#include "lstate.h"

extern int XCLUA_JIT_ENABLED;
LUAI_FUNC void luaJIT_init (lua_State *L);
LUAI_FUNC void luaJIT_free (lua_State *L);
LUAI_FUNC int luaJIT_compile (lua_State *L, Proto *p);
LUAI_FUNC void luaJIT_free_trace (lua_State *L, void *trace);

#endif
