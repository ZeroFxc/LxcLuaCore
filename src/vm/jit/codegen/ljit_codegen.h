#ifndef ljit_codegen_h
#define ljit_codegen_h

void ljit_codegen(void *ctx);

void ljit_cg_emit_add(void *ctx);
void ljit_cg_emit_sub(void *ctx);

void ljit_cg_emit_jmp(void *ctx);
void ljit_cg_emit_ret(void *ctx);

void ljit_cg_emit_gettable(void *ctx);
void ljit_cg_emit_settable(void *ctx);

void ljit_cg_emit_call(void *ctx);

void ljit_cg_emit_conv(void *ctx);

#endif
