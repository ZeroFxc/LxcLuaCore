#include "ljit_ir.h"
#include "../core/ljit_debug.h"

int ljit_ir_new_label(ljit_ctx_t *ctx) {
    if (!ctx) return -1;
    int label_id = ctx->next_label_id++;
    JIT_DBG(MOD_IR_LABEL, "new label: id=%d", label_id);
    return label_id;
}
