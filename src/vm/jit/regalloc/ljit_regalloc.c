#include "ljit_regalloc.h"

void ljit_regalloc(ljit_ctx_t *ctx) {
    if (!ctx) return;

    ljit_reg_live(ctx);
    ljit_reg_graph(ctx);
    ljit_reg_color(ctx);
    ljit_reg_spill(ctx);
    ljit_reg_alloc_process(ctx);
}
