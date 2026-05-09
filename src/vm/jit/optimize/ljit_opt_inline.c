#include "ljit_opt.h"
#include "../../../core/lopcodes.h"

/*
 * M3 Function Inlining (Basic)
 * This pass performs basic function inlining for trivial closures.
 * It looks for an IR_CALL where the closure was created by IR_CLOSURE
 * in the same block. If the target Proto is completely empty (e.g. just OP_RETURN0),
 * the call is removed.
 */
void ljit_opt_inline(ljit_ctx_t *ctx) {
    if (!ctx || !ctx->ir_head) return;

    ljit_ir_node_t *node = ctx->ir_head;

    while (node) {
        if (node->op == IR_CALL) {
            int closure_reg = -1;
            if (node->dest.type == IR_VAL_REG) {
                closure_reg = node->dest.v.reg;
            }

            /* Trace backward to find the IR_CLOSURE for this register */
            if (closure_reg >= 0) {
                ljit_ir_node_t *prev = node->prev;
                int found_closure_idx = -1;

                while (prev) {
                    if (prev->op == IR_CLOSURE && prev->dest.type == IR_VAL_REG && prev->dest.v.reg == closure_reg) {
                        if (prev->src1.type == IR_VAL_INT) {
                            found_closure_idx = prev->src1.v.i;
                        }
                        break;
                    }
                    /* If register is modified by something else, we can't be sure */
                    if (prev->dest.type == IR_VAL_REG && prev->dest.v.reg == closure_reg) {
                        break;
                    }
                    /* Stop at control flow */
                    if (prev->op == IR_JMP || prev->op == IR_CJMP || prev->op == IR_RET || prev->op == IR_CALL) {
                        break;
                    }
                    prev = prev->prev;
                }

                if (found_closure_idx >= 0 && found_closure_idx < ctx->proto->sizep) {
                    Proto *child_proto = ctx->proto->p[found_closure_idx];

                    /* Check if the child prototype is a trivial empty function: just OP_RETURN0 */
                    if (child_proto && child_proto->sizecode == 1) {
                        Instruction inst = child_proto->code[0];
                        if (GET_OPCODE(inst) == OP_RETURN0) {
                            /* To safely remove the call, we must ensure it expects exactly 0 returns
                               (otherwise we'd need to load NILs into expected return registers),
                               and that it doesn't use variable arguments (LUA_MULTRET). */
                            if (node->src2.type == IR_VAL_INT && node->src2.v.i == 0 &&
                                node->src1.type == IR_VAL_INT && node->src1.v.i >= 0) {
                                /* The function does nothing and caller expects nothing.
                                   We can safely remove the IR_CALL.
                                   Replace IR_CALL with IR_NOP. */
                                node->op = IR_NOP;
                                node->dest.type = IR_VAL_NONE;
                                node->src1.type = IR_VAL_NONE;
                                node->src2.type = IR_VAL_NONE;
                            }
                        }
                    }
                }
            }
        }
        node = node->next;
    }
}
