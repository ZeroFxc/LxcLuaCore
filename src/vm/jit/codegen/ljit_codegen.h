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

void ljit_cg_emit_concat(void *node, void *ctx);
void ljit_cg_emit_forprep(void *node, void *ctx);
void ljit_cg_emit_forloop(void *node, void *ctx);

void ljit_cg_emit_conv(void *node, void *ctx);

/* Emitter Helpers */
struct ljit_ir_val_t;
void ljit_cg_emit_load_operand(void *compiler, int target_reg, void *val);
void ljit_cg_emit_store_operand(void *compiler, void *val, int src_reg);

#endif

#include "../../../core/lobject.h"
#include "../../../core/lstate.h"
#include "../sljit/ljit_sljit.h"
void SLJIT_FUNC ljit_icall_gettable(lua_State *L, StkId ra, TValue *rb, TValue *rc);
void SLJIT_FUNC ljit_icall_settable(lua_State *L, TValue *ra, TValue *rb, TValue *rc);
void SLJIT_FUNC ljit_icall_concat(lua_State *L, int total, StkId ra);
sljit_sw SLJIT_FUNC ljit_icall_forprep(lua_State *L, StkId ra);
sljit_sw SLJIT_FUNC ljit_icall_forloop(lua_State *L, StkId ra);
