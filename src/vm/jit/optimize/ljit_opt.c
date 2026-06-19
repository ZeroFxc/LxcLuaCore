#include "ljit_opt.h"
#include "../core/ljit_debug.h"

void ljit_optimize(ljit_ctx_t *ctx) {
    if (!ctx) return;

    JIT_DBG(MOD_OPT, "start");
    JIT_DBG(MOD_OPT, "constant folding...");
    ljit_opt_const(ctx);
    JIT_DBG(MOD_OPT, "CSE...");
    ljit_opt_cse(ctx);
    JIT_DBG(MOD_OPT, "peephole...");
    ljit_opt_peep(ctx);
    JIT_DBG(MOD_OPT, "DCE...");
    ljit_opt_dce(ctx);
    JIT_DBG(MOD_OPT, "inlining...");
    ljit_opt_inline(ctx);
    JIT_DBG(MOD_OPT, "done");
}
