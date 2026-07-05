#include "ljit_analyze.h"
#include "../core/ljit_debug.h"
#include "../../../core/lopcodes.h"
#include "../../../core/lua.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 前向声明 */
static ljit_type_t type_meet(ljit_type_t a, ljit_type_t b);

void ljit_analyze_dataflow(ljit_ctx_t *ctx) {
    if (!ctx || !ctx->proto || !ctx->analyze_info) return;

    ljit_analyze_info_t *info = (ljit_analyze_info_t *)ctx->analyze_info;
    Proto *proto = ctx->proto;

    JIT_DBG(MOD_ANALYZE, "dataflow analysis: sizecode=%d, max_regs=%d", proto->sizecode, info->max_regs);

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
            case OP_SHLI:
            case OP_SHRI:
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

    JIT_DBG(MOD_ANALYZE, "type inference: sizecode=%d", proto->sizecode);

    for (int pc = 0; pc < proto->sizecode; pc++) {
        Instruction i = proto->code[pc];
        OpCode op = GET_OPCODE(i);
        int a = GETARG_A(i);

        switch (op) {
            case OP_LOADI: {
                /* 加载立即整数 */
                if (a < info->max_regs) {
                    info->reg_types[a] = JIT_TYPE_INT;
                    JIT_DBG(MOD_ANALYZE, "  pc=%d OP_LOADI: R[%d] = INT", pc, a);
                }
                break;
            }
            case OP_LOADF: {
                /* 加载立即浮点数 */
                if (a < info->max_regs) {
                    info->reg_types[a] = JIT_TYPE_NUM;
                    JIT_DBG(MOD_ANALYZE, "  pc=%d OP_LOADF: R[%d] = NUM", pc, a);
                }
                break;
            }
            case OP_LOADK: {
                /* 加载常量，根据常量类型推断 */
                int bx = GETARG_Bx(i);
                if (a < info->max_regs && bx < proto->sizek) {
                    const TValue *kv = &proto->k[bx];
                    ljit_type_t t;
                    if (ttisinteger(kv)) {
                        t = JIT_TYPE_INT;
                    } else if (ttisfloat(kv)) {
                        t = JIT_TYPE_NUM;
                    } else if (ttisstring(kv)) {
                        t = JIT_TYPE_STR;
                    } else if (ttisboolean(kv)) {
                        t = JIT_TYPE_BOOL;
                    } else if (ttisnil(kv)) {
                        t = JIT_TYPE_NIL;
                    } else if (ttistable(kv)) {
                        t = JIT_TYPE_TAB;
                    } else if (ttisfunction(kv)) {
                        t = JIT_TYPE_FUNC;
                    } else {
                        t = JIT_TYPE_ANY;
                    }
                    info->reg_types[a] = t;
                    JIT_DBG(MOD_ANALYZE, "  pc=%d OP_LOADK: R[%d] = type(%d) from K[%d]", pc, a, t, bx);
                }
                break;
            }
            case OP_LOADNIL: {
                /* 加载nil到一组寄存器 */
                int b = GETARG_B(i);
                for (int reg = a; reg <= a + b && reg < info->max_regs; reg++) {
                    info->reg_types[reg] = JIT_TYPE_NIL;
                    JIT_DBG(MOD_ANALYZE, "  pc=%d OP_LOADNIL: R[%d] = NIL", pc, reg);
                }
                break;
            }
            case OP_LOADTRUE:
            case OP_LOADFALSE: {
                /* 加载布尔值 */
                if (a < info->max_regs) {
                    info->reg_types[a] = JIT_TYPE_BOOL;
                    JIT_DBG(MOD_ANALYZE, "  pc=%d OP_LOADBOOL: R[%d] = BOOL", pc, a);
                }
                break;
            }
            case OP_NEWTABLE: {
                /* 创建新表 */
                if (a < info->max_regs) {
                    info->reg_types[a] = JIT_TYPE_TAB;
                    JIT_DBG(MOD_ANALYZE, "  pc=%d OP_NEWTABLE: R[%d] = TAB", pc, a);
                }
                break;
            }
            case OP_CLOSURE: {
                /* 创建闭包 */
                if (a < info->max_regs) {
                    info->reg_types[a] = JIT_TYPE_FUNC;
                    JIT_DBG(MOD_ANALYZE, "  pc=%d OP_CLOSURE: R[%d] = FUNC", pc, a);
                }
                break;
            }
            case OP_ADD:
            case OP_SUB:
            case OP_MUL:
            case OP_DIV:
            case OP_MOD:
            case OP_POW:
            case OP_UNM: {
                /* 算术运算，保守推断为NUM（浮点） */
                if (a < info->max_regs) {
                    info->reg_types[a] = JIT_TYPE_NUM;
                    JIT_DBG(MOD_ANALYZE, "  pc=%d OP_ARITH: R[%d] = NUM", pc, a);
                }
                break;
            }
            case OP_IDIV:
            case OP_BAND:
            case OP_BOR:
            case OP_BXOR:
            case OP_SHL:
            case OP_SHR:
            case OP_BNOT:
            case OP_SHLI:
            case OP_SHRI: {
                /* 整数运算/位运算 */
                if (a < info->max_regs) {
                    info->reg_types[a] = JIT_TYPE_INT;
                    JIT_DBG(MOD_ANALYZE, "  pc=%d OP_INTARITH: R[%d] = INT", pc, a);
                }
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
                /* 关系运算符和逻辑非 */
                if (op == OP_NOT) {
                    if (a < info->max_regs) {
                        info->reg_types[a] = JIT_TYPE_BOOL;
                        JIT_DBG(MOD_ANALYZE, "  pc=%d OP_NOT: R[%d] = BOOL", pc, a);
                    }
                }
                break;
            }
            case OP_CONCAT: {
                /* 字符串连接 */
                if (a < info->max_regs) {
                    info->reg_types[a] = JIT_TYPE_STR;
                    JIT_DBG(MOD_ANALYZE, "  pc=%d OP_CONCAT: R[%d] = STR", pc, a);
                }
                break;
            }
            case OP_LEN: {
                /* 取长度运算符，结果为整数 */
                if (a < info->max_regs) {
                    info->reg_types[a] = JIT_TYPE_INT;
                    JIT_DBG(MOD_ANALYZE, "  pc=%d OP_LEN: R[%d] = INT", pc, a);
                }
                break;
            }
            case OP_GETUPVAL:
            case OP_GETTABUP:
            case OP_GETTABLE:
            case OP_GETI:
            case OP_GETFIELD:
            case OP_MAPGET: {
                /* 各种取值操作，保守设置为ANY */
                if (a < info->max_regs) {
                    info->reg_types[a] = JIT_TYPE_ANY;
                    JIT_DBG(MOD_ANALYZE, "  pc=%d OP_GET*: R[%d] = ANY", pc, a);
                }
                break;
            }
            case OP_CALL:
            case OP_TAILCALL: {
                /* 函数调用，标记返回值寄存器为ANY */
                int c = GETARG_C(i);
                if (a < info->max_regs) {
                    int nret;
                    if (c == 0 || c == LUA_MULTRET) {
                        /* 多返回值，至少标记a寄存器 */
                        nret = 1;
                    } else {
                        /* c表示返回值数量+1，实际返回c-1个 */
                        nret = c - 1;
                        if (nret < 1) nret = 1;
                    }
                    for (int reg = a; reg < a + nret && reg < info->max_regs; reg++) {
                        info->reg_types[reg] = JIT_TYPE_ANY;
                        JIT_DBG(MOD_ANALYZE, "  pc=%d OP_CALL: R[%d] = ANY (retval)", pc, reg);
                    }
                }
                break;
            }
            case OP_SELF: {
                /* 方法调用: R[A+1] = R[B], R[A] = 方法 */
                int b = GETARG_B(i);
                /* R[A] 是方法，保守设为ANY */
                if (a < info->max_regs) {
                    info->reg_types[a] = JIT_TYPE_ANY;
                    JIT_DBG(MOD_ANALYZE, "  pc=%d OP_SELF: R[%d] = ANY (method)", pc, a);
                }
                /* R[A+1] 是self/源对象，继承R[B]的类型 */
                if (a + 1 < info->max_regs) {
                    if (b < info->max_regs) {
                        info->reg_types[a + 1] = info->reg_types[b];
                    } else {
                        info->reg_types[a + 1] = JIT_TYPE_ANY;
                    }
                    JIT_DBG(MOD_ANALYZE, "  pc=%d OP_SELF: R[%d] = type(%d) (from R[%d])", pc, a + 1, info->reg_types[a + 1], b);
                }
                break;
            }
            default:
                break;
        }
    }
}

/* ========== CFG 数据流分析 ========== */

/* 合并两个类型：实现 meet 语义 */
static ljit_type_t type_meet(ljit_type_t a, ljit_type_t b) {
    /* 包含 ANY 的合并结果为 ANY */
    if (a == JIT_TYPE_ANY || b == JIT_TYPE_ANY) return JIT_TYPE_ANY;

    /* 相同类型直接返回 */
    if (a == b) return a;

    /* INT ∪ NUM = NUM */
    if ((a == JIT_TYPE_INT && b == JIT_TYPE_NUM) ||
        (a == JIT_TYPE_NUM && b == JIT_TYPE_INT))
        return JIT_TYPE_NUM;

    /* 其他不同类型合并为 ANY */
    return JIT_TYPE_ANY;
}

/* 对单个 BB 运行传输函数：从入口类型 in_types 扫描 BB 内指令，更新为出口类型 out_types */
static void transfer_block(ljit_analyze_info_t *info, Proto *proto, ljit_bb_t *bb) {
    int max_regs = info->max_regs;
    int bb_id = bb->bb_id;
    ljit_type_t *state = info->out_types + bb_id * max_regs;

    /* 从入口类型初始化状态 */
    memcpy(state, info->in_types + bb_id * max_regs, max_regs * sizeof(ljit_type_t));

    /* 线性扫描 BB 内所有指令，更新类型 */
    for (int pc = bb->start_pc; pc <= bb->end_pc; pc++) {
        Instruction i = proto->code[pc];
        OpCode op = GET_OPCODE(i);
        int a = GETARG_A(i);

        switch (op) {
            case OP_LOADI: {
                if (a < max_regs) state[a] = JIT_TYPE_INT;
                break;
            }
            case OP_LOADF: {
                if (a < max_regs) state[a] = JIT_TYPE_NUM;
                break;
            }
            case OP_LOADK: {
                int bx = GETARG_Bx(i);
                if (a < max_regs && bx < proto->sizek) {
                    const TValue *kv = &proto->k[bx];
                    if (ttisinteger(kv)) state[a] = JIT_TYPE_INT;
                    else if (ttisfloat(kv)) state[a] = JIT_TYPE_NUM;
                    else if (ttisstring(kv)) state[a] = JIT_TYPE_STR;
                    else if (ttisboolean(kv)) state[a] = JIT_TYPE_BOOL;
                    else if (ttisnil(kv)) state[a] = JIT_TYPE_NIL;
                    else if (ttistable(kv)) state[a] = JIT_TYPE_TAB;
                    else if (ttisfunction(kv)) state[a] = JIT_TYPE_FUNC;
                    else state[a] = JIT_TYPE_ANY;
                }
                break;
            }
            case OP_LOADNIL: {
                int b = GETARG_B(i);
                for (int reg = a; reg <= a + b && reg < max_regs; reg++) {
                    state[reg] = JIT_TYPE_NIL;
                }
                break;
            }
            case OP_LOADTRUE:
            case OP_LOADFALSE: {
                if (a < max_regs) state[a] = JIT_TYPE_BOOL;
                break;
            }
            case OP_NEWTABLE: {
                if (a < max_regs) state[a] = JIT_TYPE_TAB;
                break;
            }
            case OP_CLOSURE: {
                if (a < max_regs) state[a] = JIT_TYPE_FUNC;
                break;
            }
            case OP_ADD: case OP_SUB: case OP_MUL:
            case OP_DIV: case OP_MOD: case OP_POW:
            case OP_UNM: {
                if (a < max_regs) state[a] = JIT_TYPE_NUM;
                break;
            }
            case OP_IDIV: case OP_BAND: case OP_BOR:
            case OP_BXOR: case OP_SHL: case OP_SHR:
            case OP_BNOT: case OP_SHLI: case OP_SHRI: {
                if (a < max_regs) state[a] = JIT_TYPE_INT;
                break;
            }
            case OP_NOT: {
                if (a < max_regs) state[a] = JIT_TYPE_BOOL;
                break;
            }
            case OP_CONCAT: {
                if (a < max_regs) state[a] = JIT_TYPE_STR;
                break;
            }
            case OP_LEN: {
                if (a < max_regs) state[a] = JIT_TYPE_INT;
                break;
            }
            case OP_GETUPVAL: case OP_GETTABUP:
            case OP_GETTABLE: case OP_GETI:
            case OP_GETFIELD: case OP_MAPGET: {
                if (a < max_regs) state[a] = JIT_TYPE_ANY;
                break;
            }
            case OP_CALL: case OP_TAILCALL: {
                if (a < max_regs) state[a] = JIT_TYPE_ANY;
                break;
            }
            case OP_MOVE: {
                /* 类型传播：dest = src 的类型 */
                int b = GETARG_B(i);
                if (a < max_regs && b < max_regs) {
                    state[a] = state[b];
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
            default:
                break;
        }
    }
}

/* 基于 CFG 的迭代数据流分析 */
void ljit_analyze_cfg_flow(ljit_ctx_t *ctx) {
    if (!ctx || !ctx->proto || !ctx->analyze_info || !ctx->cfg) return;

    ljit_analyze_info_t *info = (ljit_analyze_info_t *)ctx->analyze_info;
    Proto *proto = ctx->proto;
    int max_regs = info->max_regs;

    /* 统计 BB 数量 */
    int bb_count = 0;
    ljit_bb_t *bb = ctx->cfg;
    while (bb) { bb_count++; bb = bb->next; }
    info->num_bbs = bb_count;

    if (bb_count == 0) return;

    int total = bb_count * max_regs;

    /* 分配 in_types 和 out_types 扁平数组 */
    info->in_types = (ljit_type_t *)calloc(total, sizeof(ljit_type_t));
    info->out_types = (ljit_type_t *)calloc(total, sizeof(ljit_type_t));
    if (!info->in_types || !info->out_types) {
        if (info->in_types) { free(info->in_types); info->in_types = NULL; }
        if (info->out_types) { free(info->out_types); info->out_types = NULL; }
        return;
    }

    /* 初始化：入口块（BB 0）in_types 全为 JIT_TYPE_ANY，
       其他 BB in_types 也全为 JIT_TYPE_ANY（默认值即 calloc 的 0） */
    JIT_DBG(MOD_ANALYZE, "CFG flow analysis: num_bbs=%d, max_regs=%d, total=%d", bb_count, max_regs, total);

    /* 迭代数据流分析，直到不动点或达到上限 */
    int iter;
    int changed;
    for (iter = 0; iter < 10; iter++) {
        changed = 0;

        for (bb = ctx->cfg; bb; bb = bb->next) {
            int bb_id = bb->bb_id;
            ljit_type_t *in_ptr = info->in_types + bb_id * max_regs;
            ljit_type_t *out_ptr = info->out_types + bb_id * max_regs;

            /* 合并（meet）所有前驱的 out_types 到当前 BB 的 in_types */
            if (bb_id == 0) {
                /* 入口块：in_types 保持 JIT_TYPE_ANY（不变） */
            } else {
                /* 对于非入口块，合并所有前驱的 out_types */
                if (bb->pred_count > 0) {
                    /* 从第一个前驱初始化 */
                    int first_pred_id = bb->preds[0]->bb_id;
                    memcpy(in_ptr, info->out_types + first_pred_id * max_regs,
                           max_regs * sizeof(ljit_type_t));
                    /* 合并其余前驱 */
                    for (int p = 1; p < bb->pred_count; p++) {
                        int pred_id = bb->preds[p]->bb_id;
                        ljit_type_t *pred_out = info->out_types + pred_id * max_regs;
                        for (int r = 0; r < max_regs; r++) {
                            in_ptr[r] = type_meet(in_ptr[r], pred_out[r]);
                        }
                    }
                }
                /* 无前驱的块保持 JIT_TYPE_ANY */
            }

            /* 保存旧的 out_types 用于比较 */
            ljit_type_t *old_out = (ljit_type_t *)malloc(max_regs * sizeof(ljit_type_t));
            if (old_out) {
                memcpy(old_out, out_ptr, max_regs * sizeof(ljit_type_t));
            }

            /* 运行传输函数 */
            transfer_block(info, proto, bb);

            /* 检查是否变化 */
            if (old_out) {
                if (memcmp(old_out, out_ptr, max_regs * sizeof(ljit_type_t)) != 0) {
                    changed = 1;
                }
                free(old_out);
            }
        }

        JIT_DBG(MOD_ANALYZE, "  iter %d: changed=%d", iter, changed);
        if (!changed) break;
    }

    JIT_DBG(MOD_ANALYZE, "CFG flow analysis done: %d iterations", iter + 1);

    /* 打印每个 BB 的入口类型摘要 */
    for (bb = ctx->cfg; bb; bb = bb->next) {
        int bb_id = bb->bb_id;
        ljit_type_t *in_ptr = info->in_types + bb_id * max_regs;
        char buf[512] = {0};
        int off = 0;
        for (int r = 0; r < max_regs && off < (int)sizeof(buf) - 16; r++) {
            if (in_ptr[r] != JIT_TYPE_ANY) {
                off += snprintf(buf + off, sizeof(buf) - off, " R%d=%d", r, in_ptr[r]);
            }
        }
        if (off == 0) {
            snprintf(buf, sizeof(buf), " (all ANY)");
        }
        JIT_DBG(MOD_ANALYZE, "  BB%d in_types:%s", bb_id, buf);
    }
}

void ljit_analyze_destroy(ljit_ctx_t *ctx) {
    if (!ctx || !ctx->analyze_info) return;

    ljit_analyze_info_t *info = (ljit_analyze_info_t *)ctx->analyze_info;
    if (info->reg_types) free(info->reg_types);
    if (info->def_pc) free(info->def_pc);
    if (info->is_live) free(info->is_live);
    /* 释放 CFG 数据流分析数组 */
    if (info->in_types) free(info->in_types);
    if (info->out_types) free(info->out_types);
    free(info);
    ctx->analyze_info = NULL;
}

void ljit_analyze(ljit_ctx_t *ctx) {
    if (!ctx || !ctx->proto) return;

    JIT_DBG(MOD_ANALYZE, "start: maxstacksize=%d, sizecode=%d", ctx->proto->maxstacksize, ctx->proto->sizecode);

    // Allocate analyze info
    ljit_analyze_info_t *info = (ljit_analyze_info_t *)malloc(sizeof(ljit_analyze_info_t));
    if (info) {
        info->max_regs = ctx->proto->maxstacksize;
        info->num_bbs = 0;
        info->in_types = NULL;
        info->out_types = NULL;

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

        // Perform type inference (linear scan, as baseline)
        ljit_analyze_type_inference(ctx);

        // Perform CFG-based iterative dataflow analysis
        ljit_analyze_cfg_flow(ctx);

        // 将 CFG 分析结果合并回全局 reg_types（对所有 BB 的 out_types 做 meet）
        if (info->in_types && info->out_types && ctx->cfg) {
            // 用 BB0 的入口类型初始化（保守初始值）
            memcpy(info->reg_types, info->in_types, info->max_regs * sizeof(ljit_type_t));
            // 遍历所有BB的out_types做meet
            for (ljit_bb_t *b = ctx->cfg; b; b = b->next) {
                ljit_type_t *out = info->out_types + b->bb_id * info->max_regs;
                for (int r = 0; r < info->max_regs; r++) {
                    info->reg_types[r] = type_meet(info->reg_types[r], out[r]);
                }
            }
        }
    }

    JIT_DBG(MOD_ANALYZE, "done");
}