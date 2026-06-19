#include "ljit_regalloc.h"
#include "../core/ljit_debug.h"
#include <stdlib.h>

void ljit_regalloc(ljit_ctx_t *ctx) {
    if (!ctx || !ctx->ir_head) return;

    JIT_DBG(MOD_REG, "regalloc start");

    // Run passes
    JIT_DBG(MOD_REG, "live interval analysis...");
    ljit_reg_live(ctx);
    JIT_DBG(MOD_REG, "interference graph...");
    ljit_reg_graph(ctx);
    JIT_DBG(MOD_REG, "graph coloring...");
    ljit_reg_color(ctx);
    JIT_DBG(MOD_REG, "spill handling...");
    ljit_reg_spill(ctx);
    JIT_DBG(MOD_REG, "applying mappings...");
    ljit_reg_alloc_process(ctx);

    ljit_regalloc_info_t *info = (ljit_regalloc_info_t *)ctx->regalloc_info;
    if (!info) return;

    // Cleanup info
    if (info->is_livein) free(info->is_livein);
    if (info->intervals) free(info->intervals);
    if (info->interference_graph) free(info->interference_graph);
    if (info->reg_mapping) free(info->reg_mapping);
    if (info->is_spilled) free(info->is_spilled);
    if (info->stack_offsets) free(info->stack_offsets);
    free(info);
    ctx->regalloc_info = NULL;
}
