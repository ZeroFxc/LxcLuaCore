#include "ljit_codegen.h"
#include "../core/ljit_internal.h"
#include "../ir/ljit_ir.h"
#include "../sljit/ljit_sljit.h"
#include <stdlib.h>

void *ljit_codegen(void *ctx_ptr) {
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    if (!ctx) return NULL;

    struct sljit_compiler *compiler = sljit_create_compiler(NULL);
    if (!compiler) return NULL;

    ctx->compiler = compiler;
    ctx->labels = (struct sljit_label **)calloc(ctx->next_label_id + 1, sizeof(struct sljit_label *));

    /*
     * Enter function arguments mapping:
     * jit_func_t(StkId base) -> SLJIT_ARGS1V(W) -> base in SLJIT_S0.
     * SLJIT_S0 will hold the Lua virtual register base address.
     */
    sljit_emit_enter(compiler, 0, SLJIT_ARGS1V(W), 3, 3, 0);

    ljit_ir_node_t *node = ctx->ir_head;
    while (node) {
        switch (node->op) {
            case IR_ADD: ljit_cg_emit_add(node, ctx); break;
            case IR_SUB: ljit_cg_emit_sub(node, ctx); break;
            case IR_MUL: ljit_cg_emit_mul(node, ctx); break;
            case IR_DIV: ljit_cg_emit_div(node, ctx); break;
            case IR_MOD: ljit_cg_emit_mod(node, ctx); break;
            case IR_MOV: ljit_cg_emit_mov(node, ctx); break;
            case IR_LOADI: ljit_cg_emit_loadi(node, ctx); break;
            case IR_JMP: ljit_cg_emit_jmp(node, ctx); break;
            case IR_RET: ljit_cg_emit_ret(node, ctx); break;
            case IR_GETTABLE: ljit_cg_emit_gettable(node, ctx); break;
            case IR_SETTABLE: ljit_cg_emit_settable(node, ctx); break;
            case IR_CALL: ljit_cg_emit_call(node, ctx); break;
            // Additional instructions can be mapped here as they are implemented
            default: break;
        }
        node = node->next;
    }

    sljit_emit_return_void(compiler);

    void *code = sljit_generate_code(compiler, 0, NULL);

    sljit_free_compiler(compiler);
    ctx->compiler = NULL;

    return code;
}
