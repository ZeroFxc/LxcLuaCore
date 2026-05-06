#include "ljit_codegen.h"
#include "../ir/ljit_ir.h"
#include "../sljit/ljit_sljit.h"

void ljit_cg_emit_add(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    if (!node || !ctx || !ctx->compiler) return;

    if (node->dest.is_spilled && node->src1.is_spilled && node->src2.is_spilled) {
        sljit_emit_op2((struct sljit_compiler *)ctx->compiler, SLJIT_ADD,
            SLJIT_MEM1(SLJIT_SP), node->dest.stack_ofs,
            SLJIT_MEM1(SLJIT_SP), node->src1.stack_ofs,
            SLJIT_MEM1(SLJIT_SP), node->src2.stack_ofs);
    }
}

void ljit_cg_emit_sub(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    if (!node || !ctx || !ctx->compiler) return;

    if (node->dest.is_spilled && node->src1.is_spilled && node->src2.is_spilled) {
        sljit_emit_op2((struct sljit_compiler *)ctx->compiler, SLJIT_SUB,
            SLJIT_MEM1(SLJIT_SP), node->dest.stack_ofs,
            SLJIT_MEM1(SLJIT_SP), node->src1.stack_ofs,
            SLJIT_MEM1(SLJIT_SP), node->src2.stack_ofs);
    }
}
