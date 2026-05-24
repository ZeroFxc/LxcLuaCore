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
                         node->src1.type == IR_VAL_NUM || node->src2.type == IR_VAL_NUM);

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

    int ra = node->dest.v.reg;
    int bx = node->src1.v.i;
    int tvalue_size = sizeof(TValue);

    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R1, 0, SLJIT_S0, 0,
        SLJIT_IMM, (sljit_sw)(ra * tvalue_size));
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS2(W, W, W),
        SLJIT_IMM, (sljit_sw)ljit_icall_forprep);

    /* C 函数调用已修改 Lua 栈上的 ra+3 (用户可见循环变量),
     * 但此前 live-in scan 分配的 phys_reg 仍是旧值,
     * 必须在进入循环体前从栈重新加载 */
    {
        ljit_ir_node_t *scan = ctx->ir_head;
        int reloaded = 0;
        while (scan && !reloaded) {
            if (scan->dest.type == IR_VAL_REG && scan->dest.v.reg == ra + 3
                && !scan->dest.is_spilled) {
                sljit_emit_op1(compiler, SLJIT_MOV, scan->dest.phys_reg, 0,
                    SLJIT_MEM1(SLJIT_S0), (sljit_sw)((ra + 3) * tvalue_size));
                reloaded = 1;
            }
            if (!reloaded && scan->src1.type == IR_VAL_REG
                && scan->src1.v.reg == ra + 3 && !scan->src1.is_spilled) {
                sljit_emit_op1(compiler, SLJIT_MOV, scan->src1.phys_reg, 0,
                    SLJIT_MEM1(SLJIT_S0), (sljit_sw)((ra + 3) * tvalue_size));
                reloaded = 1;
            }
            if (!reloaded && scan->src2.type == IR_VAL_REG
                && scan->src2.v.reg == ra + 3 && !scan->src2.is_spilled) {
                sljit_emit_op1(compiler, SLJIT_MOV, scan->src2.phys_reg, 0,
                    SLJIT_MEM1(SLJIT_S0), (sljit_sw)((ra + 3) * tvalue_size));
                reloaded = 1;
            }
            scan = scan->next;
        }
    }

    struct sljit_jump *jmp = sljit_emit_cmp(compiler, SLJIT_NOT_EQUAL,
        SLJIT_R0, 0, SLJIT_IMM, 0);
    if (jmp) {
        int idx = ctx->num_jumps++;
        ctx->jumps[idx] = jmp;
        ctx->jump_targets[idx] = node->original_pc + bx + 2;
    }
}

void ljit_cg_emit_forloop(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    int ra = node->dest.v.reg;
    int bx = node->src1.v.i;
    int tvalue_size = sizeof(TValue);
    int value_size = sizeof(Value);

    /* 检查 ra+2 的 tt_ 字段是否为 LUA_VNUMINT (整数步长) */
    sljit_emit_op1(compiler, SLJIT_MOV32, SLJIT_R3, 0,
        SLJIT_MEM1(SLJIT_S0), (sljit_sw)((ra + 2) * tvalue_size + value_size));
    struct sljit_jump *to_float = sljit_emit_cmp(compiler, SLJIT_NOT_EQUAL,
        SLJIT_R3, 0, SLJIT_IMM, (sljit_sw)LUA_VNUMINT);

    /* ===== 内联整数 FORLOOP 快速路径 ===== */

    /* 加载循环计数器 count = *(ra+1).value_ (有符号整数) */
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0,
        SLJIT_MEM1(SLJIT_S0), (sljit_sw)((ra + 1) * tvalue_size));

    /* 如果 count <= 0 则退出循环 */
    struct sljit_jump *exit_loop = sljit_emit_cmp(compiler, SLJIT_LESS_EQUAL,
        SLJIT_R0, 0, SLJIT_IMM, 0);

    /* count -= 1, 写回 ra+1 */
    sljit_emit_op2(compiler, SLJIT_SUB, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, 1);
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_MEM1(SLJIT_S0),
        (sljit_sw)((ra + 1) * tvalue_size), SLJIT_R0, 0);

    /* 加载 step = *(ra+2).value_ */
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0,
        SLJIT_MEM1(SLJIT_S0), (sljit_sw)((ra + 2) * tvalue_size));

    /* 加载 idx = *(ra).value_ */
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0,
        SLJIT_MEM1(SLJIT_S0), (sljit_sw)(ra * tvalue_size));

    /* idx += step */
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R2, 0, SLJIT_R2, 0, SLJIT_R1, 0);

    /* 写回 idx 到 ra 和 ra+3 (用户可见循环变量) */
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_MEM1(SLJIT_S0),
        (sljit_sw)(ra * tvalue_size), SLJIT_R2, 0);
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_MEM1(SLJIT_S0),
        (sljit_sw)((ra + 3) * tvalue_size), SLJIT_R2, 0);

    /* 重新加载 ra+3 的 phys_reg: 栈已更新, 但非spilled的S寄存器仍是旧值 */
    {
        ljit_ir_node_t *scan = ctx->ir_head;
        int reloaded = 0;
        while (scan && !reloaded) {
            if (scan->dest.type == IR_VAL_REG && scan->dest.v.reg == ra + 3
                && !scan->dest.is_spilled) {
                sljit_emit_op1(compiler, SLJIT_MOV, scan->dest.phys_reg, 0,
                    SLJIT_MEM1(SLJIT_S0), (sljit_sw)((ra + 3) * tvalue_size));
                reloaded = 1;
            }
            if (!reloaded && scan->src1.type == IR_VAL_REG
                && scan->src1.v.reg == ra + 3 && !scan->src1.is_spilled) {
                sljit_emit_op1(compiler, SLJIT_MOV, scan->src1.phys_reg, 0,
                    SLJIT_MEM1(SLJIT_S0), (sljit_sw)((ra + 3) * tvalue_size));
                reloaded = 1;
            }
            if (!reloaded && scan->src2.type == IR_VAL_REG
                && scan->src2.v.reg == ra + 3 && !scan->src2.is_spilled) {
                sljit_emit_op1(compiler, SLJIT_MOV, scan->src2.phys_reg, 0,
                    SLJIT_MEM1(SLJIT_S0), (sljit_sw)((ra + 3) * tvalue_size));
                reloaded = 1;
            }
            scan = scan->next;
        }
    }

    /* 跳回循环体起始 */
    struct sljit_jump *jmp_loop = sljit_emit_jump(compiler, SLJIT_JUMP);
    if (jmp_loop) {
        int idx = ctx->num_jumps++;
        ctx->jumps[idx] = jmp_loop;
        ctx->jump_targets[idx] = node->original_pc + 1 - bx;
    }

    /* 退出标签: count == 0 时跳到这里 */
    struct sljit_label *lbl_exit = sljit_emit_label(compiler);
    sljit_set_label(exit_loop, lbl_exit);

    /* 跳过浮点回退路径 */
    struct sljit_jump *skip_float = sljit_emit_jump(compiler, SLJIT_JUMP);

    /* ===== 浮点回退路径 (通过 C 函数) ===== */
    struct sljit_label *lbl_float = sljit_emit_label(compiler);
    sljit_set_label(to_float, lbl_float);

    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R1, 0, SLJIT_S0, 0,
        SLJIT_IMM, (sljit_sw)(ra * tvalue_size));
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS2(W, W, W),
        SLJIT_IMM, (sljit_sw)ljit_icall_forloop);

    /* icall 返回后 R0 = 1(继续) 或 0(退出) */
    struct sljit_jump *jmp_float_loop = sljit_emit_cmp(compiler, SLJIT_NOT_EQUAL,
        SLJIT_R0, 0, SLJIT_IMM, 0);
    if (jmp_float_loop) {
        int idx2 = ctx->num_jumps++;
        ctx->jumps[idx2] = jmp_float_loop;
        ctx->jump_targets[idx2] = node->original_pc + 1 - bx;
    }

    struct sljit_label *lbl_done = sljit_emit_label(compiler);
    sljit_set_label(skip_float, lbl_done);
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
