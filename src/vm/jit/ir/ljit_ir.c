#include "ljit_ir.h"
#include "../frontend/ljit_analyze.h"
#include <stdlib.h>

void *ljit_context_create(lua_State *L, Proto *proto) {
    ljit_ctx_t *ctx = (ljit_ctx_t *)malloc(sizeof(ljit_ctx_t));
    if (ctx) {
        ctx->L = L;
        ctx->proto = proto;
        ctx->cfg = NULL;
        ctx->ir_head = NULL;
        ctx->ir_tail = NULL;
        ctx->next_label_id = 0;
        ctx->compiler = NULL;
        ctx->labels = NULL;
        ctx->num_jumps = 0;
        ctx->jumps = NULL;
        ctx->jump_targets = NULL;
        ctx->analyze_info = NULL;
    }
    return (void *)ctx;
}

void ljit_context_destroy(void *ctx_ptr) {
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    if (ctx) {
        ljit_ir_node_t *curr = ctx->ir_head;
        while (curr) {
            ljit_ir_node_t *next = curr->next;
            free(curr);
            curr = next;
        }

        ljit_bb_t *bb = ctx->cfg;
        while (bb) {
            ljit_bb_t *next = bb->next;
            free(bb);
            bb = next;
        }

        if (ctx->labels) {
            free(ctx->labels);
            ctx->labels = NULL;
        }
        if (ctx->jumps) {
            free(ctx->jumps);
            ctx->jumps = NULL;
        }
        if (ctx->jump_targets) {
            free(ctx->jump_targets);
            ctx->jump_targets = NULL;
        }

        ljit_analyze_destroy(ctx);

        free(ctx);
    }
}

ljit_ir_node_t *ljit_ir_new(ljit_ir_op_t op, int pc) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)calloc(1, sizeof(ljit_ir_node_t));
    if (node) {
        node->op = op;
        node->dest.type = IR_VAL_NONE;
        node->src1.type = IR_VAL_NONE;
        node->src2.type = IR_VAL_NONE;
        node->original_pc = pc;
        node->prev = NULL;
        node->next = NULL;
    }
    return node;
}
