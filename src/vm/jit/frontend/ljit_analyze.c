#include "ljit_analyze.h"
#include "../../../core/lopcodes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void ljit_analyze_dataflow(ljit_ctx_t *ctx) {
    if (!ctx || !ctx->proto || !ctx->analyze_info) return;

    ljit_analyze_info_t *info = (ljit_analyze_info_t *)ctx->analyze_info;
    Proto *proto = ctx->proto;

    for (int pc = 0; pc < proto->sizecode; pc++) {
        Instruction i = proto->code[pc];
        OpCode op = GET_OPCODE(i);

        // A basic linear scan to record the definition PC for registers.
        // We look at the 'A' argument as the destination register for most opcodes.
        switch (op) {
            case OP_MOVE:
            case OP_LOADI:
            case OP_LOADF:
            case OP_LOADK:
            case OP_LOADKX:
            case OP_LOADFALSE:
            case OP_LOADTRUE:
            case OP_LOADNIL:
            case OP_GETUPVAL:
            case OP_GETTABUP:
            case OP_GETTABLE:
            case OP_GETI:
            case OP_GETFIELD:
            case OP_NEWTABLE:
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
            case OP_SHR:
            case OP_UNM:
            case OP_BNOT:
            case OP_NOT:
            case OP_LEN:
            case OP_CONCAT:
            case OP_CALL:
            case OP_TAILCALL: {
                int a = GETARG_A(i);
                if (a < info->max_regs) {
                    info->def_pc[a] = pc;
                    info->is_live[a] = 1;
                }
                break;
            }
            default:
                break;
        }
    }
}

void ljit_analyze_type_inference(ljit_ctx_t *ctx) {
    if (!ctx || !ctx->proto || !ctx->analyze_info) return;

    ljit_analyze_info_t *info = (ljit_analyze_info_t *)ctx->analyze_info;
    Proto *proto = ctx->proto;

    for (int pc = 0; pc < proto->sizecode; pc++) {
        Instruction i = proto->code[pc];
        OpCode op = GET_OPCODE(i);

        switch (op) {
            case OP_LOADI: {
                int a = GETARG_A(i);
                if (a < info->max_regs) info->reg_types[a] = JIT_TYPE_INT;
                break;
            }
            case OP_LOADF: {
                int a = GETARG_A(i);
                if (a < info->max_regs) info->reg_types[a] = JIT_TYPE_NUM;
                break;
            }
            case OP_LOADNIL: {
                int a = GETARG_A(i);
                int b = GETARG_B(i);
                for (int reg = a; reg <= a + b && reg < info->max_regs; reg++) {
                    info->reg_types[reg] = JIT_TYPE_NIL;
                }
                break;
            }
            case OP_LOADTRUE:
            case OP_LOADFALSE: {
                int a = GETARG_A(i);
                if (a < info->max_regs) info->reg_types[a] = JIT_TYPE_BOOL;
                break;
            }
            case OP_NEWTABLE: {
                int a = GETARG_A(i);
                if (a < info->max_regs) info->reg_types[a] = JIT_TYPE_TAB;
                break;
            }
            case OP_ADD:
            case OP_SUB:
            case OP_MUL:
            case OP_DIV:
            case OP_MOD:
            case OP_POW:
            case OP_UNM: {
                int a = GETARG_A(i);
                if (a < info->max_regs) {
                    // This is heuristic; precise type depends on operands
                    info->reg_types[a] = JIT_TYPE_NUM;
                }
                break;
            }
            case OP_IDIV:
            case OP_BAND:
            case OP_BOR:
            case OP_BXOR:
            case OP_SHL:
            case OP_SHR:
            case OP_BNOT: {
                int a = GETARG_A(i);
                if (a < info->max_regs) info->reg_types[a] = JIT_TYPE_INT;
                break;
            }
            case OP_NOT:
            case OP_EQ:
            case OP_LT:
            case OP_LE:
            case OP_EQK:
            case OP_EQI:
            case OP_LTI:
            case OP_LEI:
            case OP_GTI:
            case OP_GEI: {
                // Relational operators usually don't set a register directly (they jump),
                // except OP_NOT
                if (op == OP_NOT) {
                    int a = GETARG_A(i);
                    if (a < info->max_regs) info->reg_types[a] = JIT_TYPE_BOOL;
                }
                break;
            }
            default:
                break;
        }
    }
}

void ljit_analyze_destroy(ljit_ctx_t *ctx) {
    if (!ctx || !ctx->analyze_info) return;

    ljit_analyze_info_t *info = (ljit_analyze_info_t *)ctx->analyze_info;
    if (info->reg_types) free(info->reg_types);
    if (info->def_pc) free(info->def_pc);
    if (info->is_live) free(info->is_live);
    free(info);
    ctx->analyze_info = NULL;
}

void ljit_analyze(ljit_ctx_t *ctx) {
    if (!ctx || !ctx->proto) return;

    // Allocate analyze info
    ljit_analyze_info_t *info = (ljit_analyze_info_t *)malloc(sizeof(ljit_analyze_info_t));
    if (info) {
        info->max_regs = ctx->proto->maxstacksize;

        // Allocate arrays based on max virtual registers
        info->reg_types = (ljit_type_t *)calloc(info->max_regs, sizeof(ljit_type_t));
        info->def_pc = (int *)malloc(info->max_regs * sizeof(int));
        info->is_live = (int *)calloc(info->max_regs, sizeof(int));

        if (!info->reg_types || !info->def_pc || !info->is_live) {
            if (info->reg_types) free(info->reg_types);
            if (info->def_pc) free(info->def_pc);
            if (info->is_live) free(info->is_live);
            free(info);
            ctx->analyze_info = NULL;
        } else {
            for (int i = 0; i < info->max_regs; i++) {
                info->def_pc[i] = -1; // -1 means undefined
            }
            ctx->analyze_info = info;
        }
    }

    // Build the Control Flow Graph (CFG)
    ctx->cfg = ljit_ir_bb_build(ctx->proto);

    if (ctx->analyze_info) {
        // Perform data flow analysis
        ljit_analyze_dataflow(ctx);

        // Perform type inference
        ljit_analyze_type_inference(ctx);
    }
}
