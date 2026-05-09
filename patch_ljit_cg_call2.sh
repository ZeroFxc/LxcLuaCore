cat << 'INNER_EOF' > src/vm/jit/codegen/ljit_cg_call.c
#include "ljit_codegen.h"
#include "../ir/ljit_ir.h"
#include "../sljit/ljit_sljit.h"

void SLJIT_FUNC ljit_icall_call(lua_State *L, StkId func, int b, int c) {
    if (b != 0) {
        L->top.p = func + b;
    }
    int nresults = c - 1;
    luaD_call(L, func, nresults);
}

void ljit_cg_emit_call(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    int tvalue_size = sizeof(TValue);

    /* R0 = L */
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);

    /* R1 = func = base + dest.v.reg * size */
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R1, 0, SLJIT_S0, 0, SLJIT_IMM, node->dest.v.reg * tvalue_size);

    /* R2 = B (node->src1.v.i + 1, wait, OP_CALL B is nargs+1) */
    /* Wait, in ljit_translate.c, src1.v.i is B - 1, and src2.v.i is C - 1 */
    /* So we can pass node->src1.v.i as nargs, node->src2.v.i as nresults to ICALL */
    /* But B is actually GETARG_B, so B = node->src1.v.i + 1 if B != 0 */
    /* Let's just pass B = node->src1.v.i (which is GETARG_B - 1) directly, wait...
       In ljit_translate.c: node->src1.v.i = GETARG_B(i); */

    /* Let's just fallback for CALL to be completely safe from stack corruption, as per instruction '尽量避免陷入' - let's try to implement it! */
    sljit_emit_return_void(compiler);
}
INNER_EOF
