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
void ljit_cg_emit_newtable(void *node, void *ctx);
void ljit_cg_emit_geti(void *node, void *ctx);
void ljit_cg_emit_seti(void *node, void *ctx);
void ljit_cg_emit_getfield(void *node, void *ctx);
void ljit_cg_emit_setfield(void *node, void *ctx);
void ljit_cg_emit_gettabup(void *node, void *ctx);
void ljit_cg_emit_settabup(void *node, void *ctx);

void ljit_cg_emit_call(void *node, void *ctx);

void ljit_cg_emit_concat(void *node, void *ctx);
void ljit_cg_emit_forprep(void *node, void *ctx);
void ljit_cg_emit_forloop(void *node, void *ctx);
void ljit_cg_emit_pow(void *node, void *ctx);
void ljit_cg_emit_nop(void *node, void *ctx);

void ljit_cg_emit_conv(void *node, void *ctx);

struct ljit_ctx;
/* Emitter Helpers */
struct ljit_ir_val_t;
void ljit_cg_emit_load_operand(struct ljit_ctx *ctx, int target_reg, void *val);
void ljit_cg_emit_store_operand(struct ljit_ctx *ctx, void *val, int src_reg);

#endif

#include "../../../core/lobject.h"
#include "../../../core/lstate.h"
#include "../sljit/ljit_sljit.h"
void SLJIT_FUNC ljit_icall_gettable(lua_State *L, StkId ra, TValue *rb, TValue *rc);
void SLJIT_FUNC ljit_icall_settable(lua_State *L, TValue *ra, TValue *rb, TValue *rc);
void SLJIT_FUNC ljit_icall_newtable(lua_State *L, int b, int c, StkId ra);
void SLJIT_FUNC ljit_icall_geti(lua_State *L, StkId ra, TValue *rb, int c);
void SLJIT_FUNC ljit_icall_seti(lua_State *L, StkId ra, int c, TValue *rc);
void SLJIT_FUNC ljit_icall_getfield(lua_State *L, StkId ra, TValue *rb, TValue *rc);
void SLJIT_FUNC ljit_icall_setfield(lua_State *L, StkId ra, TValue *rb, TValue *rc);
void SLJIT_FUNC ljit_icall_gettabup(lua_State *L, StkId ra, int upval_idx, TValue *rc);
void SLJIT_FUNC ljit_icall_settabup(lua_State *L, int upval_idx, TValue *rb, TValue *rc);
void SLJIT_FUNC ljit_icall_pow(lua_State *L, TValue *ra, TValue *rb, TValue *rc);
void SLJIT_FUNC ljit_icall_concat(lua_State *L, int total, StkId ra);
sljit_sw SLJIT_FUNC ljit_icall_forprep(lua_State *L, StkId ra);
sljit_sw SLJIT_FUNC ljit_icall_forloop(lua_State *L, StkId ra);
void ljit_cg_emit_closure(void *node, void *ctx);
void ljit_cg_emit_newclass(void *node, void *ctx);
void ljit_cg_emit_newobj(void *node, void *ctx);
void ljit_cg_emit_inherit(void *node, void *ctx);
void ljit_cg_emit_getsuper(void *node, void *ctx);

void ljit_cg_emit_len(void *node, void *ctx);

void SLJIT_FUNC ljit_icall_len(lua_State *L, StkId ra, TValue *rb);
void ljit_cg_emit_len(void *node, void *ctx);
