#include "ljit_regalloc.h"
#include <stdlib.h>

void ljit_regalloc(ljit_ctx_t *ctx) {
    if (!ctx || !ctx->ir_head) return;

    // Run passes
    ljit_reg_live(ctx);
    ljit_reg_graph(ctx);
    ljit_reg_color(ctx);
    ljit_reg_spill(ctx);
    ljit_reg_alloc_process(ctx);

    ljit_regalloc_info_t *info = (ljit_regalloc_info_t *)ctx->regalloc_info;
    if (!info) return;

    // Cleanup info
    if (info->intervals) free(info->intervals);
    if (info->interference_graph) free(info->interference_graph);
    if (info->reg_mapping) free(info->reg_mapping);
    if (info->is_spilled) free(info->is_spilled);
    if (info->stack_offsets) free(info->stack_offsets);
    free(info);
    ctx->regalloc_info = NULL;
}
