#include "ljit_regalloc.h"
#include "../core/ljit_debug.h"
#include <stdlib.h>

void ljit_reg_spill(ljit_ctx_t *ctx) {
    if (!ctx || !ctx->proto || !ctx->regalloc_info) return;

    int max_vregs = ctx->proto->maxstacksize;
    ljit_regalloc_info_t *info = (ljit_regalloc_info_t *)ctx->regalloc_info;

    info->stack_offsets = (int *)calloc(max_vregs, sizeof(int));
    if (!info->stack_offsets) return;

    for (int i = 0; i < max_vregs; i++) {
        info->stack_offsets[i] = i * sizeof(TValue);
    }

    JIT_DBG(MOD_REG_SPILL, "spill slots: max_vregs=%d, tvalue_size=%zu", max_vregs, sizeof(TValue));
}
