#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ljit_analyze.h"
#include "../core/ljit_debug.h"
#include "../../../core/lopcodes.h"

/*
 * 根据指令推断目标寄存器类型
 * 返回值: 该指令定义目标寄存器后的类型
 */
static ljit_type_t infer_dest_type(Proto *proto, Instruction i, ljit_type_t *state) {
    OpCode op = GET_OPCODE(i);
    int a = GETARG_A(i);

    switch (op) {
        case OP_LOADI:
            return JIT_TYPE_INT;
        case OP_LOADF:
            return JIT_TYPE_NUM;
        case OP_LOADK: {
            int bx = GETARG_Bx(i);
            if (bx < proto->sizek) {
                const TValue *kv = &proto->k[bx];
                if (ttisinteger(kv)) return JIT_TYPE_INT;
                else if (ttisfloat(kv)) return JIT_TYPE_NUM;
                else if (ttisstring(kv)) return JIT_TYPE_STR;
                else if (ttisboolean(kv)) return JIT_TYPE_BOOL;
                else if (ttisnil(kv)) return JIT_TYPE_NIL;
                else if (ttistable(kv)) return JIT_TYPE_TAB;
                else if (ttisfunction(kv)) return JIT_TYPE_FUNC;
            }
            return JIT_TYPE_ANY;
        }
        case OP_LOADKX:
            return JIT_TYPE_ANY;
        case OP_LOADFALSE:
        case OP_LOADTRUE:
        case OP_LFALSESKIP:
            return JIT_TYPE_BOOL;
        case OP_NEWTABLE:
        case OP_NEWMAP:
            return JIT_TYPE_TAB;
        case OP_CLOSURE:
            return JIT_TYPE_FUNC;
        case OP_ADD: case OP_SUB: case OP_MUL:
        case OP_DIV: case OP_MOD: case OP_POW:
        case OP_UNM: case OP_ADDI: case OP_ADDK:
        case OP_SUBK: case OP_MULK: case OP_DIVK:
        case OP_MODK: case OP_POWK:
            return JIT_TYPE_NUM;
        case OP_IDIV: case OP_BAND: case OP_BOR:
        case OP_BXOR: case OP_SHL: case OP_SHR:
        case OP_BNOT: case OP_SHLI: case OP_SHRI:
        case OP_IDIVK: case OP_BANDK: case OP_BORK:
        case OP_BXORK:
            return JIT_TYPE_INT;
        case OP_NOT:
            return JIT_TYPE_BOOL;
        case OP_CONCAT:
            return JIT_TYPE_STR;
        case OP_LEN:
            return JIT_TYPE_INT;
        case OP_GETUPVAL: case OP_GETTABUP:
        case OP_GETTABLE: case OP_GETI:
        case OP_GETFIELD: case OP_MAPGET:
        case OP_GETSUPER: case OP_GETPROP:
            return JIT_TYPE_ANY;
        case OP_CALL: case OP_TAILCALL:
        case OP_NEWCLASS: case OP_NEWOBJ:
            return JIT_TYPE_ANY;
        case OP_MOVE: {
            int b = GETARG_B(i);
            if (state && b >= 0) return state[b];
            return JIT_TYPE_ANY;
        }
        case OP_SELF:
            return JIT_TYPE_ANY;
        case OP_VARARG: case OP_VARARGPREP:
            return JIT_TYPE_ANY;
        case OP_TESTSET:
            return JIT_TYPE_BOOL;
        case OP_FORPREP: case OP_FORLOOP:
        case OP_TFORPREP: case OP_TFORLOOP:
        case OP_TFORCALL:
            return JIT_TYPE_ANY;
        default:
            return JIT_TYPE_ANY;
    }
}

/*
 * 更新state数组：执行指令后更新定义的寄存器类型
 */
static void update_state_after_ins(Proto *proto, Instruction i, ljit_type_t *state, int max_regs) {
    if (!state) return;
    OpCode op = GET_OPCODE(i);
    int a = GETARG_A(i);

    switch (op) {
        /* 这些指令不定义新值，只是修改对象内容，保持寄存器类型不变 */
        case OP_SETTABLE: case OP_SETTABUP: case OP_MAPSET:
        case OP_SETLIST: case OP_SETUPVAL: case OP_SETI:
        case OP_SETFIELD: case OP_SETMETHOD: case OP_SETPROP:
        case OP_SETSTATIC: case OP_SETSUPER: case OP_CLOSE:
        case OP_TBC:
            break;
        case OP_LOADNIL: {
            int b = GETARG_B(i);
            for (int reg = a; reg <= a + b && reg < max_regs; reg++) {
                state[reg] = JIT_TYPE_NIL;
            }
            break;
        }
        case OP_SELF: {
            int b = GETARG_B(i);
            if (a < max_regs) state[a] = JIT_TYPE_ANY;
            if (a + 1 < max_regs && b < max_regs) {
                state[a + 1] = state[b];
            }
            break;
        }
        case OP_MOVE: {
            int b = GETARG_B(i);
            if (a < max_regs && b < max_regs) {
                state[a] = state[b];
            }
            break;
        }
        default: {
            ljit_type_t t = infer_dest_type(proto, i, state);
            if (a < max_regs && a >= 0) {
                state[a] = t;
            }
            break;
        }
    }
}

void ljit_translate(ljit_ctx_t *ctx) {
    if (!ctx || !ctx->proto) return;

    Proto *proto = ctx->proto;
    ljit_analyze_info_t *ainfo = (ljit_analyze_info_t *)ctx->analyze_info;
    int max_regs = ainfo ? ainfo->max_regs : proto->maxstacksize;

    JIT_DBG(MOD_TR, "translate sizecode=%d, maxstacksize=%d", proto->sizecode, proto->maxstacksize);
    for (int pc = 0; pc < proto->sizecode; pc++) {
        Instruction i = proto->code[pc];
        OpCode op = GET_OPCODE(i);
        JIT_DBG(MOD_TR, "  pc=%d op=%d A=%d Bx=%d", pc, op, GETARG_A(i), GETARG_Bx(i));
    }

    /* 分配局部类型状态数组，用于BB内精确类型传播 */
    ljit_type_t *state = (ljit_type_t *)malloc(max_regs * sizeof(ljit_type_t));
    if (!state) return;

    /* 遍历每个BB，按BB顺序翻译 */
    for (ljit_bb_t *bb = ctx->cfg; bb; bb = bb->next) {
        /* BB入口：初始化state为该BB的入口类型（join合并后） */
        if (ainfo && ainfo->in_types) {
            memcpy(state, ainfo->in_types + bb->bb_id * max_regs, max_regs * sizeof(ljit_type_t));
        } else {
            /* 无CFG分析结果，全部保守设为ANY */
            for (int r = 0; r < max_regs; r++) state[r] = JIT_TYPE_ANY;
        }

        /* BB内逐pc翻译 */
        for (int pc = bb->start_pc; pc <= bb->end_pc; pc++) {
            Instruction i = proto->code[pc];
            OpCode op = GET_OPCODE(i);

            switch (op) {
                case OP_MOVE: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_MOV, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                    node->flags = infer_dest_type(proto, i, state);
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_LOADI: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_LOADI, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_INT; node->src1.v.i = GETARG_sBx(i);
                    node->flags = JIT_TYPE_INT;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_LOADF: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_LOADF, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_NUM; node->src1.v.n = (lua_Number)GETARG_sBx(i);
                    node->flags = JIT_TYPE_NUM;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_LOADK: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_LOADK, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_CONST; node->src1.v.k = GETARG_Bx(i);
                    node->flags = infer_dest_type(proto, i, state);
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_LOADFALSE:
                case OP_LOADTRUE: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_LOADBOOL, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_INT; node->src1.v.i = (op == OP_LOADTRUE) ? 1 : 0;
                    node->flags = JIT_TYPE_BOOL;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_LFALSESKIP: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_LOADBOOL, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_INT; node->src1.v.i = 0;
                    node->flags = JIT_TYPE_BOOL;
                    ljit_ir_append(ctx, node);

                    ljit_ir_node_t *jmp_node = ljit_ir_new(IR_JMP, pc);
                    jmp_node->dest.type = IR_VAL_LABEL;
                    jmp_node->dest.v.label_id = pc + 2;
                    ljit_ir_append(ctx, jmp_node);
                    break;
                }
                case OP_LOADNIL: {
                    int a = GETARG_A(i);
                    int b = GETARG_B(i);
                    for (int j = 0; j <= b; j++) {
                        ljit_ir_node_t *node = ljit_ir_new(IR_LOADNIL, pc);
                        node->dest.type = IR_VAL_REG; node->dest.v.reg = a + j;
                        node->flags = JIT_TYPE_NIL;
                        ljit_ir_append(ctx, node);
                    }
                    break;
                }
                case OP_GETTABLE: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_GETTABLE, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                    node->src2.type = IR_VAL_REG; node->src2.v.reg = GETARG_C(i);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_SETTABLE: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_SETTABLE, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                    node->src2.type = IR_VAL_REG; node->src2.v.reg = GETARG_C(i);
                    node->flags = state[GETARG_A(i)];
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_NEWTABLE: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_NEWTABLE, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_INT; node->src1.v.i = GETARG_vB(i);
                    node->src2.type = IR_VAL_INT; node->src2.v.i = GETARG_vC(i);
                    node->flags = JIT_TYPE_TAB;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_NEWMAP: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_NEWMAP, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->flags = JIT_TYPE_TAB;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_MAPGET: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_GETMAP, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                    node->src2.type = IR_VAL_REG; node->src2.v.reg = GETARG_C(i);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_MAPSET: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_SETMAP, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                    node->src2.type = IR_VAL_REG; node->src2.v.reg = GETARG_C(i);
                    node->flags = state[GETARG_A(i)];
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_ADD:
                case OP_SUB:
                case OP_MUL:
                case OP_MOD:
                case OP_POW:
                case OP_DIV:
                case OP_IDIV:
                case OP_BAND:
                case OP_BOR:
                case OP_BXOR:
                case OP_SHL:
                case OP_SHR: {
                    ljit_ir_op_t ir_op = IR_NOP;
                    if (op == OP_ADD) ir_op = IR_ADD;
                    else if (op == OP_SUB) ir_op = IR_SUB;
                    else if (op == OP_MUL) ir_op = IR_MUL;
                    else if (op == OP_MOD) ir_op = IR_MOD;
                    else if (op == OP_POW) ir_op = IR_POW;
                    else if (op == OP_DIV) ir_op = IR_DIV;
                    else if (op == OP_IDIV) ir_op = IR_IDIV;
                    else if (op == OP_BAND) ir_op = IR_BAND;
                    else if (op == OP_BOR) ir_op = IR_BOR;
                    else if (op == OP_BXOR) ir_op = IR_BXOR;
                    else if (op == OP_SHL) ir_op = IR_SHL;
                    else if (op == OP_SHR) ir_op = IR_SHR;

                    ljit_ir_node_t *node = ljit_ir_new(ir_op, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                    node->src2.type = IR_VAL_REG; node->src2.v.reg = GETARG_C(i);
                    node->flags = infer_dest_type(proto, i, state);
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_UNM:
                case OP_BNOT:
                case OP_NOT: {
                    ljit_ir_op_t ir_op = IR_NOP;
                    if (op == OP_UNM) ir_op = IR_UNM;
                    else if (op == OP_BNOT) ir_op = IR_BNOT;
                    else if (op == OP_NOT) ir_op = IR_NOT;

                    ljit_ir_node_t *node = ljit_ir_new(ir_op, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                    node->flags = infer_dest_type(proto, i, state);
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_ADDI: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_ADD, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                    node->src2.type = IR_VAL_INT; node->src2.v.i = GETARG_sC(i);
                    node->flags = JIT_TYPE_NUM;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_SHLI: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_SHL, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_INT; node->src1.v.i = GETARG_sC(i);
                    node->src2.type = IR_VAL_REG; node->src2.v.reg = GETARG_B(i);
                    node->flags = JIT_TYPE_INT;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_SHRI: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_SHR, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                    node->src2.type = IR_VAL_INT; node->src2.v.i = GETARG_sC(i);
                    node->flags = JIT_TYPE_INT;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_LT:
                case OP_LE:
                case OP_EQ: {
                    ljit_ir_op_t ir_op = (op == OP_LT) ? IR_CMP_LT : ((op == OP_LE) ? IR_CMP_LE : IR_CMP_EQ);
                    ljit_ir_node_t *node = ljit_ir_new(ir_op, pc);
                    node->dest.type = IR_VAL_INT; node->dest.v.i = GETARG_k(i);
                    node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_A(i);
                    node->src2.type = IR_VAL_REG; node->src2.v.reg = GETARG_B(i);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_LTI:
                case OP_LEI:
                case OP_GTI:
                case OP_GEI:
                case OP_EQI: {
                    ljit_ir_op_t ir_op;
                    if (op == OP_LTI) ir_op = IR_CMP_LT;
                    else if (op == OP_LEI) ir_op = IR_CMP_LE;
                    else if (op == OP_GTI) ir_op = IR_CMP_GT;
                    else if (op == OP_GEI) ir_op = IR_CMP_GE;
                    else ir_op = IR_CMP_EQ;

                    ljit_ir_node_t *node = ljit_ir_new(ir_op, pc);
                    node->dest.type = IR_VAL_INT; node->dest.v.i = GETARG_k(i);
                    node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_A(i);
                    node->src2.type = IR_VAL_INT; node->src2.v.i = GETARG_sB(i);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_JMP: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_JMP, pc);
                    node->dest.type = IR_VAL_LABEL;
                    node->dest.v.label_id = pc + 1 + GETARG_sJ(i);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_MMBINI:
                case OP_MMBINK:
                case OP_MMBIN: {
                    /* 元方法二元操作：操作成功时跳过，失败时调用元方法
                     * 当前JIT不支持元方法内联，生成IR_NOP占位，运行时会回退到解释器处理 */
                    JIT_DBG(MOD_TR, "MMBIN: pc=%d op=%d, generating fallback NOP", pc, op);
                    ljit_ir_node_t *node = ljit_ir_new(IR_NOP, pc);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_CONCAT: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_CONCAT, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_INT; node->src1.v.i = GETARG_B(i);
                    node->flags = JIT_TYPE_STR;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_TFORCALL: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_TFORCALL, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_INT; node->src1.v.i = GETARG_C(i);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_TFORLOOP: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_TFORLOOP, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_INT; node->src1.v.i = GETARG_Bx(i);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_FORPREP: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_FORPREP, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_INT; node->src1.v.i = GETARG_Bx(i);
                    JIT_DBG(MOD_TR, "FORPREP pc=%d A=%d Bx=%d", pc, GETARG_A(i), GETARG_Bx(i));
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_FORLOOP: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_FORLOOP, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_INT; node->src1.v.i = GETARG_Bx(i);
                    JIT_DBG(MOD_TR, "FORLOOP pc=%d A=%d Bx=%d", pc, GETARG_A(i), GETARG_Bx(i));
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_VARARG: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_VARARG, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_INT; node->src1.v.i = GETARG_B(i);
                    node->src2.type = IR_VAL_INT; node->src2.v.i = GETARG_C(i);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_VARARGPREP: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_VARARGPREP, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_INT; node->src1.v.i = GETARG_B(i);
                    node->src2.type = IR_VAL_INT; node->src2.v.i = GETARG_C(i);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_NEWCLASS: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_NEWCLASS, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_INT; node->src1.v.i = GETARG_Bx(i);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_NEWOBJ: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_NEWOBJ, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                    node->src2.type = IR_VAL_INT; node->src2.v.i = GETARG_C(i);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_CLOSURE: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_CLOSURE, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_INT; node->src1.v.i = GETARG_Bx(i);
                    node->flags = JIT_TYPE_FUNC;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_CALL:
                case OP_TAILCALL: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_CALL, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_INT; node->src1.v.i = GETARG_B(i) - 1;
                    node->src2.type = IR_VAL_INT; node->src2.v.i = GETARG_C(i) - 1;

                    int func_reg = GETARG_A(i);
                    for (int scan = pc - 1; scan >= 0; scan--) {
                        Instruction si = proto->code[scan];
                        OpCode sop = GET_OPCODE(si);
                        int sa = GETARG_A(si);
                        int writes_reg = 0;
                        switch (sop) {
                            case OP_MOVE: case OP_LOADI: case OP_LOADF: case OP_LOADK:
                            case OP_LOADKX: case OP_LOADFALSE: case OP_LOADTRUE:
                            case OP_LOADNIL: case OP_GETUPVAL: case OP_GETTABUP:
                            case OP_GETTABLE: case OP_GETI: case OP_GETFIELD:
                            case OP_NEWTABLE: case OP_NEWMAP: case OP_MAPGET:
                            case OP_ADD: case OP_SUB: case OP_MUL:
                            case OP_MOD: case OP_POW: case OP_DIV: case OP_IDIV:
                            case OP_BAND: case OP_BOR: case OP_BXOR: case OP_SHL:
                            case OP_SHR: case OP_SHLI: case OP_SHRI: case OP_UNM:
                            case OP_BNOT: case OP_NOT: case OP_LEN: case OP_CONCAT:
                            case OP_CALL: case OP_TAILCALL: case OP_NEWCLASS:
                            case OP_NEWOBJ: case OP_CLOSURE: case OP_SELF:
                            case OP_ADDK: case OP_SUBK: case OP_MULK: case OP_MODK:
                            case OP_POWK: case OP_DIVK: case OP_IDIVK: case OP_BANDK:
                            case OP_BORK: case OP_BXORK: case OP_VARARG:
                            case OP_CASE: case OP_NEWCONCEPT: case OP_NEWNAMESPACE:
                            case OP_NEWSUPER: case OP_GETCMDS: case OP_GETOPS:
                            case OP_ASYNCWRAP: case OP_AWAIT: case OP_GENERICWRAP:
                            case OP_MERGE: case OP_REGEX:
                            case OP_ADDI: case OP_SPACESHIP: case OP_TESTSET:
                            case OP_GETSUPER: case OP_GETPROP: case OP_IN:
                            case OP_SLICE:
                                writes_reg = (sa == func_reg); break;
                            default: break;
                        }
                        if (writes_reg) {
                            if (sop == OP_CLOSURE) {
                                int bx = GETARG_Bx(si);
                                if (bx < proto->sizep && proto->p[bx] == ctx->proto) {
                                    node->self_rec = 1;
                                    JIT_DBG(MOD_TR, "OP_CALL pc=%d: detected self-recursion (closure at pc=%d)", pc, scan);
                                }
                            } else if (sop == OP_GETUPVAL) {
                                int upval_idx = GETARG_B(si);
                                int found_chain = 0;
                                for (int scan2 = scan - 1; scan2 >= 0; scan2--) {
                                    Instruction si2 = proto->code[scan2];
                                    if (GET_OPCODE(si2) == OP_SETUPVAL && GETARG_B(si2) == upval_idx) {
                                        int src_reg = GETARG_A(si2);
                                        found_chain = 1;
                                        for (int scan3 = scan2 - 1; scan3 >= 0; scan3--) {
                                            Instruction si3 = proto->code[scan3];
                                            OpCode sop3 = GET_OPCODE(si3);
                                            if (sop3 == OP_CLOSURE && GETARG_A(si3) == src_reg) {
                                                int bx = GETARG_Bx(si3);
                                                if (bx < proto->sizep && proto->p[bx] == ctx->proto) {
                                                    node->self_rec = 1;
                                                    JIT_DBG(MOD_TR, "OP_CALL pc=%d: detected self-recursion (upval at pc=%d, closure at pc=%d)", pc, scan, scan3);
                                                }
                                                break;
                                            }
                                            int writes_src = 0;
                                            switch (sop3) {
                                                case OP_MOVE: case OP_LOADI: case OP_LOADF: case OP_LOADK:
                                                case OP_LOADKX: case OP_LOADFALSE: case OP_LOADTRUE:
                                                case OP_LOADNIL: case OP_GETUPVAL: case OP_GETTABUP:
                                                case OP_GETTABLE: case OP_GETI: case OP_GETFIELD:
                                                case OP_NEWTABLE: case OP_NEWMAP: case OP_MAPGET:
                                                case OP_ADD: case OP_SUB: case OP_MUL:
                                                case OP_MOD: case OP_POW: case OP_DIV: case OP_IDIV:
                                                case OP_BAND: case OP_BOR: case OP_BXOR: case OP_SHL:
                                                case OP_SHR: case OP_SHLI: case OP_SHRI: case OP_UNM:
                                                case OP_BNOT: case OP_NOT: case OP_LEN: case OP_CONCAT:
                                                case OP_CALL: case OP_TAILCALL: case OP_NEWCLASS:
                                                case OP_NEWOBJ: case OP_CLOSURE: case OP_SELF:
                                                case OP_ADDK: case OP_SUBK: case OP_MULK: case OP_MODK:
                                                case OP_POWK: case OP_DIVK: case OP_IDIVK: case OP_BANDK:
                                                case OP_BORK: case OP_BXORK: case OP_VARARG:
                                                case OP_CASE: case OP_NEWCONCEPT: case OP_NEWNAMESPACE:
                                                case OP_NEWSUPER: case OP_GETCMDS: case OP_GETOPS:
                                                case OP_ASYNCWRAP: case OP_AWAIT: case OP_GENERICWRAP:
                                                case OP_MERGE: case OP_REGEX:
                                                    writes_src = (GETARG_A(si3) == src_reg); break;
                                                default: break;
                                            }
                                            if (writes_src && sop3 != OP_CLOSURE) break;
                                        }
                                        break;
                                    }
                                }
                                if (!found_chain && upval_idx == 0) {
                                    node->self_rec = 1;
                                    JIT_DBG(MOD_TR, "OP_CALL pc=%d: detected self-recursion (upval[0] self, SETUPVAL in outer scope)", pc);
                                }
                            } else if (sop == OP_GETTABUP) {
                                /*
                                 * GETTABUP 从 _ENV["funcname"] 加载全局函数.
                                 * 编译时无法100%确定是否为自递归 (函数名未存储在 Proto 中),
                                 * 依赖运行时检查 (codegen 中的 SLJIT_MOV_U8 类型比较).
                                 * 标记为 self_rec=0, 让 codegen 生成运行时 Proto 比较.
                                 */
                                int upval_idx = GETARG_B(si);
                                int const_idx = GETARG_C(si);
                                JIT_DBG(MOD_TR, "OP_CALL pc=%d: func loaded via GETTABUP upval=%d const=%d, "
                                    "defer to runtime self-rec check", pc, upval_idx, const_idx);
                            }
                            break;
                        }
                    }
                    fprintf(stderr, "[JIT-TR] OP_CALL pc=%d: self_rec=%d, func_reg=%d, checking implicit\n",
                        pc, node->self_rec, func_reg);
                    if (!node->self_rec && func_reg == 0) {
                        node->self_rec = 1;
                        fprintf(stderr, "[JIT-TR] OP_CALL pc=%d: set self_rec=1 (R0 implicit)\n", pc);
                        JIT_DBG(MOD_TR, "OP_CALL pc=%d: detected self-recursion (R0 implicit, no explicit write)", pc);
                    }
                    fprintf(stderr, "[JIT-TR] OP_CALL pc=%d: final self_rec=%d\n", pc, node->self_rec);

                    /*
                     * 自递归类型特化: 若翻译阶段已确认 self_rec=1,
                     * 将返回值寄存器类型标记为 JIT_TYPE_INT,
                     * 使后续算术操作走 INT_FASTPATH 而非 GUARDED_INT_FASTPATH.
                     * 对 fib 等递归密集场景, 省去每次 ADD 的运行时类型守卫.
                     */
                    if (node->self_rec && ainfo && ainfo->reg_types) {
                        int base_reg = GETARG_A(i);
                        int nret = GETARG_C(i) - 1;
                        if (nret < 1) nret = 1;
                        for (int rr = base_reg; rr < base_reg + nret && rr < ainfo->max_regs; rr++) {
                            ainfo->reg_types[rr] = JIT_TYPE_INT;
                        }
                        JIT_DBG(MOD_TR, "OP_CALL pc=%d: self_rec type specialization, "
                            "R[%d..%d] = INT", pc, base_reg, base_reg + nret - 1);
                        fprintf(stderr, "[JIT-TR] OP_CALL pc=%d: self_rec type specialization, "
                            "R[%d..%d] = INT\n",
                            pc, base_reg, base_reg + nret - 1);

                        /*
                         * 参数类型推断: 自递归函数的参数(n)来自递归调用,
                         * 已知为整数类型. 将 R0 标记为 INT 使 n-1/n-2 等
                         * 算术操作走 INT_FASTPATH 而非 GUARDED_FASTPATH.
                         */
                        if (ainfo->max_regs > 0 && ainfo->reg_types[0] != JIT_TYPE_INT) {
                            ainfo->reg_types[0] = JIT_TYPE_INT;
                            JIT_DBG(MOD_TR, "self_rec param type: R[0] = INT");
                            fprintf(stderr, "[JIT-TR] self_rec param type: R[0] = INT\n");
                        }
                    }
                    node->flags = node->self_rec ? JIT_TYPE_INT : JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_RETURN:
                case OP_RETURN0:
                case OP_RETURN1: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_RET, pc);
                    if (op == OP_RETURN) {
                        node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_A(i);
                        node->src2.type = IR_VAL_INT; node->src2.v.i = GETARG_B(i) - 1;
                    } else if (op == OP_RETURN0) {
                        node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_A(i);
                        node->src2.type = IR_VAL_INT; node->src2.v.i = 0;
                    } else if (op == OP_RETURN1) {
                        node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_A(i);
                        node->src2.type = IR_VAL_INT; node->src2.v.i = 1;
                    }
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_ADDK: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_ADD, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                    node->src2.type = IR_VAL_CONST; node->src2.v.k = GETARG_C(i);
                    node->flags = JIT_TYPE_NUM;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_ADDMETHOD: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_ADDMETHOD, pc);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_ASYNCWRAP: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_ASYNCWRAP, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    JIT_DBG(MOD_TR, "ASYNCWRAP: A=%d, B=%d", GETARG_A(i), GETARG_B(i));
                    break;
                }
                case OP_BANDK: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_BAND, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                    node->src2.type = IR_VAL_CONST; node->src2.v.k = GETARG_C(i);
                    node->flags = JIT_TYPE_INT;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_BORK: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_BOR, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                    node->src2.type = IR_VAL_CONST; node->src2.v.k = GETARG_C(i);
                    node->flags = JIT_TYPE_INT;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_BXORK: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_BXOR, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                    node->src2.type = IR_VAL_CONST; node->src2.v.k = GETARG_C(i);
                    node->flags = JIT_TYPE_INT;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_CASE: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_CASE, pc);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_CHECKTYPE: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_CHECKTYPE, pc);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_CLOSE: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_CLOSE, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->flags = state[GETARG_A(i)];
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_DIVK: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_DIV, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                    node->src2.type = IR_VAL_CONST; node->src2.v.k = GETARG_C(i);
                    node->flags = JIT_TYPE_NUM;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_EQK: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_EQK, pc);
                    node->dest.type = IR_VAL_INT; node->dest.v.i = GETARG_k(i);
                    node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_A(i);
                    node->src2.type = IR_VAL_CONST; node->src2.v.k = GETARG_B(i);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_ERRNNIL: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_ERRNNIL, pc);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_EXTRAARG: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_EXTRAARG, pc);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_GENERICWRAP: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_GENERICWRAP, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    JIT_DBG(MOD_TR, "GENERICWRAP: A=%d, B=%d", GETARG_A(i), GETARG_B(i));
                    break;
                }
                case OP_GETCMDS: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_GETCMDS, pc);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_GETOPS: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_GETOPS, pc);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_GETPROP: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_GETPROP, pc);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_GETSUPER: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_GETSUPER, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                    node->src2.type = IR_VAL_INT; node->src2.v.i = GETARG_C(i);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_GETVARG: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_GETVARG, pc);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_IDIVK: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_IDIV, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                    node->src2.type = IR_VAL_CONST; node->src2.v.k = GETARG_C(i);
                    node->flags = JIT_TYPE_INT;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_IMPLEMENT: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_IMPLEMENT, pc);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_IN: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_IN, pc);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_INHERIT: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_INHERIT, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_INSTANCEOF: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_INSTANCEOF, pc);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_IS: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_IS, pc);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_LEN: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_LEN, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                    node->flags = JIT_TYPE_INT;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_LINKNAMESPACE: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_LINKNAMESPACE, pc);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_LOADKX: {
                    int kx = (pc + 1 < proto->sizecode) ? GETARG_Ax(proto->code[pc + 1]) : 0;
                    ljit_ir_node_t *node = ljit_ir_new(IR_LOADKX, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_CONST; node->src1.v.k = kx;
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_MODK: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_MOD, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                    node->src2.type = IR_VAL_CONST; node->src2.v.k = GETARG_C(i);
                    node->flags = JIT_TYPE_NUM;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_MULK: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_MUL, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                    node->src2.type = IR_VAL_CONST; node->src2.v.k = GETARG_C(i);
                    node->flags = JIT_TYPE_NUM;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_NEWCONCEPT: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_NEWCONCEPT, pc);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_NEWNAMESPACE: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_NEWNAMESPACE, pc);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_NEWSUPER: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_NEWSUPER, pc);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_NOP: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_NOP, pc);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_CUSTOM: {
                    /* 自定义操作码扩展：运行时分发到自定义处理器
                     * 当前JIT不支持自定义操作码，生成回退NOP */
                    JIT_DBG(MOD_TR, "OP_CUSTOM: pc=%d Ax=%d, generating fallback NOP",
                        pc, GETARG_Ax(i));
                    ljit_ir_node_t *node = ljit_ir_new(IR_NOP, pc);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_POWK: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_POW, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                    node->src2.type = IR_VAL_CONST; node->src2.v.k = GETARG_C(i);
                    node->flags = JIT_TYPE_NUM;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_SELF: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_SELF, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                    node->src2.type = IR_VAL_CONST; node->src2.v.k = GETARG_C(i);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_SETIFACEFLAG: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_SETIFACEFLAG, pc);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_SETTRAITFLAG: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_SETTRAITFLAG, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    JIT_DBG(MOD_TR, "SETTRAITFLAG: A=%d", GETARG_A(i));
                    break;
                }
                case OP_SETTRAITREQUIRE: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_SETTRAITREQUIRE, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_CONST; node->src1.v.k = GETARG_B(i);
                    node->src2.type = IR_VAL_INT; node->src2.v.i = GETARG_C(i);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    JIT_DBG(MOD_TR, "SETTRAITREQUIRE: A=%d, B=%d, C=%d", GETARG_A(i), GETARG_B(i), GETARG_C(i));
                    break;
                }
                case OP_USETRAIT: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_USETRAIT, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    JIT_DBG(MOD_TR, "USETRAIT: A=%d, B=%d", GETARG_A(i), GETARG_B(i));
                    break;
                }
                case OP_AWAIT: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_AWAIT, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    JIT_DBG(MOD_TR, "AWAIT: A=%d, B=%d", GETARG_A(i), GETARG_B(i));
                    break;
                }
                case OP_MERGE: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_MERGE, pc);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_REGEX: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_REGEX, pc);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_SETLIST: {
                    int last = GETARG_vC(i);
                    if (TESTARG_k(i)) {
                        ljit_ir_node_t *node = ljit_ir_new(IR_SETLIST, pc);
                        node->src1.type = IR_VAL_INT; node->src1.v.i = -1;
                        node->flags = JIT_TYPE_ANY;
                        ljit_ir_append(ctx, node);
                    } else {
                        ljit_ir_node_t *node = ljit_ir_new(IR_SETLIST, pc);
                        node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                        node->src1.type = IR_VAL_INT; node->src1.v.i = GETARG_vB(i);
                        node->src2.type = IR_VAL_INT; node->src2.v.i = last;
                        node->flags = state[GETARG_A(i)];
                        ljit_ir_append(ctx, node);
                    }
                    break;
                }
                case OP_SETMETHOD: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_SETMETHOD, pc);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_SETPROP: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_SETPROP, pc);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_SETSTATIC: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_SETSTATIC, pc);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_SETSUPER: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_SETSUPER, pc);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_SLICE: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_SLICE, pc);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_SPACESHIP: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_SPACESHIP, pc);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_SUBK: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_SUB, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                    node->src2.type = IR_VAL_CONST; node->src2.v.k = GETARG_C(i);
                    node->flags = JIT_TYPE_NUM;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_TBC: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_TBC, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->flags = state[GETARG_A(i)];
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_TEST: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_TEST, pc);
                    node->dest.type = IR_VAL_INT; node->dest.v.i = GETARG_k(i);
                    node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_A(i);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_TESTNIL: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_TESTNIL, pc);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_TESTSET: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_TESTSET, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                    node->src2.type = IR_VAL_INT; node->src2.v.i = GETARG_k(i);
                    node->flags = JIT_TYPE_BOOL;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_TFORPREP: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_TFORPREP, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_INT; node->src1.v.i = GETARG_Bx(i);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_GETUPVAL: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_GETUPVAL, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_INT; node->src1.v.i = GETARG_B(i);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_SETUPVAL: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_SETUPVAL, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_INT; node->src1.v.i = GETARG_B(i);
                    node->flags = state[GETARG_A(i)];
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_GETTABUP: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_GETTABUP, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_INT; node->src1.v.i = GETARG_B(i);
                    int c = GETARG_C(i);
                    node->src2.type = IR_VAL_CONST; node->src2.v.k = c;
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_SETTABUP: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_SETTABUP, pc);
                    node->dest.type = IR_VAL_INT; node->dest.v.i = GETARG_A(i);
                    int b = GETARG_B(i);
                    node->src1.type = IR_VAL_CONST; node->src1.v.k = b;
                    int c = GETARG_C(i);
                    if (TESTARG_k(i)) {
                        node->src2.type = IR_VAL_CONST; node->src2.v.k = c;
                    } else {
                        node->src2.type = IR_VAL_REG; node->src2.v.reg = c;
                    }
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_GETI: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_GETI, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                    node->src2.type = IR_VAL_INT; node->src2.v.i = GETARG_C(i);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_SETI: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_SETI, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_INT; node->src1.v.i = GETARG_B(i);
                    if (TESTARG_k(i)) {
                        node->src2.type = IR_VAL_CONST; node->src2.v.k = GETARG_C(i);
                    } else {
                        node->src2.type = IR_VAL_REG; node->src2.v.reg = GETARG_C(i);
                    }
                    node->flags = state[GETARG_A(i)];
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_GETFIELD: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_GETFIELD, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                    node->src2.type = IR_VAL_CONST; node->src2.v.k = GETARG_C(i);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
                case OP_SETFIELD: {
                    ljit_ir_node_t *node = ljit_ir_new(IR_SETFIELD, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_CONST; node->src1.v.k = GETARG_B(i);
                    if (TESTARG_k(i)) {
                        node->src2.type = IR_VAL_CONST; node->src2.v.k = GETARG_C(i);
                    } else {
                        node->src2.type = IR_VAL_REG; node->src2.v.reg = GETARG_C(i);
                    }
                    node->flags = state[GETARG_A(i)];
                    ljit_ir_append(ctx, node);
                    break;
                }
                default: {
                    /* 未支持的操作码：生成 IR_NOP 占位，同时记录调试日志
                     * 运行时若遇到未内联支持的IR，codegen 会触发解释器回退 (ljit_icall_fallback) */
                    JIT_DBG(MOD_TR, "UNHANDLED opcode: pc=%d op=%d A=%d, generating fallback NOP",
                        pc, op, GETARG_A(i));
                    ljit_ir_node_t *node = ljit_ir_new(IR_NOP, pc);
                    node->flags = JIT_TYPE_ANY;
                    ljit_ir_append(ctx, node);
                    break;
                }
            }

            /* 更新state：执行完当前指令后，更新定义的寄存器类型 */
            update_state_after_ins(proto, i, state, max_regs);
        }
    }

    free(state);
}
