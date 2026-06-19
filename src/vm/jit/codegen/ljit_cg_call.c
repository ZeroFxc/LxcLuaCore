#include "ljit_codegen.h"
#include "../core/ljit_debug.h"
#include "../../../core/ldo.h"
#include "../../../vm/lvm.h"
#include "../ir/ljit_ir.h"
#include "../sljit/ljit_sljit.h"

/* 直接调用 JIT 编译的函数，跳过 luaD_call -> ccall -> luaV_execute 解释器链路 */
void SLJIT_FUNC ljit_icall_call(lua_State *L, StkId func, int nargs, int nresults) {
    if (nargs >= 0) {
        L->top.p = func + nargs + 1;
    }

    /* 检查被调函数是否已 JIT 编译 */
    TValue *f = s2v(func);
    if (ttisLclosure(f)) {
        Proto *p = clLvalue(f)->p;
        if (p->jit_trace) {
            L->nCcalls++;

            /* 快速路径：检测自递归调用（相同函数），跳过 luaD_precall 的完整开销 */
            CallInfo *caller_ci = L->ci;
            if (caller_ci != NULL) {
                TValue *caller_f = s2v(caller_ci->func.p);
                if (ttisLclosure(caller_f) && clLvalue(caller_f)->p == p) {
                    /* 自递归调用：轻量级帧设置，跳过 checkstackGCp、upvalue 检查、nil 填充等 */
                    int fsize = p->maxstacksize;
                    luaD_checkstack(L, fsize);  /* 只检查栈空间，不触发 GC */

                    CallInfo *ci = (L->ci->next ? L->ci->next : luaE_extendCI(L));
                    L->ci = ci;
                    ci->func.p = func;
                    ci->nresults = nresults;
                    ci->callstatus = CIST_FRESH;
                    ci->u.l.savedpc = p->code;
                    ci->top.p = func + 1 + fsize;
                    L->top.p = ci->top.p;

                    typedef int (*jit_func_t)(StkId);
                    jit_func_t jit_func = (jit_func_t)p->jit_trace;
                    StkId base = ci->func.p + 1;
                    int done = jit_func(base);
                    if (done) {
                        L->nCcalls--;
                        return;
                    }
                    /* JIT 回退，走解释器兜底 */
                    luaV_execute(L, ci);
                    L->nCcalls--;
                    return;
                }
            }

            /* 标准路径：非自递归调用，通过 luaD_precall 设置调用帧 */
            CallInfo *ci = luaD_precall(L, func, nresults);
            if (ci != NULL) {
                /* Lua 函数：直接调用 JIT 代码 */
                ci->callstatus = CIST_FRESH;
                typedef int (*jit_func_t)(StkId);
                jit_func_t jit_func = (jit_func_t)p->jit_trace;
                StkId base = ci->func.p + 1;
                int done = jit_func(base);
                if (done) {
                    L->nCcalls--;
                    return;  /* JIT 执行成功 */
                }
                /* JIT 回退，走解释器兜底 */
                luaV_execute(L, ci);
                L->nCcalls--;
                return;
            }
            /* C 函数，precallC 已执行完毕 */
            L->nCcalls--;
            return;
        }
    }

    /* 无 JIT 代码，走标准调用路径 */
    luaD_call(L, func, nresults);
}

void ljit_cg_emit_call(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    int tvalue_size = sizeof(TValue);

    JIT_DBG(MOD_CG_CALL, "CALL: pc=%d, func=R%d, nargs=%d, nresults=%d",
        node->original_pc, node->dest.v.reg, node->src1.v.i, node->src2.v.i);

    /* R0 = L */
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);

    /* R1 = func = base + dest.v.reg * size */
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R1, 0, SLJIT_S0, 0, SLJIT_IMM, node->dest.v.reg * tvalue_size);

    /* R2 = nargs (node->src1.v.i) */
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, node->src1.v.i);

    /* R3 = nresults (node->src2.v.i) */
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R3, 0, SLJIT_IMM, node->src2.v.i);

    /* Call ljit_icall_call */
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4V(W, W, W, W), SLJIT_IMM, (sljit_sw)ljit_icall_call);

    /* 调用后从Lua栈重新加载返回值到目标物理寄存器 */
    if (!node->dest.is_spilled && node->dest.phys_reg != 0) {
        sljit_emit_op1(compiler, SLJIT_MOV, node->dest.phys_reg, 0,
            SLJIT_MEM1(SLJIT_S0), node->dest.v.reg * tvalue_size);
    }
}
