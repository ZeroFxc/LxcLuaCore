#ifndef ljit_opt_h
#define ljit_opt_h

#include "../ir/ljit_ir.h"

void ljit_optimize(ljit_ctx_t *ctx);

void ljit_opt_const(ljit_ctx_t *ctx);
void ljit_opt_cse(ljit_ctx_t *ctx);
void ljit_opt_peep(ljit_ctx_t *ctx);
void ljit_opt_dce(ljit_ctx_t *ctx);

#endif
