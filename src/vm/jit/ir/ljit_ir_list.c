#include "ljit_ir.h"

void ljit_ir_append(ljit_ctx_t *ctx, ljit_ir_node_t *node) {
    if (!ctx || !node) return;

    if (!ctx->ir_head) {
        ctx->ir_head = node;
        ctx->ir_tail = node;
        node->prev = NULL;
        node->next = NULL;
    } else {
        node->prev = ctx->ir_tail;
        node->next = NULL;
        ctx->ir_tail->next = node;
        ctx->ir_tail = node;
    }
}
