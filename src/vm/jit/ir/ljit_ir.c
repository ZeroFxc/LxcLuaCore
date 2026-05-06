#include "ljit_ir.h"
#include <stdlib.h>

void *ljit_context_create(lua_State *L, Proto *proto) {
    ljit_ctx_t *ctx = (ljit_ctx_t *)malloc(sizeof(ljit_ctx_t));
    if (ctx) {
        ctx->L = L;
        ctx->proto = proto;
    }
    return (void *)ctx;
}

void ljit_context_destroy(void *ctx) {
    if (ctx) {
        free(ctx);
    }
}

void *ljit_ir_create(void) {
    return NULL;
}
