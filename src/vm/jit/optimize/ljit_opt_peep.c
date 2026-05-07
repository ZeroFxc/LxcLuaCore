#include "ljit_opt.h"
#include "../ir/ljit_ir.h"
#include <stdlib.h>

void ljit_opt_peep(ljit_ctx_t *ctx) {
    if (!ctx || !ctx->ir_head) return;

    ljit_ir_node_t *node = ctx->ir_head;

    while (node) {
        /* Eliminate redundant moves: IR_MOV R, R */
        if (node->op == IR_MOV && node->dest.type == IR_VAL_REG && node->src1.type == IR_VAL_REG) {
            if (node->dest.v.reg == node->src1.v.reg) {
                node->op = IR_NOP;
            }
        }

        /* Simplify identity operations */
        /* ADD x, 0 -> MOV x */
        else if (node->op == IR_ADD) {
            if (node->src2.type == IR_VAL_INT && node->src2.v.i == 0) {
                node->op = IR_MOV;
                node->src2.type = IR_VAL_NONE;
            } else if (node->src1.type == IR_VAL_INT && node->src1.v.i == 0) {
                node->op = IR_MOV;
                node->src1 = node->src2;
                node->src2.type = IR_VAL_NONE;
            }
        }
        /* SUB x, 0 -> MOV x */
        else if (node->op == IR_SUB) {
            if (node->src2.type == IR_VAL_INT && node->src2.v.i == 0) {
                node->op = IR_MOV;
                node->src2.type = IR_VAL_NONE;
            }
        }
        /* MUL x, 1 -> MOV x */
        else if (node->op == IR_MUL) {
            if (node->src2.type == IR_VAL_INT && node->src2.v.i == 1) {
                node->op = IR_MOV;
                node->src2.type = IR_VAL_NONE;
            } else if (node->src1.type == IR_VAL_INT && node->src1.v.i == 1) {
                node->op = IR_MOV;
                node->src1 = node->src2;
                node->src2.type = IR_VAL_NONE;
            }
            /* MUL x, 0 -> LOADI 0 */
            else if ((node->src2.type == IR_VAL_INT && node->src2.v.i == 0) ||
                     (node->src1.type == IR_VAL_INT && node->src1.v.i == 0)) {
                node->op = IR_LOADI;
                node->src1.type = IR_VAL_INT;
                node->src1.v.i = 0;
                node->src2.type = IR_VAL_NONE;
            }
        }
        /* DIV x, 1 -> MOV x */
        else if (node->op == IR_DIV) {
            if (node->src2.type == IR_VAL_INT && node->src2.v.i == 1) {
                node->op = IR_MOV;
                node->src2.type = IR_VAL_NONE;
            }
        }

        node = node->next;
    }
}
