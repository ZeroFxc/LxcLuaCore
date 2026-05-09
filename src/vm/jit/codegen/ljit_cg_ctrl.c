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

    ljit_cg_emit_load_operand((struct ljit_ctx *)ctx, SLJIT_R0, &node->src1);
    ljit_cg_emit_load_operand((struct ljit_ctx *)ctx, SLJIT_R1, &node->src2);

    sljit_s32 type;
    switch (node->op) {
        case IR_CMP_LT: type = SLJIT_LESS; break;
        case IR_CMP_LE: type = SLJIT_LESS_EQUAL; break;
        case IR_CMP_EQ: type = SLJIT_EQUAL; break;
        case IR_CMP_GT: type = SLJIT_GREATER; break;
        case IR_CMP_GE: type = SLJIT_GREATER_EQUAL; break;
        default: type = SLJIT_EQUAL; break;
    }

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
        ctx->jump_targets[idx] = node->original_pc + 2;
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
        ctx->jump_targets[idx] = node->original_pc + node->src1.v.i + 1;
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
        ctx->jump_targets[idx] = node->original_pc - node->src1.v.i;
    }
}
