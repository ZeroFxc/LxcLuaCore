#ifndef ljit_regalloc_h
#define ljit_regalloc_h

#include "../ir/ljit_ir.h"

void ljit_regalloc(ljit_ctx_t *ctx);
void ljit_reg_live(ljit_ctx_t *ctx);
void ljit_reg_graph(ljit_ctx_t *ctx);
void ljit_reg_color(ljit_ctx_t *ctx);
void ljit_reg_spill(ljit_ctx_t *ctx);
void ljit_reg_alloc_process(ljit_ctx_t *ctx);

#endif
