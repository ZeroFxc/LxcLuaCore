#include "ljit_codegen.h"
#include "../ir/ljit_ir.h"
#include "../sljit/ljit_sljit.h"

void ljit_cg_emit_conv(void *node, void *ctx) {
    (void)node;
    (void)ctx;
}

void ljit_cg_emit_mov(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    if (node->dest.is_spilled && node->src1.is_spilled) {
        /* Load src1 (TValue) */
        /* Since TValue is 16 bytes (or more), for MVP we just copy value and tt_ */
        /* Copy value_ (offset 0) */
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_S0), node->src1.stack_ofs);
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_MEM1(SLJIT_S0), node->dest.stack_ofs, SLJIT_R0, 0);

        /* Copy tt_ (offset 8) */
        sljit_emit_op1(compiler, SLJIT_MOV32, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_S0), node->src1.stack_ofs + 8);
        sljit_emit_op1(compiler, SLJIT_MOV32, SLJIT_MEM1(SLJIT_S0), node->dest.stack_ofs + 8, SLJIT_R0, 0);
    }
}

void ljit_cg_emit_loadi(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    if (node->dest.is_spilled && node->src1.type == IR_VAL_INT) {
        /* Load immediate integer into dest value_ */
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_MEM1(SLJIT_S0), node->dest.stack_ofs, SLJIT_IMM, node->src1.v.i);

        /* Set type tag to LUA_VNUMINT (3) at offset 8 */
        sljit_emit_op1(compiler, SLJIT_MOV32, SLJIT_MEM1(SLJIT_S0), node->dest.stack_ofs + 8, SLJIT_IMM, 3);
    }
}
