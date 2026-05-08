#ifndef ljit_codegen_h
#define ljit_codegen_h

void *ljit_codegen(void *ctx);

void ljit_cg_emit_add(void *node, void *ctx);
void ljit_cg_emit_sub(void *node, void *ctx);
void ljit_cg_emit_mul(void *node, void *ctx);
void ljit_cg_emit_div(void *node, void *ctx);
void ljit_cg_emit_mod(void *node, void *ctx);
void ljit_cg_emit_idiv(void *node, void *ctx);
void ljit_cg_emit_unm(void *node, void *ctx);
void ljit_cg_emit_not(void *node, void *ctx);

void ljit_cg_emit_band(void *node, void *ctx);
void ljit_cg_emit_bor(void *node, void *ctx);
void ljit_cg_emit_bxor(void *node, void *ctx);
void ljit_cg_emit_shl(void *node, void *ctx);
void ljit_cg_emit_shr(void *node, void *ctx);
void ljit_cg_emit_bnot(void *node, void *ctx);

void ljit_cg_emit_mov(void *node, void *ctx);
void ljit_cg_emit_loadi(void *node, void *ctx);
void ljit_cg_emit_loadf(void *node, void *ctx);
void ljit_cg_emit_loadk(void *node, void *ctx);
void ljit_cg_emit_loadnil(void *node, void *ctx);
void ljit_cg_emit_loadbool(void *node, void *ctx);

void ljit_cg_emit_jmp(void *node, void *ctx);
void ljit_cg_emit_cmp(void *node, void *ctx);
void ljit_cg_emit_ret(void *node, void *ctx);

void ljit_cg_emit_gettable(void *node, void *ctx);
void ljit_cg_emit_settable(void *node, void *ctx);

void ljit_cg_emit_call(void *node, void *ctx);

void ljit_cg_emit_conv(void *node, void *ctx);

#endif
