cat << 'INNER_EOF' > src/vm/jit/codegen/ljit_cg_closure.c
#include "ljit_codegen.h"
#include "../ir/ljit_ir.h"
#include "../sljit/ljit_sljit.h"

void SLJIT_FUNC ljit_icall_closure(lua_State *L, Proto *p, UpVal **encup, StkId base, StkId ra);
void SLJIT_FUNC ljit_icall_newclass(lua_State *L, TString *classname);
void SLJIT_FUNC ljit_icall_newobj(lua_State *L, int nargs, StkId rb, StkId ra);
void SLJIT_FUNC ljit_icall_inherit(lua_State *L, StkId rb, StkId ra);
void SLJIT_FUNC ljit_icall_getsuper(lua_State *L, TString *key, StkId rb, StkId ra);

void ljit_cg_emit_closure(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    int tvalue_size = sizeof(TValue);

    /* R0 = L */
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);

    /* R1 = Proto *p */
    Proto *p = ctx->proto->p[node->src1.v.i];
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM, (sljit_sw)p);

    /* R2 = UpVal **encup */
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, (sljit_sw)ctx->ci->func.p->c.upvals);

    /* R3 = base */
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R3, 0, SLJIT_S0, 0);

    /* R4 = ra (base + dest.v.reg * size) */
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_S1, 0, SLJIT_S0, 0, SLJIT_IMM, node->dest.v.reg * tvalue_size);

    /* Call ljit_icall_closure */
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS5V(W, W, W, W, W), SLJIT_IMM, (sljit_sw)ljit_icall_closure);
}
INNER_EOF
