#include "ljit_opt.h"
#include "../ir/ljit_ir.h"
#include "../core/ljit_debug.h"

/* 优化统计计数器 */
static int opt_const_fold_count = 0;    /* 常量折叠次数 */
static int opt_guard_elim_count = 0;    /* 冗余 guard 消除次数 */

/*
 * M3 Constant Folding (常量折叠)
 * 遍历 IR 链表，如果发现算术指令的操作数都是常量（目前以 IR_LOADI 加载的立即数为主），
 * 则在编译期计算出结果，并将该算术指令替换为 IR_LOADI 指令。
 *
 * 增强版：同时支持直接 IR_VAL_INT 操作数，无需向后搜索。
 */
void ljit_opt_const(ljit_ctx_t *ctx) {
    if (!ctx || !ctx->ir_head) return;

    ljit_ir_node_t *node = ctx->ir_head;
    while (node) {
        /* 只处理基础算术指令 */
        if (node->op == IR_ADD || node->op == IR_SUB || node->op == IR_MUL || node->op == IR_DIV) {

            /* 检查 src1 和 src2 是否都是虚拟寄存器，并且它们的值是否在之前被常量赋值过。
               为了简化，在这个局部块内向前寻找最近的赋值（简单的局部数据流分析） */
            int is_src1_const = 0, is_src2_const = 0;
            lua_Integer val1 = 0, val2 = 0;

            /* 增强：先检查是否直接为 IR_VAL_INT */
            if (node->src1.type == IR_VAL_INT) {
                is_src1_const = 1;
                val1 = node->src1.v.i;
            } else if (node->src1.type == IR_VAL_REG) {
                ljit_ir_node_t *prev = node->prev;
                while (prev) {
                    if (prev->dest.type == IR_VAL_REG && prev->dest.v.reg == node->src1.v.reg) {
                        if (prev->op == IR_LOADI && prev->src1.type == IR_VAL_INT) {
                            is_src1_const = 1;
                            val1 = prev->src1.v.i;
                        }
                        break; /* 找到了最近的赋值，跳出 */
                    }
                    /* 如果遇到了控制流跳转，安全起见停止向前搜索常量 */
                    if (prev->op == IR_JMP || prev->op == IR_CJMP || prev->op == IR_RET) break;
                    prev = prev->prev;
                }
            }

            /* 增强：先检查是否直接为 IR_VAL_INT */
            if (node->src2.type == IR_VAL_INT) {
                is_src2_const = 1;
                val2 = node->src2.v.i;
            } else if (node->src2.type == IR_VAL_REG) {
                ljit_ir_node_t *prev = node->prev;
                while (prev) {
                    if (prev->dest.type == IR_VAL_REG && prev->dest.v.reg == node->src2.v.reg) {
                        if (prev->op == IR_LOADI && prev->src1.type == IR_VAL_INT) {
                            is_src2_const = 1;
                            val2 = prev->src1.v.i;
                        }
                        break;
                    }
                    if (prev->op == IR_JMP || prev->op == IR_CJMP || prev->op == IR_RET) break;
                    prev = prev->prev;
                }
            }

            /* 如果两个操作数都可以确定为常量 */
            if (is_src1_const && is_src2_const) {
                lua_Integer result = 0;
                int can_fold = 1;

                switch (node->op) {
                    case IR_ADD: result = val1 + val2; break;
                    case IR_SUB: result = val1 - val2; break;
                    case IR_MUL: result = val1 * val2; break;
                    case IR_DIV:
                        if (val2 != 0) {
                            result = val1 / val2;
                        } else {
                            can_fold = 0; /* 运行时除以0，不进行常量折叠 */
                        }
                        break;
                    default: can_fold = 0; break;
                }

                if (can_fold) {
                    /* 将当前运算节点替换为 IR_LOADI，把结果直接赋给目标寄存器 */
                    JIT_DBG(MOD_OPT_CONST, "fold: pc=%d, op=%d, val1=%lld, val2=%lld -> result=%lld",
                        node->original_pc, node->op, (long long)val1, (long long)val2, (long long)result);
                    node->op = IR_LOADI;
                    node->src1.type = IR_VAL_INT;
                    node->src1.v.i = result;
                    node->src2.type = IR_VAL_NONE;
                    opt_const_fold_count++;
                }
            }
        }
        node = node->next;
    }
}

/*
 * 冗余 guard 消除：移除连续的 IR_CHECKTYPE 节点。
 * IR_CHECKTYPE 是字节码级别的类型检查，连续的多个检查中，
 * 第一个检查失败会使后续检查不可达，因此只保留第一个。
 */
void ljit_opt_guard_elim(ljit_ctx_t *ctx) {
    if (!ctx || !ctx->ir_head) return;

    ljit_ir_node_t *node = ctx->ir_head;
    while (node) {
        if (node->op == IR_CHECKTYPE) {
            /* 检查后续节点是否也是 IR_CHECKTYPE */
            ljit_ir_node_t *next = node->next;
            while (next && next->op == IR_CHECKTYPE) {
                JIT_DBG(MOD_OPT_CONST, "guard_elim: remove redundant IR_CHECKTYPE at pc=%d",
                    next->original_pc);
                /* 将冗余的 IR_CHECKTYPE 替换为 IR_NOP */
                next->op = IR_NOP;
                opt_guard_elim_count++;
                next = next->next;
            }
            /* 跳过已处理的冗余节点 */
            node = next;
            continue;
        }
        node = node->next;
    }
}

/*
 * 输出优化统计信息
 */
void ljit_opt_print_stats(void) {
    if (opt_const_fold_count > 0 || opt_guard_elim_count > 0) {
        fprintf(stderr, "[JIT-STATS] IR optimization stats:\n");
        fprintf(stderr, "  [JIT-STATS]   const_fold        : %d\n", opt_const_fold_count);
        fprintf(stderr, "  [JIT-STATS]   guard_elim        : %d\n", opt_guard_elim_count);
    }
}
