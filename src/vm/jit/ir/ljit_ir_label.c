#include "ljit_ir.h"

int ljit_ir_new_label(ljit_ctx_t *ctx) {
    if (!ctx) return -1;
    return ctx->next_label_id++;
}
