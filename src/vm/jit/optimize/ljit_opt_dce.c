#include "ljit_opt.h"
#include "../ir/ljit_ir.h"
#include <stdlib.h>

/*
 * M3 死代码消除 (Dead Code Elimination)
 * 遍历 IR 链表，如果发现某条指令计算的值（目标寄存器）在后续的代码中
 * 并没有被任何源操作数使用，且该指令没有其他副作用（如简单的 LOADI 或 MOV），
 * 则该节点被视为死代码，并将其从 IR 链表中移除。
 */
void ljit_opt_dce(ljit_ctx_t *ctx) {
    if (!ctx || !ctx->ir_head) return;

    int changed = 1;

    while (changed) {
        changed = 0;
        ljit_ir_node_t *node = ctx->ir_tail; // 从后往前遍历比较方便找使用情况

        while (node) {
            ljit_ir_node_t *prev_node = node->prev;

            // 目前只安全地消除没有副作用的纯计算指令（常量加载，移动）
            // 如果常量折叠起作用，会产生大量未被使用的 LOADI。
            if (node->op == IR_LOADI || node->op == IR_LOADF || node->op == IR_LOADK || node->op == IR_MOV) {
                if (node->dest.type == IR_VAL_REG) {
                    int is_used = 0;
                    int dest_reg = node->dest.v.reg;
                    ljit_ir_node_t *curr = node->next;

                    while (curr) {
                        if ((curr->src1.type == IR_VAL_REG && curr->src1.v.reg == dest_reg) ||
                            (curr->src2.type == IR_VAL_REG && curr->src2.v.reg == dest_reg)) {
                            is_used = 1;
                            break;
                        }

                        // 如果遇到了对同一寄存器的重新赋值，说明之前的值已经被覆盖了，不再向后查找
                        if (curr->dest.type == IR_VAL_REG && curr->dest.v.reg == dest_reg) {
                            break;
                        }

                        // 为了简化，遇到控制流分支时停止搜索并假设被使用（保守策略）
                        if (curr->op == IR_JMP || curr->op == IR_CJMP || curr->op == IR_RET) {
                            is_used = 1;
                            break;
                        }

                        curr = curr->next;
                    }

                    if (!is_used) {
                        /* 将 node 从链表中移除 */
                        if (node->prev) node->prev->next = node->next;
                        else ctx->ir_head = node->next;

                        if (node->next) node->next->prev = node->prev;
                        else ctx->ir_tail = node->prev;

                        free(node);
                        changed = 1; // 链表有修改，可能产生新的死代码，需要重试
                    }
                }
            }

            node = prev_node;
        }
    }
}
