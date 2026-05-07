#include "ljit_codegen.h"
#include "../ir/ljit_ir.h"
#include "../sljit/ljit_sljit.h"

void ljit_cg_emit_add(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    if (node->dest.is_spilled && node->src1.is_spilled && node->src2.is_spilled) {
        /*
         * Base pointer is in SLJIT_S0.
         * TValue layout:
         * offset 0: union Value value_ (which contains lua_Integer i)
         * offset 8: lu_byte tt_
         * We load the integer value directly for MVP.
         * src1 value into R0
         */
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_S0), node->src1.stack_ofs);
        /* src2 value into R1 */
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_MEM1(SLJIT_S0), node->src2.stack_ofs);

        /* R0 = R0 + R1 */
        sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0);

        /* Store result back to dest */
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_MEM1(SLJIT_S0), node->dest.stack_ofs, SLJIT_R0, 0);

        /* Set type tag to LUA_VNUMINT (3) at offset 8 */
        sljit_emit_op1(compiler, SLJIT_MOV32, SLJIT_MEM1(SLJIT_S0), node->dest.stack_ofs + 8, SLJIT_IMM, 3);
    }
}

void ljit_cg_emit_mul(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    if (node->dest.is_spilled && node->src1.is_spilled && node->src2.is_spilled) {
        /* Load src1 to R0 */
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_S0), node->src1.stack_ofs);
        /* Load src2 to R1 */
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_MEM1(SLJIT_S0), node->src2.stack_ofs);

        /* R0 = R0 * R1 */
        sljit_emit_op2(compiler, SLJIT_MUL, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0);

        /* Store result back to dest */
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_MEM1(SLJIT_S0), node->dest.stack_ofs, SLJIT_R0, 0);

        /* Set type tag to LUA_VNUMINT (3) at offset 8 */
        sljit_emit_op1(compiler, SLJIT_MOV32, SLJIT_MEM1(SLJIT_S0), node->dest.stack_ofs + 8, SLJIT_IMM, 3);
    }
}

void ljit_cg_emit_div(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    if (node->dest.is_spilled && node->src1.is_spilled && node->src2.is_spilled) {
        /* Load src1 to R0 */
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_S0), node->src1.stack_ofs);
        /* Load src2 to R1 */
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_MEM1(SLJIT_S0), node->src2.stack_ofs);

        /* sljit division requires specific registers: dividend in R0, divisor in R1. Result in R0 (quotient), R1 (remainder) */
        sljit_emit_op0(compiler, SLJIT_DIV_SW);

        /* Store result back to dest */
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_MEM1(SLJIT_S0), node->dest.stack_ofs, SLJIT_R0, 0);

        /* Set type tag to LUA_VNUMINT (3) at offset 8 */
        sljit_emit_op1(compiler, SLJIT_MOV32, SLJIT_MEM1(SLJIT_S0), node->dest.stack_ofs + 8, SLJIT_IMM, 3);
    }
}

void ljit_cg_emit_mod(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    if (node->dest.is_spilled && node->src1.is_spilled && node->src2.is_spilled) {
        /* Load src1 to R0 */
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_S0), node->src1.stack_ofs);
        /* Load src2 to R1 */
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_MEM1(SLJIT_S0), node->src2.stack_ofs);

        /* sljit division requires specific registers: dividend in R0, divisor in R1. Result in R0 (quotient), R1 (remainder) */
        sljit_emit_op0(compiler, SLJIT_DIV_SW);

        /* Store remainder (R1) back to dest */
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_MEM1(SLJIT_S0), node->dest.stack_ofs, SLJIT_R1, 0);

        /* Set type tag to LUA_VNUMINT (3) at offset 8 */
        sljit_emit_op1(compiler, SLJIT_MOV32, SLJIT_MEM1(SLJIT_S0), node->dest.stack_ofs + 8, SLJIT_IMM, 3);
    }
}

void ljit_cg_emit_sub(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    if (node->dest.is_spilled && node->src1.is_spilled && node->src2.is_spilled) {
        /* Load src1 to R0 */
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_S0), node->src1.stack_ofs);
        /* Load src2 to R1 */
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_MEM1(SLJIT_S0), node->src2.stack_ofs);

        /* R0 = R0 - R1 */
        sljit_emit_op2(compiler, SLJIT_SUB, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0);

        /* Store result back to dest */
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_MEM1(SLJIT_S0), node->dest.stack_ofs, SLJIT_R0, 0);

        /* Set type tag to LUA_VNUMINT (3) at offset 8 */
        sljit_emit_op1(compiler, SLJIT_MOV32, SLJIT_MEM1(SLJIT_S0), node->dest.stack_ofs + 8, SLJIT_IMM, 3);
    }
}
