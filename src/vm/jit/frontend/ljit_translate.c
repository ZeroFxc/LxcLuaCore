#include <stdio.h>
#include "ljit_analyze.h"
#include "../core/ljit_debug.h"
#include "../../../core/lopcodes.h"

void ljit_translate(ljit_ctx_t *ctx) {
    if (!ctx || !ctx->proto) return;

    Proto *proto = ctx->proto;

    JIT_DBG(MOD_TR, "translate sizecode=%d, maxstacksize=%d", proto->sizecode, proto->maxstacksize);
    for (int pc = 0; pc < proto->sizecode; pc++) {
        Instruction i = proto->code[pc];
        OpCode op = GET_OPCODE(i);
        JIT_DBG(MOD_TR, "  pc=%d op=%d A=%d Bx=%d", pc, op, GETARG_A(i), GETARG_Bx(i));
    }

    for (int pc = 0; pc < proto->sizecode; pc++) {
        Instruction i = proto->code[pc];
        OpCode op = GET_OPCODE(i);

        switch (op) {
            case OP_MOVE: {
                ljit_ir_node_t *node = ljit_ir_new(IR_MOV, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_LOADI: {
                ljit_ir_node_t *node = ljit_ir_new(IR_LOADI, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                node->src1.type = IR_VAL_INT; node->src1.v.i = GETARG_sBx(i);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_LOADF: {
                ljit_ir_node_t *node = ljit_ir_new(IR_LOADF, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                node->src1.type = IR_VAL_NUM; node->src1.v.n = (lua_Number)GETARG_sBx(i);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_LOADK: {
                ljit_ir_node_t *node = ljit_ir_new(IR_LOADK, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                node->src1.type = IR_VAL_CONST; node->src1.v.k = GETARG_Bx(i);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_LOADFALSE:
            case OP_LOADTRUE: {
                ljit_ir_node_t *node = ljit_ir_new(IR_LOADBOOL, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                node->src1.type = IR_VAL_INT; node->src1.v.i = (op == OP_LOADTRUE) ? 1 : 0;
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_LFALSESKIP: {
                ljit_ir_node_t *node = ljit_ir_new(IR_LOADBOOL, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                node->src1.type = IR_VAL_INT; node->src1.v.i = 0;
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
                    ljit_ir_append(ctx, node);
                }
                break;
            }
            case OP_GETTABLE: {
                ljit_ir_node_t *node = ljit_ir_new(IR_GETTABLE, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                node->src2.type = IR_VAL_REG; node->src2.v.reg = GETARG_C(i);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_SETTABLE: {
                ljit_ir_node_t *node = ljit_ir_new(IR_SETTABLE, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                node->src2.type = IR_VAL_REG; node->src2.v.reg = GETARG_C(i);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_NEWTABLE: {
                /* R[A] := {} (ivABC format: uses GETARG_vB, GETARG_vC) */
                ljit_ir_node_t *node = ljit_ir_new(IR_NEWTABLE, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                node->src1.type = IR_VAL_INT; node->src1.v.i = GETARG_vB(i);
                node->src2.type = IR_VAL_INT; node->src2.v.i = GETARG_vC(i);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_NEWMAP: {
                /* R[A] := [] (iABC format, B和C未使用) */
                ljit_ir_node_t *node = ljit_ir_new(IR_NEWMAP, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_MAPGET: {
                /* R[A] := R[B][R[C]] (iABC format) */
                ljit_ir_node_t *node = ljit_ir_new(IR_GETMAP, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                node->src2.type = IR_VAL_REG; node->src2.v.reg = GETARG_C(i);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_MAPSET: {
                /* R[A][R[B]] := R[C] (iABC format) */
                ljit_ir_node_t *node = ljit_ir_new(IR_SETMAP, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                node->src2.type = IR_VAL_REG; node->src2.v.reg = GETARG_C(i);
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
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_ADDI: {
                ljit_ir_node_t *node = ljit_ir_new(IR_ADD, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                node->src2.type = IR_VAL_INT; node->src2.v.i = GETARG_sC(i);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_SHLI: {
                ljit_ir_node_t *node = ljit_ir_new(IR_SHL, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                node->src1.type = IR_VAL_INT; node->src1.v.i = GETARG_sC(i);
                node->src2.type = IR_VAL_REG; node->src2.v.reg = GETARG_B(i);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_SHRI: {
                ljit_ir_node_t *node = ljit_ir_new(IR_SHR, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                /* For OP_SHRI, setivalue(s2v(ra), luaV_shiftl(ib, -ic))
                   So value to shift is ib (src1), shift amount is -ic (src2) */
                node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                node->src2.type = IR_VAL_INT; node->src2.v.i = GETARG_sC(i);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_LT:
            case OP_LE:
            case OP_EQ: {
                ljit_ir_op_t ir_op = (op == OP_LT) ? IR_CMP_LT : ((op == OP_LE) ? IR_CMP_LE : IR_CMP_EQ);
                ljit_ir_node_t *node = ljit_ir_new(ir_op, pc);
                node->dest.type = IR_VAL_INT; node->dest.v.i = GETARG_k(i); // k bit
                node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_A(i);
                node->src2.type = IR_VAL_REG; node->src2.v.reg = GETARG_B(i);
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
                node->dest.type = IR_VAL_INT; node->dest.v.i = GETARG_k(i); // k bit
                node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_A(i);
                node->src2.type = IR_VAL_INT; node->src2.v.i = GETARG_sB(i);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_JMP: {
                ljit_ir_node_t *node = ljit_ir_new(IR_JMP, pc);
                node->dest.type = IR_VAL_LABEL;
                node->dest.v.label_id = pc + 1 + GETARG_sJ(i);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_MMBINI:
            case OP_MMBINK:
            case OP_MMBIN: {
                /* Ignore metamethod fallbacks for JIT MVP */
                break;
            }
            case OP_CONCAT: {
                ljit_ir_node_t *node = ljit_ir_new(IR_CONCAT, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                node->src1.type = IR_VAL_INT; node->src1.v.i = GETARG_B(i);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_TFORCALL: {
                ljit_ir_node_t *node = ljit_ir_new(IR_TFORCALL, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                node->src1.type = IR_VAL_INT; node->src1.v.i = GETARG_C(i);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_TFORLOOP: {
                ljit_ir_node_t *node = ljit_ir_new(IR_TFORLOOP, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                node->src1.type = IR_VAL_INT; node->src1.v.i = GETARG_Bx(i);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_FORPREP: {
                ljit_ir_node_t *node = ljit_ir_new(IR_FORPREP, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                node->src1.type = IR_VAL_INT; node->src1.v.i = GETARG_Bx(i);
                JIT_DBG(MOD_TR, "FORPREP pc=%d A=%d Bx=%d", pc, GETARG_A(i), GETARG_Bx(i));
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_FORLOOP: {
                ljit_ir_node_t *node = ljit_ir_new(IR_FORLOOP, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                node->src1.type = IR_VAL_INT; node->src1.v.i = GETARG_Bx(i);
                JIT_DBG(MOD_TR, "FORLOOP pc=%d A=%d Bx=%d", pc, GETARG_A(i), GETARG_Bx(i));
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_VARARG: {
                ljit_ir_node_t *node = ljit_ir_new(IR_VARARG, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                node->src1.type = IR_VAL_INT; node->src1.v.i = GETARG_B(i);
                node->src2.type = IR_VAL_INT; node->src2.v.i = GETARG_C(i);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_VARARGPREP: {
                ljit_ir_node_t *node = ljit_ir_new(IR_VARARGPREP, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                node->src1.type = IR_VAL_INT; node->src1.v.i = GETARG_B(i);
                node->src2.type = IR_VAL_INT; node->src2.v.i = GETARG_C(i);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_NEWCLASS: {
                ljit_ir_node_t *node = ljit_ir_new(IR_NEWCLASS, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                node->src1.type = IR_VAL_INT; node->src1.v.i = GETARG_Bx(i);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_NEWOBJ: {
                ljit_ir_node_t *node = ljit_ir_new(IR_NEWOBJ, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                node->src1.type = IR_VAL_INT; node->src1.v.i = GETARG_B(i);
                node->src2.type = IR_VAL_INT; node->src2.v.i = GETARG_C(i);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_CLOSURE: {
                ljit_ir_node_t *node = ljit_ir_new(IR_CLOSURE, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                node->src1.type = IR_VAL_INT; node->src1.v.i = GETARG_Bx(i);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_CALL:
            case OP_TAILCALL: {
                ljit_ir_node_t *node = ljit_ir_new(IR_CALL, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                node->src1.type = IR_VAL_INT; node->src1.v.i = GETARG_B(i) - 1;
                node->src2.type = IR_VAL_INT; node->src2.v.i = GETARG_C(i) - 1;

                /*
                 * 自递归检测: 扫描当前pc之前的字节码, 查找最后写入目标寄存器A的指令.
                 * 若为OP_CLOSURE且加载的Proto与当前函数相同, 则标记为自递归调用.
                 * 这使codegen可以跳过运行时Proto比较, 直接使用ljit_jitcall_self快速路径.
                 */
                int func_reg = GETARG_A(i);
                for (int scan = pc - 1; scan >= 0; scan--) {
                    Instruction si = proto->code[scan];
                    OpCode sop = GET_OPCODE(si);
                    int sa = GETARG_A(si);
                    /* 检查该指令是否写入func_reg */
                    int writes_reg = 0;
                    switch (sop) {
                        case OP_MOVE: case OP_LOADI: case OP_LOADF: case OP_LOADK:
                        case OP_LOADKX: case OP_LOADFALSE: case OP_LOADTRUE:
                        case OP_LOADNIL: case OP_GETUPVAL: case OP_GETTABUP:
                        case OP_GETTABLE: case OP_GETI: case OP_GETFIELD:
                        case OP_NEWTABLE: case OP_ADD: case OP_SUB: case OP_MUL:
                        case OP_MOD: case OP_POW: case OP_DIV: case OP_IDIV:
                        case OP_BAND: case OP_BOR: case OP_BXOR: case OP_SHL:
                        case OP_SHR: case OP_SHLI: case OP_SHRI: case OP_UNM:
                        case OP_BNOT: case OP_NOT: case OP_LEN: case OP_CONCAT:
                        case OP_CALL: case OP_TAILCALL: case OP_NEWCLASS:
                        case OP_NEWOBJ: case OP_CLOSURE: case OP_SELF:
                        case OP_ADDK: case OP_SUBK: case OP_MULK: case OP_MODK:
                        case OP_POWK: case OP_DIVK: case OP_IDIVK: case OP_BANDK:
                        case OP_BORK: case OP_BXORK: case OP_VARARG:
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
                            /*
                             * OP_GETUPVAL 路径: 局部递归函数通过 upvalue 访问自身.
                             * 追踪 OP_GETUPVAL -> OP_SETUPVAL -> OP_CLOSURE 链,
                             * 确认 upvalue 中存储的是当前函数自身的闭包.
                             * 典型场景: local function f(n) ... return f(n-1) + f(n-2) end
                             *
                             * 注意: 若 SETUPVAL 在外层函数(如 main chunk)中,
                             * 当前函数内无法追踪到, 此时通过 upval_idx==0 做启发式判断.
                             */
                            int upval_idx = GETARG_B(si);
                            int found_chain = 0;
                            for (int scan2 = scan - 1; scan2 >= 0; scan2--) {
                                Instruction si2 = proto->code[scan2];
                                if (GET_OPCODE(si2) == OP_SETUPVAL && GETARG_B(si2) == upval_idx) {
                                    int src_reg = GETARG_A(si2);
                                    found_chain = 1;
                                    /* 追踪 SETUPVAL 的源寄存器, 查找对应的 OP_CLOSURE */
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
                                        /* 若遇到其他写入 src_reg 的指令, 停止追踪 */
                                        int writes_src = 0;
                                        switch (sop3) {
                                            case OP_MOVE: case OP_LOADI: case OP_LOADF: case OP_LOADK:
                                            case OP_LOADKX: case OP_LOADFALSE: case OP_LOADTRUE:
                                            case OP_LOADNIL: case OP_GETUPVAL: case OP_GETTABUP:
                                            case OP_GETTABLE: case OP_GETI: case OP_GETFIELD:
                                            case OP_NEWTABLE: case OP_ADD: case OP_SUB: case OP_MUL:
                                            case OP_MOD: case OP_POW: case OP_DIV: case OP_IDIV:
                                            case OP_BAND: case OP_BOR: case OP_BXOR: case OP_SHL:
                                            case OP_SHR: case OP_SHLI: case OP_SHRI: case OP_UNM:
                                            case OP_BNOT: case OP_NOT: case OP_LEN: case OP_CONCAT:
                                            case OP_CALL: case OP_TAILCALL: case OP_NEWCLASS:
                                            case OP_NEWOBJ: case OP_CLOSURE: case OP_SELF:
                                            case OP_ADDK: case OP_SUBK: case OP_MULK: case OP_MODK:
                                            case OP_POWK: case OP_DIVK: case OP_IDIVK: case OP_BANDK:
                                            case OP_BORK: case OP_BXORK: case OP_VARARG:
                                                writes_src = (GETARG_A(si3) == src_reg); break;
                                            default: break;
                                        }
                                        if (writes_src && sop3 != OP_CLOSURE) break;
                                    }
                                    break;
                                }
                            }
                            /*
                             * 若 SETUPVAL 链在当前函数内未找到,
                             * 但 upval_idx == 0(第一个upvalue通常是递归函数自身),
                             * 则标记为自递归调用.
                             */
                            if (!found_chain && upval_idx == 0) {
                                node->self_rec = 1;
                                JIT_DBG(MOD_TR, "OP_CALL pc=%d: detected self-recursion (upval[0] self, SETUPVAL in outer scope)", pc);
                            }
                        }
                        break; /* 找到最后写入指令, 停止扫描 */
                    }
                }
                /*
                 * 若扫描到函数开头仍未找到写入 func_reg 的指令,
                 * 且 func_reg == 0, 则说明函数通过 R0 隐式传入
                 * (Lua 调用约定: R0 始终是当前被调用的函数自身).
                 * 这是典型的自递归调用模式.
                 */
                if (!node->self_rec && func_reg == 0) {
                    node->self_rec = 1;
                    JIT_DBG(MOD_TR, "OP_CALL pc=%d: detected self-recursion (R0 implicit, no explicit write)", pc);
                }

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
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_ADDK: {
                ljit_ir_node_t *node = ljit_ir_new(IR_ADD, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                node->src2.type = IR_VAL_CONST; node->src2.v.k = GETARG_C(i);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_ADDMETHOD: {
                ljit_ir_node_t *node = ljit_ir_new(IR_ADDMETHOD, pc);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_ASYNCWRAP: {
                ljit_ir_node_t *node = ljit_ir_new(IR_ASYNCWRAP, pc);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_BANDK: {
                ljit_ir_node_t *node = ljit_ir_new(IR_BAND, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                node->src2.type = IR_VAL_CONST; node->src2.v.k = GETARG_C(i);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_BORK: {
                ljit_ir_node_t *node = ljit_ir_new(IR_BOR, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                node->src2.type = IR_VAL_CONST; node->src2.v.k = GETARG_C(i);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_BXORK: {
                ljit_ir_node_t *node = ljit_ir_new(IR_BXOR, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                node->src2.type = IR_VAL_CONST; node->src2.v.k = GETARG_C(i);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_CASE: {
                ljit_ir_node_t *node = ljit_ir_new(IR_CASE, pc);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_CHECKTYPE: {
                ljit_ir_node_t *node = ljit_ir_new(IR_CHECKTYPE, pc);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_CLOSE: {
                ljit_ir_node_t *node = ljit_ir_new(IR_CLOSE, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_DIVK: {
                ljit_ir_node_t *node = ljit_ir_new(IR_DIV, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                node->src2.type = IR_VAL_CONST; node->src2.v.k = GETARG_C(i);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_EQK: {
                ljit_ir_node_t *node = ljit_ir_new(IR_EQK, pc);
                node->dest.type = IR_VAL_INT; node->dest.v.i = GETARG_k(i);
                node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_A(i);
                node->src2.type = IR_VAL_CONST; node->src2.v.k = GETARG_B(i);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_ERRNNIL: {
                ljit_ir_node_t *node = ljit_ir_new(IR_ERRNNIL, pc);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_EXTRAARG: {
                ljit_ir_node_t *node = ljit_ir_new(IR_EXTRAARG, pc);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_GENERICWRAP: {
                ljit_ir_node_t *node = ljit_ir_new(IR_GENERICWRAP, pc);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_GETCMDS: {
                ljit_ir_node_t *node = ljit_ir_new(IR_GETCMDS, pc);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_GETOPS: {
                ljit_ir_node_t *node = ljit_ir_new(IR_GETOPS, pc);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_GETPROP: {
                ljit_ir_node_t *node = ljit_ir_new(IR_GETPROP, pc);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_GETSUPER: {
                ljit_ir_node_t *node = ljit_ir_new(IR_GETSUPER, pc);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_GETVARG: {
                ljit_ir_node_t *node = ljit_ir_new(IR_GETVARG, pc);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_IDIVK: {
                ljit_ir_node_t *node = ljit_ir_new(IR_IDIV, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                node->src2.type = IR_VAL_CONST; node->src2.v.k = GETARG_C(i);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_IMPLEMENT: {
                ljit_ir_node_t *node = ljit_ir_new(IR_IMPLEMENT, pc);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_IN: {
                ljit_ir_node_t *node = ljit_ir_new(IR_IN, pc);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_INHERIT: {
                ljit_ir_node_t *node = ljit_ir_new(IR_INHERIT, pc);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_INSTANCEOF: {
                ljit_ir_node_t *node = ljit_ir_new(IR_INSTANCEOF, pc);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_IS: {
                ljit_ir_node_t *node = ljit_ir_new(IR_IS, pc);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_LEN: {
                ljit_ir_node_t *node = ljit_ir_new(IR_LEN, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_LINKNAMESPACE: {
                ljit_ir_node_t *node = ljit_ir_new(IR_LINKNAMESPACE, pc);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_LOADKX: {
                int kx = (pc + 1 < proto->sizecode) ? GETARG_Ax(proto->code[pc + 1]) : 0;
                ljit_ir_node_t *node = ljit_ir_new(IR_LOADKX, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                node->src1.type = IR_VAL_CONST; node->src1.v.k = kx;
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_MODK: {
                ljit_ir_node_t *node = ljit_ir_new(IR_MOD, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                node->src2.type = IR_VAL_CONST; node->src2.v.k = GETARG_C(i);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_MULK: {
                ljit_ir_node_t *node = ljit_ir_new(IR_MUL, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                node->src2.type = IR_VAL_CONST; node->src2.v.k = GETARG_C(i);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_NEWCONCEPT: {
                ljit_ir_node_t *node = ljit_ir_new(IR_NEWCONCEPT, pc);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_NEWNAMESPACE: {
                ljit_ir_node_t *node = ljit_ir_new(IR_NEWNAMESPACE, pc);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_NEWSUPER: {
                ljit_ir_node_t *node = ljit_ir_new(IR_NEWSUPER, pc);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_NOP: {
                ljit_ir_node_t *node = ljit_ir_new(IR_NOP, pc);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_POWK: {
                ljit_ir_node_t *node = ljit_ir_new(IR_POW, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                node->src2.type = IR_VAL_CONST; node->src2.v.k = GETARG_C(i);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_SELF: {
                ljit_ir_node_t *node = ljit_ir_new(IR_SELF, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                node->src2.type = IR_VAL_CONST; node->src2.v.k = GETARG_C(i);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_SETIFACEFLAG: {
                ljit_ir_node_t *node = ljit_ir_new(IR_SETIFACEFLAG, pc);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_SETTRAITFLAG: {
                /* R[A] 设置为 Trait 标志 */
                ljit_ir_node_t *node = ljit_ir_new(IR_SETTRAITFLAG, pc);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_SETTRAITREQUIRE: {
                /* R[A].__trait_requires[K[B]] := C */
                ljit_ir_node_t *node = ljit_ir_new(IR_SETTRAITREQUIRE, pc);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_USETRAIT: {
                /* R[A] use R[B]: 将trait方法复制到类中 */
                ljit_ir_node_t *node = ljit_ir_new(IR_USETRAIT, pc);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_AWAIT: {
                /* R[A] := await(R[B]): 协程异步等待 */
                ljit_ir_node_t *node = ljit_ir_new(IR_AWAIT, pc);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_MERGE: {
                /* R[A] := merge(R[B], R[C]): 表合并 */
                ljit_ir_node_t *node = ljit_ir_new(IR_MERGE, pc);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_REGEX: {
                /* R[A] := regex(K[Bx]): 正则字面量 */
                ljit_ir_node_t *node = ljit_ir_new(IR_REGEX, pc);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_SETLIST: {
                /* R[A][vC+i] := R[A+i], 1 <= i <= vB */
                int last = GETARG_vC(i);
                /* Skip EXTRAARG support for now; interpreter handles it */
                if (TESTARG_k(i)) {
                    ljit_ir_node_t *node = ljit_ir_new(IR_SETLIST, pc);
                    node->src1.type = IR_VAL_INT; node->src1.v.i = -1; /* signal: has EXTRAARG */
                    ljit_ir_append(ctx, node);
                } else {
                    ljit_ir_node_t *node = ljit_ir_new(IR_SETLIST, pc);
                    node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                    node->src1.type = IR_VAL_INT; node->src1.v.i = GETARG_vB(i);
                    node->src2.type = IR_VAL_INT; node->src2.v.i = last;
                    ljit_ir_append(ctx, node);
                }
                break;
            }
            case OP_SETMETHOD: {
                ljit_ir_node_t *node = ljit_ir_new(IR_SETMETHOD, pc);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_SETPROP: {
                ljit_ir_node_t *node = ljit_ir_new(IR_SETPROP, pc);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_SETSTATIC: {
                ljit_ir_node_t *node = ljit_ir_new(IR_SETSTATIC, pc);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_SETSUPER: {
                ljit_ir_node_t *node = ljit_ir_new(IR_SETSUPER, pc);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_SLICE: {
                ljit_ir_node_t *node = ljit_ir_new(IR_SLICE, pc);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_SPACESHIP: {
                ljit_ir_node_t *node = ljit_ir_new(IR_SPACESHIP, pc);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_SUBK: {
                ljit_ir_node_t *node = ljit_ir_new(IR_SUB, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                node->src2.type = IR_VAL_CONST; node->src2.v.k = GETARG_C(i);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_TBC: {
                ljit_ir_node_t *node = ljit_ir_new(IR_TBC, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_TEST: {
                ljit_ir_node_t *node = ljit_ir_new(IR_TEST, pc);
                node->dest.type = IR_VAL_INT; node->dest.v.i = GETARG_k(i);
                node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_A(i);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_TESTNIL: {
                ljit_ir_node_t *node = ljit_ir_new(IR_TESTNIL, pc);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_TESTSET: {
                ljit_ir_node_t *node = ljit_ir_new(IR_TESTSET, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                node->src2.type = IR_VAL_INT; node->src2.v.i = GETARG_k(i);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_TFORPREP: {
                ljit_ir_node_t *node = ljit_ir_new(IR_TFORPREP, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                node->src1.type = IR_VAL_INT; node->src1.v.i = GETARG_Bx(i);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_GETUPVAL: {
                ljit_ir_node_t *node = ljit_ir_new(IR_GETUPVAL, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                node->src1.type = IR_VAL_INT; node->src1.v.i = GETARG_B(i);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_SETUPVAL: {
                ljit_ir_node_t *node = ljit_ir_new(IR_SETUPVAL, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                node->src1.type = IR_VAL_INT; node->src1.v.i = GETARG_B(i);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_GETTABUP: {
                ljit_ir_node_t *node = ljit_ir_new(IR_GETTABUP, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                node->src1.type = IR_VAL_INT; node->src1.v.i = GETARG_B(i);

                int c = GETARG_C(i);
                node->src2.type = IR_VAL_CONST; node->src2.v.k = c;
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
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_GETI: {
                /* R[A] := R[B][C] */
                ljit_ir_node_t *node = ljit_ir_new(IR_GETI, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                node->src2.type = IR_VAL_INT; node->src2.v.i = GETARG_C(i);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_SETI: {
                /* R[A][B] := RK(C) */
                ljit_ir_node_t *node = ljit_ir_new(IR_SETI, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                node->src1.type = IR_VAL_INT; node->src1.v.i = GETARG_B(i);
                if (TESTARG_k(i)) {
                    node->src2.type = IR_VAL_CONST; node->src2.v.k = GETARG_C(i);
                } else {
                    node->src2.type = IR_VAL_REG; node->src2.v.reg = GETARG_C(i);
                }
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_GETFIELD: {
                /* R[A] := R[B][K[C]:shortstring] */
                ljit_ir_node_t *node = ljit_ir_new(IR_GETFIELD, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                node->src2.type = IR_VAL_CONST; node->src2.v.k = GETARG_C(i);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_SETFIELD: {
                /* R[A][K[B]:shortstring] := RK(C) */
                ljit_ir_node_t *node = ljit_ir_new(IR_SETFIELD, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                node->src1.type = IR_VAL_CONST; node->src1.v.k = GETARG_B(i);
                if (TESTARG_k(i)) {
                    node->src2.type = IR_VAL_CONST; node->src2.v.k = GETARG_C(i);
                } else {
                    node->src2.type = IR_VAL_REG; node->src2.v.reg = GETARG_C(i);
                }
                ljit_ir_append(ctx, node);
                break;
            }
            default: {
                ljit_ir_node_t *node = ljit_ir_new(IR_NOP, pc);
                ljit_ir_append(ctx, node);
                break;
            }
        }
    }
}
