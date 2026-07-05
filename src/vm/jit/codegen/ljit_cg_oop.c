#include "ljit_codegen.h"
#include "../ir/ljit_ir.h"
#include "../sljit/ljit_sljit.h"
#include "../core/ljit_debug.h"

void SLJIT_FUNC ljit_icall_newclass(lua_State *L, TString *classname, StkId ra);
void SLJIT_FUNC ljit_icall_inherit(lua_State *L, StkId ra, StkId rb);
void SLJIT_FUNC ljit_icall_getsuper(lua_State *L, StkId rb, TString *key, StkId ra);
void SLJIT_FUNC ljit_icall_newobj(lua_State *L, StkId rb, int nargs, StkId ra_args_base);

void ljit_cg_emit_newclass(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    int tvalue_size = sizeof(TValue);
    int ra = node->dest.v.reg;
    int k_idx = node->src1.v.i;

    JIT_DBG(MOD_CG_OOP, "NEWCLASS native: pc=%d, dest=R%d, k_idx=%d",
        node->original_pc, ra, k_idx);

    /* R0 = L (lua_State 指针) */
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);

    /* R1 = classname (TString*) 从 proto->k 表加载 */
    TValue *kv = &ctx->proto->k[k_idx];
    TString *ts = tsvalue(kv);
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM, (sljit_sw)ts);

    /* R2 = ra 地址 (栈基址 S0 + ra * sizeof(TValue)) */
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R2, 0, SLJIT_S0, 0,
                   SLJIT_IMM, (sljit_sw)(ra * tvalue_size));

    /* 调用 C 函数 ljit_icall_newclass(L, classname, ra) */
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3V(W, W, W),
                     SLJIT_IMM, (sljit_sw)ljit_icall_newclass);

    /*
     * 调用后目标寄存器 ra 在物理寄存器中的值已失效
     * 需要从栈槽重新加载到物理寄存器（如果未溢出到栈）
     */
    if (!node->dest.is_spilled) {
        sljit_emit_op1(compiler, SLJIT_MOV, node->dest.phys_reg, 0,
                       SLJIT_MEM1(SLJIT_S0), ra * tvalue_size);
    }
}

void ljit_cg_emit_newobj(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    int tvalue_size = sizeof(TValue);
    int ra = node->dest.v.reg;
    int rb_reg = node->src1.v.reg;
    int nargs = node->src2.v.i - 1;

    JIT_DBG(MOD_CG_OOP, "NEWOBJ native: pc=%d, dest=R%d, class=R%d, nargs=%d (C=%d)",
        node->original_pc, ra, rb_reg, nargs, node->src2.v.i);

    /* R0 = L (lua_State 指针) */
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);

    /* R1 = rb 地址 (类所在寄存器，S0 + rb_reg * sizeof(TValue)) */
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R1, 0, SLJIT_S0, 0,
                   SLJIT_IMM, (sljit_sw)(rb_reg * tvalue_size));

    /* R2 = nargs (实际参数个数，C - 1) */
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, (sljit_sw)nargs);

    /* R3 = ra_args_base 地址 (目标寄存器 R[A]，S0 + ra * sizeof(TValue)) */
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R3, 0, SLJIT_S0, 0,
                   SLJIT_IMM, (sljit_sw)(ra * tvalue_size));

    /* 调用 C 函数 ljit_icall_newobj(L, rb, nargs, ra_args_base) */
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4V(W, W, 32, W),
                     SLJIT_IMM, (sljit_sw)ljit_icall_newobj);

    /*
     * 调用后目标寄存器 ra 在物理寄存器中的值已失效
     * 需要从栈槽重新加载到物理寄存器
     */
    if (!node->dest.is_spilled) {
        sljit_emit_op1(compiler, SLJIT_MOV, node->dest.phys_reg, 0,
                       SLJIT_MEM1(SLJIT_S0), ra * tvalue_size);
    }
}

void ljit_cg_emit_inherit(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    int tvalue_size = sizeof(TValue);
    int ra_reg = node->dest.v.reg;
    int rb_reg = node->src1.v.reg;

    JIT_DBG(MOD_CG_OOP, "INHERIT native: pc=%d, child=R%d, parent=R%d",
        node->original_pc, ra_reg, rb_reg);

    /* R0 = L (lua_State 指针) */
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);

    /* R1 = ra 地址 (子类 R[A]，S0 + ra_reg * sizeof(TValue)) */
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R1, 0, SLJIT_S0, 0,
                   SLJIT_IMM, (sljit_sw)(ra_reg * tvalue_size));

    /* R2 = rb 地址 (父类 R[B]，S0 + rb_reg * sizeof(TValue)) */
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R2, 0, SLJIT_S0, 0,
                   SLJIT_IMM, (sljit_sw)(rb_reg * tvalue_size));

    /* 调用 C 函数 ljit_icall_inherit(L, ra, rb) */
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3V(W, W, W),
                     SLJIT_IMM, (sljit_sw)ljit_icall_inherit);

    /* INHERIT 不产生新值写入 R[A]（只是设置 __parent 字段），不需要重载寄存器 */
}

void ljit_cg_emit_getsuper(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    int tvalue_size = sizeof(TValue);
    int ra = node->dest.v.reg;
    int rb_reg = node->src1.v.reg;
    int k_idx = node->src2.v.i;

    JIT_DBG(MOD_CG_OOP, "GETSUPER native: pc=%d, dest=R%d, obj=R%d, k_idx=%d",
        node->original_pc, ra, rb_reg, k_idx);

    /* R0 = L (lua_State 指针) */
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);

    /* R1 = rb 地址 (对象 R[B]，S0 + rb_reg * sizeof(TValue)) */
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R1, 0, SLJIT_S0, 0,
                   SLJIT_IMM, (sljit_sw)(rb_reg * tvalue_size));

    /* R2 = key (TString*) 从 proto->k[K[C]] 加载 */
    TValue *kv = &ctx->proto->k[k_idx];
    TString *ts = tsvalue(kv);
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, (sljit_sw)ts);

    /* R3 = ra 地址 (目标寄存器 R[A]，S0 + ra * sizeof(TValue)) */
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R3, 0, SLJIT_S0, 0,
                   SLJIT_IMM, (sljit_sw)(ra * tvalue_size));

    /* 调用 C 函数 ljit_icall_getsuper(L, rb, key, ra) */
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4V(W, W, W, W),
                     SLJIT_IMM, (sljit_sw)ljit_icall_getsuper);

    /*
     * 调用后目标寄存器 ra 在物理寄存器中的值已失效
     * 需要从栈槽重新加载到物理寄存器
     */
    if (!node->dest.is_spilled) {
        sljit_emit_op1(compiler, SLJIT_MOV, node->dest.phys_reg, 0,
                       SLJIT_MEM1(SLJIT_S0), ra * tvalue_size);
    }
}
