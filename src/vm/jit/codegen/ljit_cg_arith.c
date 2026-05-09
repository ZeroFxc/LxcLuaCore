#include "ljit_codegen.h"
#include "../ir/ljit_ir.h"
#include "../sljit/ljit_sljit.h"
#include "../../../core/lobject.h"

void ljit_cg_emit_load_operand(struct ljit_ctx *ctx, int target_reg, void *val_ptr) {
        struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    ljit_ir_val_t *val = (ljit_ir_val_t *)val_ptr;
    if (val->type == IR_VAL_REG) {
        if (val->is_spilled) {
            sljit_emit_op1(compiler, SLJIT_MOV, target_reg, 0, SLJIT_MEM1(SLJIT_S0), val->stack_ofs);
        } else {
            if (target_reg != val->phys_reg) {
                sljit_emit_op1(compiler, SLJIT_MOV, target_reg, 0, val->phys_reg, 0);
            }
        }
    } else if (val->type == IR_VAL_INT) {
        sljit_emit_op1(compiler, SLJIT_MOV, target_reg, 0, SLJIT_IMM, val->v.i);
    } else if (val->type == IR_VAL_NUM) {
        /* Currently treating numbers as ints for MVP fast path */
        sljit_emit_op1(compiler, SLJIT_MOV, target_reg, 0, SLJIT_IMM, (sljit_sw)val->v.n);
    } else if (val->type == IR_VAL_CONST) {
        sljit_sw k_ptr = (sljit_sw)&ctx->proto->k[val->v.k];
        /* Use SLJIT_R2 as temporary pointer holding register */
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, k_ptr);
        /* Load value_ (offset 0) into target_reg */
        sljit_emit_op1(compiler, SLJIT_MOV, target_reg, 0, SLJIT_MEM1(SLJIT_R2), 0);
    }
}

void ljit_cg_emit_store_operand(struct ljit_ctx *ctx, void *val_ptr, int src_reg) {
        struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    ljit_ir_val_t *val = (ljit_ir_val_t *)val_ptr;
    if (val->type == IR_VAL_REG) {
        if (val->is_spilled) {
            sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_MEM1(SLJIT_S0), val->stack_ofs, src_reg, 0);
            /* Tag setting for number type */
            sljit_emit_op1(compiler, SLJIT_MOV_U8, SLJIT_MEM1(SLJIT_S0), val->stack_ofs + 8, SLJIT_IMM, LUA_VNUMINT);
        } else {
            if (val->phys_reg != src_reg) {
                sljit_emit_op1(compiler, SLJIT_MOV, val->phys_reg, 0, src_reg, 0);
            }
            /* Since physical registers don't store tags, tag handling is bypassed here for MVP */
        }
    }
}

void ljit_cg_emit_add(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    ljit_cg_emit_load_operand(ctx, SLJIT_R0, &node->src1);
    ljit_cg_emit_load_operand(ctx, SLJIT_R1, &node->src2);
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0);
    ljit_cg_emit_store_operand(ctx, &node->dest, SLJIT_R0);
}

void ljit_cg_emit_mul(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    ljit_cg_emit_load_operand(ctx, SLJIT_R0, &node->src1);
    ljit_cg_emit_load_operand(ctx, SLJIT_R1, &node->src2);
    sljit_emit_op2(compiler, SLJIT_MUL, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0);
    ljit_cg_emit_store_operand(ctx, &node->dest, SLJIT_R0);
}

void ljit_cg_emit_div(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    ljit_cg_emit_load_operand(ctx, SLJIT_R0, &node->src1);
    ljit_cg_emit_load_operand(ctx, SLJIT_R1, &node->src2);
    sljit_emit_op0(compiler, SLJIT_DIV_SW);
    ljit_cg_emit_store_operand(ctx, &node->dest, SLJIT_R0);
}

void ljit_cg_emit_idiv(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    ljit_cg_emit_load_operand(ctx, SLJIT_R0, &node->src1);
    ljit_cg_emit_load_operand(ctx, SLJIT_R1, &node->src2);
    sljit_emit_op0(compiler, SLJIT_DIV_SW);
    ljit_cg_emit_store_operand(ctx, &node->dest, SLJIT_R0);
}

void ljit_cg_emit_mod(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    ljit_cg_emit_load_operand(ctx, SLJIT_R0, &node->src1);
    ljit_cg_emit_load_operand(ctx, SLJIT_R1, &node->src2);
    sljit_emit_op0(compiler, SLJIT_DIV_SW);
    ljit_cg_emit_store_operand(ctx, &node->dest, SLJIT_R1);
}

void ljit_cg_emit_sub(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    ljit_cg_emit_load_operand(ctx, SLJIT_R0, &node->src1);
    ljit_cg_emit_load_operand(ctx, SLJIT_R1, &node->src2);
    sljit_emit_op2(compiler, SLJIT_SUB, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0);
    ljit_cg_emit_store_operand(ctx, &node->dest, SLJIT_R0);
}

void ljit_cg_emit_unm(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, 0);
    ljit_cg_emit_load_operand(ctx, SLJIT_R1, &node->src1);
    sljit_emit_op2(compiler, SLJIT_SUB, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0);
    ljit_cg_emit_store_operand(ctx, &node->dest, SLJIT_R0);
}

void ljit_cg_emit_not(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    ljit_cg_emit_load_operand(ctx, SLJIT_R0, &node->src1);
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM, 1);
    sljit_emit_op2(compiler, SLJIT_XOR, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0);
    ljit_cg_emit_store_operand(ctx, &node->dest, SLJIT_R0);
}

void ljit_cg_emit_pow(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    int tvalue_size = sizeof(TValue);

    /* R0 = ctx->L */
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);

    /* R1 = ra address (base + dest.v.reg * size) */
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R1, 0, SLJIT_S0, 0, SLJIT_IMM, node->dest.v.reg * tvalue_size);

    /* R2 = rb address (base + src1.v.reg * size) */
    if (node->src1.type == IR_VAL_CONST) {
        sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R2, 0, SLJIT_IMM, (sljit_sw)ctx->proto->k, SLJIT_IMM, node->src1.v.k * tvalue_size);
    } else {
        sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R2, 0, SLJIT_S0, 0, SLJIT_IMM, node->src1.v.reg * tvalue_size);
    }

    /* R3 = rc address (base + src2.v.reg * size) */
    if (node->src2.type == IR_VAL_CONST) {
        sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R3, 0, SLJIT_IMM, (sljit_sw)ctx->proto->k, SLJIT_IMM, node->src2.v.k * tvalue_size);
    } else {
        sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R3, 0, SLJIT_S0, 0, SLJIT_IMM, node->src2.v.reg * tvalue_size);
    }

    /* Call ljit_icall_pow */
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4V(W, W, W, W), SLJIT_IMM, (sljit_sw)ljit_icall_pow);
}

void ljit_cg_emit_nop(void *node_ptr, void *ctx_ptr) {
    /* Do nothing */
    (void)node_ptr;
    (void)ctx_ptr;
}

void ljit_cg_emit_band(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    ljit_cg_emit_load_operand(ctx, SLJIT_R0, &node->src1);
    ljit_cg_emit_load_operand(ctx, SLJIT_R1, &node->src2);
    sljit_emit_op2(compiler, SLJIT_AND, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0);
    ljit_cg_emit_store_operand(ctx, &node->dest, SLJIT_R0);
}

void ljit_cg_emit_bor(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    ljit_cg_emit_load_operand(ctx, SLJIT_R0, &node->src1);
    ljit_cg_emit_load_operand(ctx, SLJIT_R1, &node->src2);
    sljit_emit_op2(compiler, SLJIT_OR, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0);
    ljit_cg_emit_store_operand(ctx, &node->dest, SLJIT_R0);
}

void ljit_cg_emit_bxor(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    ljit_cg_emit_load_operand(ctx, SLJIT_R0, &node->src1);
    ljit_cg_emit_load_operand(ctx, SLJIT_R1, &node->src2);
    sljit_emit_op2(compiler, SLJIT_XOR, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0);
    ljit_cg_emit_store_operand(ctx, &node->dest, SLJIT_R0);
}

void ljit_cg_emit_shl(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    ljit_cg_emit_load_operand(ctx, SLJIT_R0, &node->src1);
    ljit_cg_emit_load_operand(ctx, SLJIT_R1, &node->src2);
    sljit_emit_op2(compiler, SLJIT_SHL, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0);
    ljit_cg_emit_store_operand(ctx, &node->dest, SLJIT_R0);
}

void ljit_cg_emit_shr(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    ljit_cg_emit_load_operand(ctx, SLJIT_R0, &node->src1);
    ljit_cg_emit_load_operand(ctx, SLJIT_R1, &node->src2);
    sljit_emit_op2(compiler, SLJIT_LSHR, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0);
    ljit_cg_emit_store_operand(ctx, &node->dest, SLJIT_R0);
}

void ljit_cg_emit_bnot(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    ljit_cg_emit_load_operand(ctx, SLJIT_R0, &node->src1);
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM, -1);
    sljit_emit_op2(compiler, SLJIT_XOR, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0);
    ljit_cg_emit_store_operand(ctx, &node->dest, SLJIT_R0);
}
