#include "ljit_regalloc.h"
#include <stdlib.h>

void ljit_reg_spill(ljit_ctx_t *ctx) {
    if (!ctx || !ctx->proto || !ctx->regalloc_info) return;

    int max_vregs = ctx->proto->maxstacksize;
    ljit_regalloc_info_t *info = (ljit_regalloc_info_t *)ctx->regalloc_info;

    info->stack_offsets = (int *)calloc(max_vregs, sizeof(int));
    if (!info->stack_offsets) return;

    for (int i = 0; i < max_vregs; i++) {
        if (info->is_spilled[i]) {
            info->stack_offsets[i] = i * sizeof(TValue);
        } else {
            info->stack_offsets[i] = -1; // Not spilled
        }
    }
}
