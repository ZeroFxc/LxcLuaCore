#include "ljit_codegen.h"
#include "../ir/ljit_ir.h"
#include "../sljit/ljit_sljit.h"

void SLJIT_FUNC ljit_icall_newclass(lua_State *L, TString *classname);
void SLJIT_FUNC ljit_icall_newobj(lua_State *L, int nargs, StkId rb, StkId ra);
void SLJIT_FUNC ljit_icall_inherit(lua_State *L, StkId rb, StkId ra);
void SLJIT_FUNC ljit_icall_getsuper(lua_State *L, TString *key, StkId rb, StkId ra);

void ljit_cg_emit_newclass(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    /* R0 = ctx->L */
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);

    /* R1 = classname (TString *) from proto->k */
    TValue *kv = &ctx->proto->k[node->src1.v.i];
    TString *ts = tsvalue(kv);
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM, (sljit_sw)ts);

    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS2V(W, W), SLJIT_IMM, (sljit_sw)ljit_icall_newclass);
}

void ljit_cg_emit_newobj(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    int tvalue_size = sizeof(TValue);

    /* R0 = ctx->L */
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);

    /* R1 = nargs (node->src2.v.i) */
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM, node->src2.v.i);

    /* R2 = rb address (base + src1.v.reg * size) */
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R2, 0, SLJIT_S0, 0, SLJIT_IMM, node->src1.v.reg * tvalue_size);

    /* R3 = ra address (base + dest.v.reg * size) */
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R3, 0, SLJIT_S0, 0, SLJIT_IMM, node->dest.v.reg * tvalue_size);

    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4V(W, W, W, W), SLJIT_IMM, (sljit_sw)ljit_icall_newobj);
}

void ljit_cg_emit_inherit(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    int tvalue_size = sizeof(TValue);

    /* R0 = ctx->L */
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);

    /* R1 = rb address */
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R1, 0, SLJIT_S0, 0, SLJIT_IMM, node->src1.v.reg * tvalue_size);

    /* R2 = ra address */
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R2, 0, SLJIT_S0, 0, SLJIT_IMM, node->dest.v.reg * tvalue_size);

    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3V(W, W, W), SLJIT_IMM, (sljit_sw)ljit_icall_inherit);
}

void ljit_cg_emit_getsuper(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    int tvalue_size = sizeof(TValue);

    /* R0 = ctx->L */
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);

    /* R1 = key (TString *) */
    TValue *kv = &ctx->proto->k[node->src2.v.i];
    TString *ts = tsvalue(kv);
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM, (sljit_sw)ts);

    /* R2 = rb address */
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R2, 0, SLJIT_S0, 0, SLJIT_IMM, node->src1.v.reg * tvalue_size);

    /* R3 = ra address */
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R3, 0, SLJIT_S0, 0, SLJIT_IMM, node->dest.v.reg * tvalue_size);

    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4V(W, W, W, W), SLJIT_IMM, (sljit_sw)ljit_icall_getsuper);
}
