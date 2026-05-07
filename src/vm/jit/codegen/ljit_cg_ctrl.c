#include "ljit_codegen.h"
#include "../ir/ljit_ir.h"
#include "../sljit/ljit_sljit.h"

void ljit_cg_emit_jmp(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    struct sljit_jump *jmp = sljit_emit_jump(compiler, SLJIT_JUMP);
    if (jmp) {
        int idx = ctx->num_jumps++;
        ctx->jumps[idx] = jmp;
        ctx->jump_targets[idx] = node->dest.v.label_id;
    }
}

void ljit_cg_emit_ret(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    if (!node || !ctx || !ctx->compiler) return;

    // For RET, we simply emit a return void.
    sljit_emit_return_void((struct sljit_compiler *)ctx->compiler);
}
