#include "ljit_codegen.h"
#include "../ir/ljit_ir.h"
#include "../sljit/ljit_sljit.h"

void ljit_cg_emit_jmp(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    if (!node || !ctx || !ctx->compiler) return;

    // MVP jump: just a jump to a label ID (assuming labels are handled via sljit_emit_jump)
    // For now we leave it as stub to avoid complex jump resolution in MVP
}

void ljit_cg_emit_ret(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    if (!node || !ctx || !ctx->compiler) return;

    // For RET, we simply emit a return void.
    sljit_emit_return_void((struct sljit_compiler *)ctx->compiler);
}
