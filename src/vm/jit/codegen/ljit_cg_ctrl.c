#include "ljit_codegen.h"
#include "../ir/ljit_ir.h"
#include "../sljit/ljit_sljit.h"
#include "../../../core/lfunc.h"
#include "../../../core/ldo.h"
#include "../../../core/lobject.h"
#include "../../../core/lopcodes.h"

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

    int need_semantic = (node->src1.type == IR_VAL_CONST || node->src2.type == IR_VAL_CONST ||
                         node->src1.type == IR_VAL_NUM || node->src2.type == IR_VAL_NUM ||
                         (node->src1.type == IR_VAL_REG && node->src2.type == IR_VAL_REG));

    if (need_semantic) {
        /* Get TValue address of src1 */
        if (node->src1.type == IR_VAL_REG) {
            sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R1, 0, SLJIT_S0, 0, SLJIT_IMM, node->src1.stack_ofs);
        } else if (node->src1.type == IR_VAL_CONST) {
            sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM, (sljit_sw)&ctx->proto->k[node->src1.v.k]);
        } else {
            return;
        }
        /* Get TValue address of src2 */
        if (node->src2.type == IR_VAL_REG) {
            sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R2, 0, SLJIT_S0, 0, SLJIT_IMM, node->src2.stack_ofs);
        } else if (node->src2.type == IR_VAL_CONST) {
            sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, (sljit_sw)&ctx->proto->k[node->src2.v.k]);
        } else {
            return;
        }
        /* Call ljit_icall_compare(L, a, b, opcode|k) */
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R3, 0, SLJIT_IMM, ((sljit_sw)node->op << 1) | node->dest.v.i);
        sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4(W, W, W, W, W), SLJIT_IMM, (sljit_sw)ljit_icall_compare);
        /* R0: 1 = should jump (cond != k), 0 = fall through */
        struct sljit_jump *jmp = sljit_emit_cmp(compiler, SLJIT_NOT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, 0);
        if (jmp) {
            int idx = ctx->num_jumps++;
            ctx->jumps[idx] = jmp;
            ctx->jump_targets[idx] = node->original_pc + 2;
        }
    } else {
        sljit_s32 k = node->dest.v.i;
        ljit_cg_emit_load_operand(ctx, SLJIT_R0, &node->src1);
        ljit_cg_emit_load_operand(ctx, SLJIT_R1, &node->src2);
        sljit_s32 cond;
        switch (node->op) {
            case IR_CMP_EQ: cond = (k ? SLJIT_NOT_EQUAL : SLJIT_EQUAL); break;
            case IR_CMP_LT: cond = (k ? SLJIT_GREATER_EQUAL : SLJIT_LESS); break;
            case IR_CMP_LE: cond = (k ? SLJIT_GREATER : SLJIT_LESS_EQUAL); break;
            case IR_CMP_GT: cond = (k ? SLJIT_LESS_EQUAL : SLJIT_GREATER); break;
            case IR_CMP_GE: cond = (k ? SLJIT_LESS : SLJIT_GREATER_EQUAL); break;
            default: return;
        }
        struct sljit_jump *jmp = sljit_emit_cmp(compiler, cond, SLJIT_R0, 0, SLJIT_R1, 0);
        if (jmp) {
            int idx = ctx->num_jumps++;
            ctx->jumps[idx] = jmp;
            ctx->jump_targets[idx] = node->original_pc + 2;
        }
    }
}


void SLJIT_FUNC ljit_icall_return(lua_State *L, StkId ra, int b, int original_pc) {
    LClosure *cl = clLvalue(s2v(L->ci->func.p));
    Instruction i = cl->p->code[original_pc];
    int n = b; /* b is already B - 1 or correct count */
    if (n < 0) n = cast_int(L->top.p - ra);



    if (TESTARG_k(i)) {
        L->ci->u2.nres = n;
        if (L->top.p < L->ci->top.p) L->top.p = L->ci->top.p;
        luaF_close(L, L->ci->func.p + 1, CLOSEKTOP, 1);
    }

    int c = GETARG_C(i);
    if (c) {
        L->ci->func.p -= L->ci->u.l.nextraargs + c;
    }

    L->top.p = ra + n;
    luaD_poscall(L, L->ci, n);
}



void ljit_cg_emit_ret(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    int tvalue_size = sizeof(TValue);

    /* R0 = L */
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);

    /* R1 = ra (base + src1.v.reg * size if OP_RETURN, but it might be OP_RETURN0 or OP_RETURN1) */
    /* Let's just pass base and compute ra in ICALL or compute it here */
    if (node->src1.type == IR_VAL_REG) {
        sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R1, 0, SLJIT_S0, 0, SLJIT_IMM, node->src1.v.reg * tvalue_size);
    } else {
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_S0, 0); /* fallback to base */
    }

    /* R2 = B (src2.v.i) */
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, node->src2.v.i);

    /* R3 = original_pc */
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R3, 0, SLJIT_IMM, node->original_pc);

    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4V(W, W, W, W), SLJIT_IMM, (sljit_sw)ljit_icall_return);
    sljit_emit_return_void(compiler);
}


void ljit_cg_emit_concat(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    int tvalue_size = sizeof(TValue);
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM, node->src1.v.i);
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R2, 0, SLJIT_S0, 0, SLJIT_IMM, node->dest.v.reg * tvalue_size);
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3V(W, W, W), SLJIT_IMM, (sljit_sw)ljit_icall_concat);
}

void ljit_cg_emit_forprep(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    int tvalue_size = sizeof(TValue);
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R1, 0, SLJIT_S0, 0, SLJIT_IMM, node->dest.v.reg * tvalue_size);
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS2(W, W, W), SLJIT_IMM, (sljit_sw)ljit_icall_forprep);

    struct sljit_jump *jmp = sljit_emit_cmp(compiler, SLJIT_NOT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, 0);
    if (jmp) {
        int idx = ctx->num_jumps++;
        ctx->jumps[idx] = jmp;
        ctx->jump_targets[idx] = node->original_pc + node->src1.v.i + 2;
    }
}

void ljit_cg_emit_forloop(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    int tvalue_size = sizeof(TValue);
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R1, 0, SLJIT_S0, 0, SLJIT_IMM, node->dest.v.reg * tvalue_size);
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS2(W, W, W), SLJIT_IMM, (sljit_sw)ljit_icall_forloop);

    struct sljit_jump *jmp = sljit_emit_cmp(compiler, SLJIT_NOT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, 0);
    if (jmp) {
        int idx = ctx->num_jumps++;
        ctx->jumps[idx] = jmp;
        ctx->jump_targets[idx] = node->original_pc + 1 - node->src1.v.i;
    }
}

extern sljit_sw SLJIT_FUNC ljit_icall_tforprep(lua_State *L, StkId ra);
extern void SLJIT_FUNC ljit_icall_tforcall(lua_State *L, StkId ra, int c);
extern sljit_sw SLJIT_FUNC ljit_icall_tforloop(lua_State *L, StkId ra);

void ljit_cg_emit_tforprep(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    int tvalue_size = sizeof(TValue);
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R1, 0, SLJIT_S0, 0, SLJIT_IMM, node->dest.v.reg * tvalue_size);

    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS2V(W, W), SLJIT_IMM, (sljit_sw)ljit_icall_tforprep);

    // Jump by GETARG_Bx(i) instruction. But wait, IR_TFORPREP does not have a branch in JIT? 
    // Actually, in lvm.c:
    // pc += GETARG_Bx(i);
    // i = *(pc++);
    // goto l_tforcall;
    // So the JIT should jump to the next instruction after the jump offset.
    // Let's look at how OP_TFORPREP is translated.
    struct sljit_jump *jmp = sljit_emit_jump(compiler, SLJIT_JUMP);
    if (jmp) {
        int idx = ctx->num_jumps++;
        ctx->jumps[idx] = jmp;
        ctx->jump_targets[idx] = node->original_pc + node->src1.v.i + 1; // It has a Bx argument! Let's check ljit_translate.c
    }
}

void ljit_cg_emit_tforcall(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    int tvalue_size = sizeof(TValue);
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R1, 0, SLJIT_S0, 0, SLJIT_IMM, node->dest.v.reg * tvalue_size);
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, node->src1.v.i); // C argument
    
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3V(W, W, W), SLJIT_IMM, (sljit_sw)ljit_icall_tforcall);
}

void ljit_cg_emit_tforloop(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    int tvalue_size = sizeof(TValue);
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R1, 0, SLJIT_S0, 0, SLJIT_IMM, node->dest.v.reg * tvalue_size);

    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS2(W, W, W), SLJIT_IMM, (sljit_sw)ljit_icall_tforloop);

    struct sljit_jump *jmp = sljit_emit_cmp(compiler, SLJIT_NOT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, 0);
    if (jmp) {
        int idx = ctx->num_jumps++;
        ctx->jumps[idx] = jmp;
        ctx->jump_targets[idx] = node->original_pc + 1 - node->src1.v.i; // Backwards jump from the NEXT instruction
    }
}
