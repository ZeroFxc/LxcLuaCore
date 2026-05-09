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

void ljit_cg_emit_cmp(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    ljit_cg_emit_load_operand(compiler, SLJIT_R0, &node->src1);
    ljit_cg_emit_load_operand(compiler, SLJIT_R1, &node->src2);

    sljit_s32 type;
    switch (node->op) {
        case IR_CMP_LT: type = SLJIT_LESS; break;
        case IR_CMP_LE: type = SLJIT_LESS_EQUAL; break;
        case IR_CMP_EQ: type = SLJIT_EQUAL; break;
        case IR_CMP_GT: type = SLJIT_GREATER; break;
        case IR_CMP_GE: type = SLJIT_GREATER_EQUAL; break;
        default: type = SLJIT_EQUAL; break;
    }

    /* In Lua, `if ((R[A] < sB) ~= k) then pc++`. This means if condition matches k, we don't jump,
       but if condition matches `~k`, we jump over the next instruction.
       Wait, let's read lopcodes.c: `if (cond != GETARG_k(i)) pc++;`. So if the condition DOES NOT match k,
       we skip the next instruction.
       If condition is true, and k=0: true != 0 is true, we skip (jump to pc+2).
       If condition is false, and k=0: false != 0 is false, we don't skip (execute pc+1).

       So we emit a jump if (condition ^ k) == true.
       Actually, `k` is typically 0 or 1.
       If `k=0`, we jump to pc+2 if condition is TRUE.
       If `k=1`, we jump to pc+2 if condition is FALSE.
    */
    int k = node->dest.v.i;
    if (k == 1) {
        /* Invert the condition */
        switch (type) {
            case SLJIT_LESS: type = SLJIT_GREATER_EQUAL; break;
            case SLJIT_LESS_EQUAL: type = SLJIT_GREATER; break;
            case SLJIT_EQUAL: type = SLJIT_NOT_EQUAL; break;
            case SLJIT_GREATER: type = SLJIT_LESS_EQUAL; break;
            case SLJIT_GREATER_EQUAL: type = SLJIT_LESS; break;
        }
    }

    struct sljit_jump *jmp = sljit_emit_cmp(compiler, type, SLJIT_R0, 0, SLJIT_R1, 0);
    if (jmp) {
        int idx = ctx->num_jumps++;
        ctx->jumps[idx] = jmp;
        /* Jump to original_pc + 2, skipping the next instruction (which is usually a JMP) */
        ctx->jump_targets[idx] = node->original_pc + 2;
    }
}

void ljit_cg_emit_ret(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    if (!node || !ctx || !ctx->compiler) return;

    // For RET, we simply emit a return void.
    sljit_emit_return_void((struct sljit_compiler *)ctx->compiler);
}

void ljit_cg_emit_concat(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    int tvalue_size = sizeof(TValue);

    /* R0 = ctx->L */
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);

    /* R1 = total elements (src1.v.i) */
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM, node->src1.v.i);

    /* R2 = ra address (base + dest.v.reg * size) */
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R2, 0, SLJIT_S0, 0, SLJIT_IMM, node->dest.v.reg * tvalue_size);

    /* Call ljit_icall_concat */
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3V(W, W, W), SLJIT_IMM, (sljit_sw)ljit_icall_concat);
}

void ljit_cg_emit_forprep(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    int tvalue_size = sizeof(TValue);

    /* R0 = ctx->L */
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);

    /* R1 = ra address (base + dest.v.reg * size) */
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R1, 0, SLJIT_S0, 0, SLJIT_IMM, node->dest.v.reg * tvalue_size);

    /* Call ljit_icall_forprep */
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS2(W, W, W), SLJIT_IMM, (sljit_sw)ljit_icall_forprep);

    /* The ICALL returns 1 to skip the loop (jump), 0 otherwise */
    struct sljit_jump *jmp = sljit_emit_cmp(compiler, SLJIT_NOT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, 0);
    if (jmp) {
        int idx = ctx->num_jumps++;
        ctx->jumps[idx] = jmp;
        /* src1.v.i is Bx, the jump offset */
        /* original_pc + Bx + 1 to skip loop body */
        ctx->jump_targets[idx] = node->original_pc + node->src1.v.i + 1;
    }
}

void ljit_cg_emit_forloop(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    int tvalue_size = sizeof(TValue);

    /* R0 = ctx->L */
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);

    /* R1 = ra address (base + dest.v.reg * size) */
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R1, 0, SLJIT_S0, 0, SLJIT_IMM, node->dest.v.reg * tvalue_size);

    /* Call ljit_icall_forloop */
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS2(W, W, W), SLJIT_IMM, (sljit_sw)ljit_icall_forloop);

    /* The ICALL returns 1 to jump back, 0 to continue forward */
    struct sljit_jump *jmp = sljit_emit_cmp(compiler, SLJIT_NOT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, 0);
    if (jmp) {
        int idx = ctx->num_jumps++;
        ctx->jumps[idx] = jmp;
        /* original_pc - Bx to jump back */
        ctx->jump_targets[idx] = node->original_pc - node->src1.v.i;
    }
}
