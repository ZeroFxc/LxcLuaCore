#include "ljit_opt.h"

void ljit_optimize(ljit_ctx_t *ctx) {
    if (!ctx) return;

    ljit_opt_const(ctx);
    ljit_opt_cse(ctx);
    ljit_opt_peep(ctx);
    ljit_opt_dce(ctx);
}
