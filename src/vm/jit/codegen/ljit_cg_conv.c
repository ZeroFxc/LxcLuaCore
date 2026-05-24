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

    int tvalue_size = sizeof(TValue);

    /* 所有MOV操作: 源栈位置和目标栈位置 */
    int src_ofs = node->src1.stack_ofs;
    int dst_ofs = node->dest.stack_ofs;

    /* 始终将完整的16字节TValue从源复制到目标 (value_ + tt_) */
    if (dst_ofs != src_ofs) {
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_S0), src_ofs);
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_MEM1(SLJIT_S0), dst_ofs, SLJIT_R0, 0);
        sljit_emit_op1(compiler, SLJIT_MOV32, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_S0), src_ofs + 8);
        sljit_emit_op1(compiler, SLJIT_MOV32, SLJIT_MEM1(SLJIT_S0), dst_ofs + 8, SLJIT_R0, 0);
    }

    /* 更新目标物理寄存器 */
    if (!node->dest.is_spilled) {
        if (node->src1.is_spilled) {
            sljit_emit_op1(compiler, SLJIT_MOV, node->dest.phys_reg, 0, SLJIT_MEM1(SLJIT_S0), src_ofs);
        } else if (node->dest.phys_reg != node->src1.phys_reg) {
            sljit_emit_op1(compiler, SLJIT_MOV, node->dest.phys_reg, 0, node->src1.phys_reg, 0);
        }
    }
}

void ljit_cg_emit_loadi(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    if (node->src1.type == IR_VAL_INT) {
        int tvalue_size = sizeof(TValue);
        sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R0, 0, SLJIT_S0, 0, SLJIT_IMM, node->dest.v.reg * tvalue_size);
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM, node->src1.v.i);
        sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS2V(W, W), SLJIT_IMM, (sljit_sw)ljit_icall_set_integer);

        if (!node->dest.is_spilled) {
            sljit_emit_op1(compiler, SLJIT_MOV, node->dest.phys_reg, 0, SLJIT_IMM, node->src1.v.i);
        }
    }
}

void ljit_cg_emit_loadf(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    if (node->src1.type == IR_VAL_NUM) {
        int tvalue_size = sizeof(TValue);
        sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R0, 0, SLJIT_S0, 0, SLJIT_IMM, node->dest.v.reg * tvalue_size);
        union { lua_Number n; sljit_sw i; } u; u.n = node->src1.v.n;
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM, u.i);
        sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS2V(W, W), SLJIT_IMM, (sljit_sw)ljit_icall_set_number);

        if (!node->dest.is_spilled) {
            sljit_emit_op1(compiler, SLJIT_MOV, node->dest.phys_reg, 0, SLJIT_IMM, u.i);
        }
    }
}

void ljit_cg_emit_loadnil(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    int tvalue_size = sizeof(TValue);
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R0, 0, SLJIT_S0, 0, SLJIT_IMM, node->dest.v.reg * tvalue_size);
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS1V(W), SLJIT_IMM, (sljit_sw)ljit_icall_set_nil);

    if (!node->dest.is_spilled) {
        sljit_emit_op1(compiler, SLJIT_MOV, node->dest.phys_reg, 0, SLJIT_IMM, 0);
    }
}

void ljit_cg_emit_loadbool(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    if (node->src1.type == IR_VAL_INT) {
        int is_true = node->src1.v.i;
        int tvalue_size = sizeof(TValue);
        sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R0, 0, SLJIT_S0, 0, SLJIT_IMM, node->dest.v.reg * tvalue_size);
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM, is_true ? 1 : 0);
        sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS2V(W, W), SLJIT_IMM, (sljit_sw)ljit_icall_set_bool);

        if (!node->dest.is_spilled) {
            sljit_emit_op1(compiler, SLJIT_MOV, node->dest.phys_reg, 0, SLJIT_IMM, is_true ? 1 : 0);
        }
    }
}

void ljit_cg_emit_loadk(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    if (node->src1.type == IR_VAL_CONST) {
        sljit_sw k_ptr = (sljit_sw)&ctx->proto->k[node->src1.v.k];

        /* Load k_ptr into R0 */
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, k_ptr);

        /* Copy value_ (offset 0) */
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_MEM1(SLJIT_R0), 0);

        /* Always write to stack (required for icall-based ops like LEN, GETTABLE, etc.) */
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_MEM1(SLJIT_S0), node->dest.stack_ofs, SLJIT_R1, 0);

        /* Copy tt_ (offset 8) */
        sljit_emit_op1(compiler, SLJIT_MOV32, SLJIT_R1, 0, SLJIT_MEM1(SLJIT_R0), 8);
        sljit_emit_op1(compiler, SLJIT_MOV32, SLJIT_MEM1(SLJIT_S0), node->dest.stack_ofs + 8, SLJIT_R1, 0);

        if (!node->dest.is_spilled) {
            /* Also load value_ into the assigned register */
            sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, k_ptr);
            sljit_emit_op1(compiler, SLJIT_MOV, node->dest.phys_reg, 0, SLJIT_MEM1(SLJIT_R0), 0);
        }
    }
}
