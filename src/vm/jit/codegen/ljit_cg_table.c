#include "ljit_codegen.h"
#include "../ir/ljit_ir.h"
#include "../sljit/ljit_sljit.h"

void ljit_cg_emit_gettable(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    /* Fallback to interpreter for gettable */
    sljit_emit_return_void(compiler);
}

void ljit_cg_emit_settable(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    /* Fallback to interpreter for settable */
    sljit_emit_return_void(compiler);
}
