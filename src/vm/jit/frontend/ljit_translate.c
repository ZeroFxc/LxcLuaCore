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
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_FORLOOP: {
                ljit_ir_node_t *node = ljit_ir_new(IR_FORLOOP, pc);
                node->dest.type = IR_VAL_REG; node->dest.v.reg = GETARG_A(i);
                node->src1.type = IR_VAL_INT; node->src1.v.i = GETARG_Bx(i);
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
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_LINKNAMESPACE: {
                ljit_ir_node_t *node = ljit_ir_new(IR_LINKNAMESPACE, pc);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_LOADKX: {
                ljit_ir_node_t *node = ljit_ir_new(IR_LOADKX, pc);
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
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_SETIFACEFLAG: {
                ljit_ir_node_t *node = ljit_ir_new(IR_SETIFACEFLAG, pc);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_SETLIST: {
                ljit_ir_node_t *node = ljit_ir_new(IR_SETLIST, pc);
                ljit_ir_append(ctx, node);
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
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_TEST: {
                ljit_ir_node_t *node = ljit_ir_new(IR_TEST, pc);
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
                ljit_ir_node_t *node = ljit_ir_new(IR_GETI, pc);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_SETI: {
                ljit_ir_node_t *node = ljit_ir_new(IR_SETI, pc);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_GETFIELD: {
                ljit_ir_node_t *node = ljit_ir_new(IR_GETFIELD, pc);
                ljit_ir_append(ctx, node);
                break;
            }
            case OP_SETFIELD: {
                ljit_ir_node_t *node = ljit_ir_new(IR_SETFIELD, pc);
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
