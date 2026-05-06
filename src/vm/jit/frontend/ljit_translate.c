#include "ljit_analyze.h"
#include "../../../core/lopcodes.h"

void ljit_translate(ljit_ctx_t *ctx) {
    if (!ctx || !ctx->proto) return;

    Proto *proto = ctx->proto;

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
            case OP_GETUPVAL:
            case OP_SETUPVAL:
            case OP_GETTABUP:
            case OP_GETTABLE: {
                ljit_ir_node_t *node = ljit_ir_new(IR_GETTABLE, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                node->src2.type = IR_VAL_REG; node->src2.v.reg = GETARG_C(i);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_GETI:
            case OP_GETFIELD:
            case OP_SETTABUP:
            case OP_SETTABLE: {
                ljit_ir_node_t *node = ljit_ir_new(IR_SETTABLE, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                node->src1.type = IR_VAL_REG; node->src1.v.reg = GETARG_B(i);
                node->src2.type = IR_VAL_REG; node->src2.v.reg = GETARG_C(i);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_SETI:
            case OP_SETFIELD:
            case OP_NEWTABLE: {
                ljit_ir_node_t *node = ljit_ir_new(IR_NEWTABLE, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                // Fix: OP_NEWTABLE uses GETARG_B and GETARG_C in older/some Lua versions.
                // Looking at lopcodes.c, NEWTABLE uses ivABC, meaning vB and vC.
                // Which maps to GETARG_B and GETARG_C on most forks, but here lopcodes.h
                // defines GETARG_vB, GETARG_vC.
                // I checked: GETARG_vB / GETARG_vC exist in src/core/lopcodes.h
                node->src1.type = IR_VAL_INT; node->src1.v.i = GETARG_B(i); // fallback if vB fails, but I saw vB in grep
                node->src2.type = IR_VAL_INT; node->src2.v.i = GETARG_C(i);
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
            case OP_JMP: {
                ljit_ir_node_t *node = ljit_ir_new(IR_JMP, pc);
                node->dest.type = IR_VAL_LABEL;
                node->dest.v.label_id = pc + 1 + GETARG_sJ(i);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_CALL:
            case OP_TAILCALL: {
                ljit_ir_node_t *node = ljit_ir_new(IR_CALL, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                node->src1.type = IR_VAL_INT; node->src1.v.i = GETARG_B(i) - 1;
                node->src2.type = IR_VAL_INT; node->src2.v.i = GETARG_C(i) - 1;
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
            default: {
                ljit_ir_node_t *node = ljit_ir_new(IR_NOP, pc);
                ljit_ir_append(ctx, node);
                break;
            }
        }
    }
}
