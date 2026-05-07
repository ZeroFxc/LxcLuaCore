#include "ljit_opt.h"
#include "../ir/ljit_ir.h"
#include <stdlib.h>

/* Helper to check if two IR values are exactly identical */
static int vals_equal(ljit_ir_val_t a, ljit_ir_val_t b) {
    if (a.type != b.type) return 0;
    switch (a.type) {
        case IR_VAL_NONE: return 1;
        case IR_VAL_REG: return a.v.reg == b.v.reg;
        case IR_VAL_CONST: return a.v.k == b.v.k;
        case IR_VAL_UPVAL: return a.v.uv == b.v.uv;
        case IR_VAL_INT: return a.v.i == b.v.i;
        case IR_VAL_NUM: return a.v.n == b.v.n;
        case IR_VAL_LABEL: return a.v.label_id == b.v.label_id;
        default: return 0;
    }
}

/* Helper to check if an instruction modifies a specific register */
static int modifies_reg(ljit_ir_node_t *node, int reg) {
    if (node->dest.type == IR_VAL_REG && node->dest.v.reg == reg) {
        return 1;
    }
    /* CALL modifies multiple registers, but for simplicity we stop search at control flow */
    if (node->op == IR_CALL) return 1;
    return 0;
}

void ljit_opt_cse(ljit_ctx_t *ctx) {
    if (!ctx || !ctx->ir_head) return;

    ljit_ir_node_t *node = ctx->ir_head;

    while (node) {
        /* Only apply CSE to pure mathematical operations for now */
        if (node->op >= IR_ADD && node->op <= IR_SHR) {

            ljit_ir_node_t *prev = node->prev;
            int found_match = 0;
            ljit_ir_node_t *match_node = NULL;

            /* Track registers clobbered between current node and match_node */
            int clobbered_regs[256] = {0};

            while (prev) {
                /* Stop searching backwards if we hit a control flow instruction */
                if (prev->op == IR_JMP || prev->op == IR_CJMP || prev->op == IR_RET || prev->op == IR_CALL) {
                    break;
                }

                /* Check if the prev instruction modifies any of the registers used by the current node */
                int conflict = 0;

                /* If prev modifies a register we use, our operation would have different inputs. Break. */
                if (node->src1.type == IR_VAL_REG && modifies_reg(prev, node->src1.v.reg)) conflict = 1;
                if (node->src2.type == IR_VAL_REG && modifies_reg(prev, node->src2.v.reg)) conflict = 1;

                if (conflict) {
                    break; /* Safe to break; values might have changed */
                }

                /* Check for identical operation */
                if (prev->op == node->op) {
                    int identical = vals_equal(prev->src1, node->src1) && vals_equal(prev->src2, node->src2);

                    /* Commutativity check for ADD, MUL, BAND, BOR, BXOR */
                    if (!identical && (node->op == IR_ADD || node->op == IR_MUL ||
                                       node->op == IR_BAND || node->op == IR_BOR || node->op == IR_BXOR)) {
                        identical = vals_equal(prev->src1, node->src2) && vals_equal(prev->src2, node->src1);
                    }

                    if (identical && prev->dest.type == IR_VAL_REG) {
                        /* Match found! Now we must check if its destination register
                           was clobbered by any instruction between `prev` and `node`. */
                        if (!clobbered_regs[prev->dest.v.reg]) {
                            found_match = 1;
                            match_node = prev;
                            break;
                        }
                    }
                }

                /* If prev modifies a register, mark it as clobbered for subsequent backwards checks */
                if (prev->dest.type == IR_VAL_REG) {
                    if (prev->dest.v.reg >= 0 && prev->dest.v.reg < 256) {
                        clobbered_regs[prev->dest.v.reg] = 1;
                    }
                }

                prev = prev->prev;
            }

            if (found_match && match_node) {
                /* Transform node into an IR_MOV using the matched node's result register */
                node->op = IR_MOV;
                node->src1 = match_node->dest;
                node->src2.type = IR_VAL_NONE;
            }
        }
        node = node->next;
    }
}
