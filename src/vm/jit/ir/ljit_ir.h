#ifndef ljit_ir_h
#define ljit_ir_h

#include "../core/ljit_internal.h"
#include "../../../core/lobject.h"
#include "../../../core/lstate.h"

typedef struct ljit_bb {
    int start_pc;
    int end_pc;
    struct ljit_bb *next;
} ljit_bb_t;

typedef struct ljit_ctx {
    lua_State *L;
    Proto *proto;
    ljit_bb_t *cfg;
} ljit_ctx_t;

void *ljit_context_create(lua_State *L, Proto *proto);
ljit_bb_t *ljit_ir_bb_build(Proto *proto);
void ljit_context_destroy(void *ctx);
void *ljit_ir_create(void);
void ljit_ir_append(void);

#endif
