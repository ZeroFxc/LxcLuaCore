#include "ljit_codegen.h"
#include "../ir/ljit_ir.h"
#include "../sljit/ljit_sljit.h"

void ljit_cg_emit_add(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    if (node->dest.is_spilled && node->src1.is_spilled) {
        /*
         * Base pointer is in SLJIT_S0.
         * TValue layout:
         * offset 0: union Value value_ (which contains lua_Integer i)
         * offset 8: lu_byte tt_
         * We load the integer value directly for MVP.
         * src1 value into R0
         */
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_S0), node->src1.stack_ofs);

        if (node->src2.type == IR_VAL_INT) {
            /* src2 is an immediate integer */
            sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM, node->src2.v.i);
        } else if (node->src2.is_spilled) {
            /* src2 value into R1 */
            sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_MEM1(SLJIT_S0), node->src2.stack_ofs);
        } else {
            return;
        }

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

void ljit_cg_emit_idiv(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    if (node->dest.is_spilled && node->src1.is_spilled && node->src2.is_spilled) {
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_S0), node->src1.stack_ofs);
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_MEM1(SLJIT_S0), node->src2.stack_ofs);

        sljit_emit_op0(compiler, SLJIT_DIV_SW);

        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_MEM1(SLJIT_S0), node->dest.stack_ofs, SLJIT_R0, 0);
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

void ljit_cg_emit_unm(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    if (node->dest.is_spilled && node->src1.is_spilled) {
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_S0), node->src1.stack_ofs);
        sljit_emit_op2(compiler, SLJIT_SUB, SLJIT_R0, 0, SLJIT_IMM, 0, SLJIT_R0, 0);
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_MEM1(SLJIT_S0), node->dest.stack_ofs, SLJIT_R0, 0);
        /* Keep type tag same as src1. MVP assumes src1 is int for now */
        sljit_emit_op1(compiler, SLJIT_MOV32, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_S0), node->src1.stack_ofs + 8);
        sljit_emit_op1(compiler, SLJIT_MOV32, SLJIT_MEM1(SLJIT_S0), node->dest.stack_ofs + 8, SLJIT_R0, 0);
    }
}

void ljit_cg_emit_not(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    if (node->dest.is_spilled && node->src1.is_spilled) {
        /* Load tt_ of src1 */
        sljit_emit_op1(compiler, SLJIT_MOV32, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_S0), node->src1.stack_ofs + 8);

        /* Check if tag is LUA_VNIL (0) or LUA_VFALSE (1) */
        /* If tag <= 1, it's falsy, so NOT is true. Else it's truthy, so NOT is false. */

        struct sljit_jump *cmp_falsy = sljit_emit_cmp(compiler, SLJIT_LESS_EQUAL, SLJIT_R0, 0, SLJIT_IMM, 1);

        /* Truthy case -> result is FALSE (1) */
        sljit_emit_op1(compiler, SLJIT_MOV32, SLJIT_MEM1(SLJIT_S0), node->dest.stack_ofs + 8, SLJIT_IMM, 1);
        struct sljit_jump *end = sljit_emit_jump(compiler, SLJIT_JUMP);

        /* Falsy case -> result is TRUE (17) */
        sljit_set_label(cmp_falsy, sljit_emit_label(compiler));
        sljit_emit_op1(compiler, SLJIT_MOV32, SLJIT_MEM1(SLJIT_S0), node->dest.stack_ofs + 8, SLJIT_IMM, 17);

        sljit_set_label(end, sljit_emit_label(compiler));
    }
}

void ljit_cg_emit_band(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    if (node->dest.is_spilled && node->src1.is_spilled) {
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_S0), node->src1.stack_ofs);
        if (node->src2.type == IR_VAL_INT) {
            sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM, node->src2.v.i);
        } else if (node->src2.is_spilled) {
            sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_MEM1(SLJIT_S0), node->src2.stack_ofs);
        } else {
            return;
        }
        sljit_emit_op2(compiler, SLJIT_AND, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0);
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_MEM1(SLJIT_S0), node->dest.stack_ofs, SLJIT_R0, 0);
        sljit_emit_op1(compiler, SLJIT_MOV32, SLJIT_MEM1(SLJIT_S0), node->dest.stack_ofs + 8, SLJIT_IMM, 3);
    }
}

void ljit_cg_emit_bor(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    if (node->dest.is_spilled && node->src1.is_spilled) {
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_S0), node->src1.stack_ofs);
        if (node->src2.type == IR_VAL_INT) {
            sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM, node->src2.v.i);
        } else if (node->src2.is_spilled) {
            sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_MEM1(SLJIT_S0), node->src2.stack_ofs);
        } else {
            return;
        }
        sljit_emit_op2(compiler, SLJIT_OR, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0);
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_MEM1(SLJIT_S0), node->dest.stack_ofs, SLJIT_R0, 0);
        sljit_emit_op1(compiler, SLJIT_MOV32, SLJIT_MEM1(SLJIT_S0), node->dest.stack_ofs + 8, SLJIT_IMM, 3);
    }
}

void ljit_cg_emit_bxor(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    if (node->dest.is_spilled && node->src1.is_spilled) {
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_S0), node->src1.stack_ofs);
        if (node->src2.type == IR_VAL_INT) {
            sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM, node->src2.v.i);
        } else if (node->src2.is_spilled) {
            sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_MEM1(SLJIT_S0), node->src2.stack_ofs);
        } else {
            return;
        }
        sljit_emit_op2(compiler, SLJIT_XOR, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0);
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_MEM1(SLJIT_S0), node->dest.stack_ofs, SLJIT_R0, 0);
        sljit_emit_op1(compiler, SLJIT_MOV32, SLJIT_MEM1(SLJIT_S0), node->dest.stack_ofs + 8, SLJIT_IMM, 3);
    }
}

void ljit_cg_emit_shl(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    if (node->dest.is_spilled) {
        if (node->src1.type == IR_VAL_INT) {
            sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, node->src1.v.i);
        } else if (node->src1.is_spilled) {
            sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_S0), node->src1.stack_ofs);
        } else {
            return;
        }

        if (node->src2.type == IR_VAL_INT) {
            sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM, node->src2.v.i);
        } else if (node->src2.is_spilled) {
            sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_MEM1(SLJIT_S0), node->src2.stack_ofs);
        } else {
            return;
        }
        sljit_emit_op2(compiler, SLJIT_SHL, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0);
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_MEM1(SLJIT_S0), node->dest.stack_ofs, SLJIT_R0, 0);
        sljit_emit_op1(compiler, SLJIT_MOV32, SLJIT_MEM1(SLJIT_S0), node->dest.stack_ofs + 8, SLJIT_IMM, 3);
    }
}

void ljit_cg_emit_shr(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    if (node->dest.is_spilled && node->src1.is_spilled) {
        if (node->src2.type == IR_VAL_INT) {
            sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_S0), node->src1.stack_ofs);
            sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM, node->src2.v.i);
        } else if (node->src2.is_spilled) {
            sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_S0), node->src1.stack_ofs);
            sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_MEM1(SLJIT_S0), node->src2.stack_ofs);
        } else {
            return;
        }
        sljit_emit_op2(compiler, SLJIT_LSHR, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0);
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_MEM1(SLJIT_S0), node->dest.stack_ofs, SLJIT_R0, 0);
        sljit_emit_op1(compiler, SLJIT_MOV32, SLJIT_MEM1(SLJIT_S0), node->dest.stack_ofs + 8, SLJIT_IMM, 3);
    }
}

void ljit_cg_emit_bnot(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    if (node->dest.is_spilled && node->src1.is_spilled) {
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_S0), node->src1.stack_ofs);
        sljit_emit_op2(compiler, SLJIT_XOR, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, -1);
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_MEM1(SLJIT_S0), node->dest.stack_ofs, SLJIT_R0, 0);
        sljit_emit_op1(compiler, SLJIT_MOV32, SLJIT_MEM1(SLJIT_S0), node->dest.stack_ofs + 8, SLJIT_IMM, 3);
    }
}
