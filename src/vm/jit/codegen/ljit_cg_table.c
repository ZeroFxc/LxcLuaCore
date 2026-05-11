#include "ljit_codegen.h"
#include "../ir/ljit_ir.h"
#include "../sljit/ljit_sljit.h"

void ljit_cg_emit_gettable(void *node_ptr, void *ctx_ptr) {
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
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R2, 0, SLJIT_S0, 0, SLJIT_IMM, node->src1.v.reg * tvalue_size);

    /* R3 = rc address (base + src2.v.reg * size) */
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R3, 0, SLJIT_S0, 0, SLJIT_IMM, node->src2.v.reg * tvalue_size);

    /* Call ljit_icall_gettable */
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4V(W, W, W, W), SLJIT_IMM, (sljit_sw)ljit_icall_gettable);

    if (!node->dest.is_spilled) {
        sljit_emit_op1(compiler, SLJIT_MOV, node->dest.phys_reg, 0, SLJIT_MEM1(SLJIT_S0), node->dest.stack_ofs);
    }
}

void ljit_cg_emit_settable(void *node_ptr, void *ctx_ptr) {
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
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R2, 0, SLJIT_S0, 0, SLJIT_IMM, node->src1.v.reg * tvalue_size);

    /* R3 = rc address (base + src2.v.reg * size) */
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R3, 0, SLJIT_S0, 0, SLJIT_IMM, node->src2.v.reg * tvalue_size);

    /* Call ljit_icall_settable */
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4V(W, W, W, W), SLJIT_IMM, (sljit_sw)ljit_icall_settable);
}

void ljit_cg_emit_geti(void *node_ptr, void *ctx_ptr) {
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
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R2, 0, SLJIT_S0, 0, SLJIT_IMM, node->src1.v.reg * tvalue_size);

    /* R3 = c (integer) */
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R3, 0, SLJIT_IMM, node->src2.v.i);

    /* Call ljit_icall_geti(L, ra, rb, c) */
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4V(W, W, W, 32), SLJIT_IMM, (sljit_sw)ljit_icall_geti);

    if (!node->dest.is_spilled) {
        sljit_emit_op1(compiler, SLJIT_MOV, node->dest.phys_reg, 0, SLJIT_MEM1(SLJIT_S0), node->dest.stack_ofs);
    }
}

void ljit_cg_emit_seti(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    int tvalue_size = sizeof(TValue);

    /* R0 = ctx->L */
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);

    /* R1 = ra address (base + dest.v.reg * size) */
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R1, 0, SLJIT_S0, 0, SLJIT_IMM, node->dest.v.reg * tvalue_size);

    /* R2 = c (integer) */
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, node->src1.v.i);

    /* R3 = rc address (base + src2.v.reg * size) */
    if (node->src2.type == IR_VAL_CONST) {
        int rk_idx = node->src2.v.k;
        TValue *rk_val = &ctx->proto->k[rk_idx];
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R3, 0, SLJIT_IMM, (sljit_sw)rk_val);
    } else {
        sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R3, 0, SLJIT_S0, 0, SLJIT_IMM, node->src2.v.reg * tvalue_size);
    }

    /* Call ljit_icall_seti(L, ra, c, rc) */
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4V(W, W, 32, W), SLJIT_IMM, (sljit_sw)ljit_icall_seti);
}

void ljit_cg_emit_getfield(void *node_ptr, void *ctx_ptr) {
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
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R2, 0, SLJIT_S0, 0, SLJIT_IMM, node->src1.v.reg * tvalue_size);

    /* R3 = rc address (ctx->proto->k + src2.v.k) */
    int k_idx = node->src2.v.k;
    TValue *k_val = &ctx->proto->k[k_idx];
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R3, 0, SLJIT_IMM, (sljit_sw)k_val);

    /* Call ljit_icall_getfield */
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4V(W, W, W, W), SLJIT_IMM, (sljit_sw)ljit_icall_getfield);

    if (!node->dest.is_spilled) {
        sljit_emit_op1(compiler, SLJIT_MOV, node->dest.phys_reg, 0, SLJIT_MEM1(SLJIT_S0), node->dest.stack_ofs);
    }
}

void ljit_cg_emit_setfield(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    int tvalue_size = sizeof(TValue);

    /* R0 = ctx->L */
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);

    /* R1 = ra address (base + dest.v.reg * size) */
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R1, 0, SLJIT_S0, 0, SLJIT_IMM, node->dest.v.reg * tvalue_size);

    /* R2 = rb address (ctx->proto->k + src1.v.k) */
    int k_idx = node->src1.v.k;
    TValue *k_val = &ctx->proto->k[k_idx];
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, (sljit_sw)k_val);

    /* R3 = rc address (base + src2.v.reg * size) */
    if (node->src2.type == IR_VAL_CONST) {
        int rk_idx = node->src2.v.k;
        TValue *rk_val = &ctx->proto->k[rk_idx];
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R3, 0, SLJIT_IMM, (sljit_sw)rk_val);
    } else {
        sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R3, 0, SLJIT_S0, 0, SLJIT_IMM, node->src2.v.reg * tvalue_size);
    }

    /* Call ljit_icall_setfield */
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4V(W, W, W, W), SLJIT_IMM, (sljit_sw)ljit_icall_setfield);
}

void ljit_cg_emit_gettabup(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    int tvalue_size = sizeof(TValue);

    /* R0 = ctx->L */
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);

    /* R1 = ra address (base + dest.v.reg * size) */
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R1, 0, SLJIT_S0, 0, SLJIT_IMM, node->dest.v.reg * tvalue_size);

    /* R2 = upval_idx (integer) */
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, node->src1.v.i);

    /* R3 = rc address (ctx->proto->k + src2.v.k) */
    int k_idx = node->src2.v.k;
    TValue *k_val = &ctx->proto->k[k_idx];
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R3, 0, SLJIT_IMM, (sljit_sw)k_val);

    /* Call ljit_icall_gettabup */
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4V(W, W, W, W), SLJIT_IMM, (sljit_sw)ljit_icall_gettabup);

    if (!node->dest.is_spilled) {
        sljit_emit_op1(compiler, SLJIT_MOV, node->dest.phys_reg, 0, SLJIT_MEM1(SLJIT_S0), node->dest.stack_ofs);
    }
}

void ljit_cg_emit_settabup(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    int tvalue_size = sizeof(TValue);

    /* R0 = ctx->L */
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);

    /* R1 = upval_idx (integer) */
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM, node->dest.v.i);

    /* R2 = rb address (ctx->proto->k + src1.v.k) */
    int k_idx = node->src1.v.k;
    TValue *k_val = &ctx->proto->k[k_idx];
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, (sljit_sw)k_val);

    /* R3 = rc address */
    if (node->src2.type == IR_VAL_CONST) {
        int rk_idx = node->src2.v.k;
        TValue *rk_val = &ctx->proto->k[rk_idx];
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R3, 0, SLJIT_IMM, (sljit_sw)rk_val);
    } else {
        sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R3, 0, SLJIT_S0, 0, SLJIT_IMM, node->src2.v.reg * tvalue_size);
    }

    /* Call ljit_icall_settabup */
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4V(W, W, W, W), SLJIT_IMM, (sljit_sw)ljit_icall_settabup);
}

void ljit_cg_emit_newtable(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    int tvalue_size = sizeof(TValue);

    /* R0 = ctx->L */
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);

    /* R1 = b (hash size, from src1.v.i usually or could be zero in MVP) */
    int b = (node->src1.type == IR_VAL_INT) ? (int)node->src1.v.i : 0;
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM, b);

    /* R2 = c (array size, from src2.v.i usually or could be zero in MVP) */
    int c = (node->src2.type == IR_VAL_INT) ? (int)node->src2.v.i : 0;
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, c);

    /* R3 = ra address (base + dest.v.reg * size) */
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R3, 0, SLJIT_S0, 0, SLJIT_IMM, node->dest.v.reg * tvalue_size);

    /* Call ljit_icall_newtable */
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4V(W, 32, 32, W), SLJIT_IMM, (sljit_sw)ljit_icall_newtable);
}

void ljit_cg_emit_setlist(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    if (node->src1.v.i == -1) return;

    int tvalue_size = sizeof(TValue);

    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);

    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R1, 0, SLJIT_S0, 0, SLJIT_IMM, node->dest.v.reg * tvalue_size);

    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, node->src1.v.i);

    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R3, 0, SLJIT_IMM, node->src2.v.i);

    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4V(W, W, 32, 32), SLJIT_IMM, (sljit_sw)ljit_icall_setlist);
}
