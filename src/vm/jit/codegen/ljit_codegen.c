#include "ljit_codegen.h"
#include "../core/ljit_internal.h"
#include "../ir/ljit_ir.h"
#include "../sljit/ljit_sljit.h"
#include <stdlib.h>
#include "../../../core/lstate.h"
#include "../../../core/ltable.h"
#include "../../../vm/lvm.h"
#include "../../../core/lobject.h"
#include "../../../core/lgc.h"
#include "../../../core/ltm.h"
#include "../../../core/lfunc.h"
#include "../../../core/ldo.h"
#include <string.h>

void SLJIT_FUNC ljit_icall_gettable(lua_State *L, StkId ra, TValue *rb, TValue *rc) {
    if (ttistable(rb)) {
       Table *h = hvalue(rb);
       if (h->is_shared) l_rwlock_rdlock(&h->lock);
       const TValue *res = luaH_get_optimized(h, rc);
       if (!isempty(res)) {
          setobj2s(L, ra, res);
          if (h->is_shared) l_rwlock_unlock(&h->lock);
       } else {
          if (h->is_shared) l_rwlock_unlock(&h->lock);
          luaV_finishget(L, rb, rc, ra, NULL);
       }
    }
    else {
      luaV_finishget(L, rb, rc, ra, NULL);
    }
}

void SLJIT_FUNC ljit_icall_settable(lua_State *L, TValue *ra, TValue *rb, TValue *rc) {
    if (ttistable(ra)) {
       Table *h = hvalue(ra);
       if (h->is_shared) l_rwlock_wrlock(&h->lock);
       const TValue *res = luaH_get_optimized(h, rb);
       if (!isempty(res) && !isabstkey(res)) {
          setobj2t(L, cast(TValue *, res), rc);
          luaC_barrierback(L, obj2gco(h), rc);
          if (h->is_shared) l_rwlock_unlock(&h->lock);
       } else {
          if (h->is_shared) l_rwlock_unlock(&h->lock);
          luaV_finishset(L, ra, rb, rc, NULL);
       }
    }
    else {
      luaV_finishset(L, ra, rb, rc, NULL);
    }
}

void SLJIT_FUNC ljit_icall_geti(lua_State *L, StkId ra, TValue *rb, int c) {
    if (!rb) return;
    if (ttistable(rb)) {
        Table *h = hvalue(rb);
        if (h->is_shared) l_rwlock_rdlock(&h->lock);
        const TValue *res = luaH_getint(h, c);
        if (!isempty(res)) {
            setobj2s(L, ra, res);
            if (h->is_shared) l_rwlock_unlock(&h->lock);
        } else {
            if (h->is_shared) l_rwlock_unlock(&h->lock);
            TValue key;
            setivalue(&key, c);
            luaV_finishget(L, rb, &key, ra, NULL);
        }
    } else {
        TValue key;
        setivalue(&key, c);
        luaV_finishget(L, rb, &key, ra, NULL);
    }
}

void SLJIT_FUNC ljit_icall_seti(lua_State *L, StkId ra, int c, TValue *rc) {
    if (!rc || !ra) return;
    if (ttistable(s2v(ra))) {
        Table *h = hvalue(s2v(ra));
        if (h->is_shared) l_rwlock_wrlock(&h->lock);
        const TValue *res = luaH_getint(h, c);
        if (!isempty(res) && !isabstkey(res)) {
            setobj2t(L, cast(TValue *, res), rc);
            luaC_barrierback(L, obj2gco(h), rc);
            if (h->is_shared) l_rwlock_unlock(&h->lock);
        } else {
            if (h->is_shared) l_rwlock_unlock(&h->lock);
            TValue key;
            setivalue(&key, c);
            luaV_finishset(L, s2v(ra), &key, rc, NULL);
        }
    } else {
        TValue key;
        setivalue(&key, c);
        luaV_finishset(L, s2v(ra), &key, rc, NULL);
    }
}

void SLJIT_FUNC ljit_icall_getfield(lua_State *L, StkId ra, TValue *rb, TValue *rc) {
    if (!rb || !rc) return;
    TString *key = tsvalue(rc);
    if (ttistable(rb)) {
        Table *h = hvalue(rb);
        if (h->is_shared) l_rwlock_rdlock(&h->lock);
        const TValue *res = luaH_getshortstr(h, key);
        if (!isempty(res)) {
            setobj2s(L, ra, res);
            if (h->is_shared) l_rwlock_unlock(&h->lock);
        } else {
            if (h->is_shared) l_rwlock_unlock(&h->lock);
            luaV_finishget(L, rb, rc, ra, NULL);
        }
    } else {
        luaV_finishget(L, rb, rc, ra, NULL);
    }
}

void SLJIT_FUNC ljit_icall_setfield(lua_State *L, StkId ra, TValue *rb, TValue *rc) {
    if (!ra || !rb || !rc) return;
    TString *key = tsvalue(rb);
    if (ttistable(s2v(ra))) {
        Table *h = hvalue(s2v(ra));
        if (h->is_shared) l_rwlock_wrlock(&h->lock);
        const TValue *res = luaH_getshortstr(h, key);
        if (!isempty(res) && !isabstkey(res)) {
            setobj2t(L, cast(TValue *, res), rc);
            luaC_barrierback(L, obj2gco(h), rc);
            if (h->is_shared) l_rwlock_unlock(&h->lock);
        } else {
            if (h->is_shared) l_rwlock_unlock(&h->lock);
            luaV_finishset(L, s2v(ra), rb, rc, NULL);
        }
    } else {
        luaV_finishset(L, s2v(ra), rb, rc, NULL);
    }
}

void SLJIT_FUNC ljit_icall_getupval(lua_State *L, StkId ra, int b) {
    LClosure *cl = clLvalue(s2v(L->ci->func.p));
    setobj2s(L, ra, cl->upvals[b]->v.p);
}

void SLJIT_FUNC ljit_icall_setupval(lua_State *L, StkId ra, int b) {
    LClosure *cl = clLvalue(s2v(L->ci->func.p));
    UpVal *uv = cl->upvals[b];
    setobj(L, uv->v.p, s2v(ra));
    luaC_barrier(L, uv, s2v(ra));
}

void SLJIT_FUNC ljit_icall_gettabup(lua_State *L, StkId ra, int upval_idx, TValue *rc) {
    if (!rc) return;
    LClosure *cl = clLvalue(s2v(L->ci->func.p));
    TValue *upval = cl->upvals[upval_idx]->v.p;
    TString *key = tsvalue(rc);
    if (ttistable(upval)) {
        Table *h = hvalue(upval);
        if (h->is_shared) l_rwlock_rdlock(&h->lock);
        const TValue *res = luaH_getshortstr(h, key);
        if (!isempty(res)) {
            setobj2s(L, ra, res);
            if (h->is_shared) l_rwlock_unlock(&h->lock);
        } else {
            if (h->is_shared) l_rwlock_unlock(&h->lock);
            luaV_finishget(L, upval, rc, ra, NULL);
        }
    } else {
        luaV_finishget(L, upval, rc, ra, NULL);
    }
}

void SLJIT_FUNC ljit_icall_settabup(lua_State *L, int upval_idx, TValue *rb, TValue *rc) {
    if (!rb || !rc) return;
    LClosure *cl = clLvalue(s2v(L->ci->func.p));
    TValue *upval = cl->upvals[upval_idx]->v.p;
    TString *key = tsvalue(rb);
    if (ttistable(upval)) {
        Table *h = hvalue(upval);
        if (h->is_shared) l_rwlock_wrlock(&h->lock);
        const TValue *res = luaH_getshortstr(h, key);
        if (!isempty(res) && !isabstkey(res)) {
            setobj2t(L, cast(TValue *, res), rc);
            luaC_barrierback(L, obj2gco(h), rc);
            if (h->is_shared) l_rwlock_unlock(&h->lock);
        } else {
            if (h->is_shared) l_rwlock_unlock(&h->lock);
            luaV_finishset(L, upval, rb, rc, NULL);
        }
    } else {
        luaV_finishset(L, upval, rb, rc, NULL);
    }
}

void SLJIT_FUNC ljit_icall_newtable(lua_State *L, int b, int c, StkId ra) {
    Table *t = luaH_new(L);
    sethvalue2s(L, ra, t);
    if (b != 0 || c != 0)
        luaH_resize(L, t, c, b);
}

#include <math.h>

void SLJIT_FUNC ljit_icall_pow(lua_State *L, TValue *ra, TValue *rb, TValue *rc) {
    lua_Number nb, nc;
    if (tonumberns(rb, nb) && tonumberns(rc, nc)) {
        setfltvalue(ra, luai_numpow(L, nb, nc));
    } else {
        luaT_trybinTM(L, rb, rc, cast(StkId, ra), TM_POW);
    }
}

void SLJIT_FUNC ljit_icall_concat(lua_State *L, int total, StkId ra) {
    L->top.p = ra + total;
    luaV_concat(L, total);
}

void SLJIT_FUNC ljit_icall_setlist(lua_State *L, StkId ra, int n, int last) {
    Table *h = hvalue(s2v(ra));
    if (n == 0)
        n = cast_int(L->top.p - ra) - 1;
    else
        L->top.p = L->ci->top.p;
    last += n;
    if (last > luaH_realasize(h))
        luaH_resizearray(L, h, last);
    for (; n > 0; n--) {
        TValue *val = s2v(ra + n);
        setobj2t(L, &h->array[last - 1], val);
        last--;
        luaC_barrierback(L, obj2gco(h), val);
    }
}

int SLJIT_FUNC ljit_icall_testset(lua_State *L, StkId ra, TValue *rb, int k) {
    if (l_isfalse(rb) == k) {
        return 1;
    } else {
        setobj2s(L, ra, rb);
        return 0;
    }
}

void SLJIT_FUNC ljit_icall_self(lua_State *L, StkId ra, TValue *rb, TValue *rc) {
    TString *key = tsvalue(rc);
    setobj2s(L, ra + 1, rb);
    if (ttistable(rb)) {
        Table *h = hvalue(rb);
        if (h->is_shared) l_rwlock_rdlock(&h->lock);
        const TValue *res;
        if (key->tt == LUA_VSHRSTR)
            res = luaH_getshortstr(h, key);
        else
            res = luaH_getstr(h, key);
        if (!isempty(res)) {
            setobj2s(L, ra, res);
            if (h->is_shared) l_rwlock_unlock(&h->lock);
        } else {
            if (h->is_shared) l_rwlock_unlock(&h->lock);
            luaV_finishget(L, rb, rc, ra, NULL);
        }
    } else {
        luaV_finishget(L, rb, rc, ra, NULL);
    }
}

void SLJIT_FUNC ljit_icall_close(lua_State *L, StkId ra) {
    luaF_close(L, ra, LUA_OK, 1);
}

void SLJIT_FUNC ljit_icall_tbc(lua_State *L, StkId ra) {
    luaF_newtbcupval(L, ra);
}

int SLJIT_FUNC ljit_icall_eqk(lua_State *L, StkId ra, TValue *rb, int k) {
    int cond = luaV_equalobj(NULL, s2v(ra), rb);
    if (cond != k) return 1;
    else return 0;
}

int SLJIT_FUNC ljit_icall_test(lua_State *L, StkId ra, int k) {
    int cond = !l_isfalse(s2v(ra));
    if (cond != k) return 1;
    else return 0;
}

int SLJIT_FUNC ljit_icall_compare(lua_State *L, TValue *a, TValue *b, int op_k) {
    int opcode = op_k >> 1;
    int k = op_k & 1;
    int cond;
    switch (opcode) {
        case IR_CMP_EQ: cond = luaV_equalobj(L, a, b); break;
        case IR_CMP_LT: cond = luaV_lessthan(L, a, b); break;
        case IR_CMP_LE: cond = luaV_lessequal(L, a, b); break;
        case IR_CMP_GT: cond = luaV_lessthan(L, b, a); break;
        case IR_CMP_GE: cond = luaV_lessequal(L, b, a); break;
        default: cond = 0; break;
    }
    return (cond != k) ? 1 : 0;
}

sljit_sw SLJIT_FUNC ljit_icall_forprep(lua_State *L, StkId ra) {
    return luaV_forprep(L, ra);
}

sljit_sw SLJIT_FUNC ljit_icall_forloop(lua_State *L, StkId ra) {
    /* Same logic as lvm.c OP_FORLOOP */
    if (ttisinteger(s2v(ra + 2))) {
        lua_Unsigned count = l_castS2U(ivalue(s2v(ra + 1)));
        if (count > 0) {
            lua_Integer step = ivalue(s2v(ra + 2));
            lua_Integer idx = ivalue(s2v(ra));
            chgivalue(s2v(ra + 1), count - 1);
            idx = intop(+, idx, step);
            chgivalue(s2v(ra), idx);
            setivalue(s2v(ra + 3), idx);
            return 1; /* Jump back */
        }
        return 0; /* Finish loop */
    } else {
        return luaV_floatforloop(ra);
    }
}

sljit_sw SLJIT_FUNC ljit_icall_tforprep(lua_State *L, StkId ra) {
    if (ttistable(s2v(ra)) && l_likely(!fasttm(L, hvalue(s2v(ra))->metatable, TM_CALL))) {
        setobjs2s(L, ra + 1, ra);
        setfvalue(s2v(ra), luaB_next);
    }
    luaF_newtbcupval(L, ra + 3);
    return 1;
}

void SLJIT_FUNC ljit_icall_tforcall(lua_State *L, StkId ra, int c) {
    memcpy(ra + 4, ra, 3 * sizeof(*ra));
    L->top.p = ra + 4 + 3;
    luaD_call(L, ra + 4, c);
}

sljit_sw SLJIT_FUNC ljit_icall_tforloop(lua_State *L, StkId ra) {
    if (!ttisnil(s2v(ra + 4))) {
        setobjs2s(L, ra + 2, ra + 4);
        return 1;
    }
    return 0;
}


void SLJIT_FUNC ljit_icall_len(lua_State *L, StkId ra, TValue *rb) {
    luaV_objlen(L, ra, rb);
}

void SLJIT_FUNC ljit_icall_call(lua_State *L, StkId func, int nargs, int nresults) {
    L->top.p = func + 1 + nargs;
    luaD_call(L, func, nresults);
}

void SLJIT_FUNC ljit_icall_ret(lua_State *L, StkId ra, int nresults) {
    CallInfo *ci = L->ci;
    if (nresults < 0)
        nresults = (int)(L->top.p - ra);
    L->top.p = ra + nresults;
    luaD_poscall(L, ci, nresults);
}

StkId SLJIT_FUNC ljit_icall_reload_base(lua_State *L) {
    return L->ci->func.p + 1;
}



#include "../../../core/lopcodes.h"


void SLJIT_FUNC ljit_icall_set_integer(StkId ra, lua_Integer v) {
    setivalue(s2v(ra), v);
}

void SLJIT_FUNC ljit_icall_set_number(StkId ra, lua_Number v) {
    setfltvalue(s2v(ra), v);
}

void SLJIT_FUNC ljit_icall_set_nil(StkId ra) {
    setnilvalue(s2v(ra));
}

void SLJIT_FUNC ljit_icall_set_bool(StkId ra, int v) {
    if (v)
        setbtvalue(s2v(ra));
    else
        setbfvalue(s2v(ra));
}


void ljit_cg_emit_len(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    int tvalue_size = sizeof(TValue);

    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R1, 0, SLJIT_S0, 0, SLJIT_IMM, node->dest.stack_ofs);
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R2, 0, SLJIT_S0, 0, SLJIT_IMM, node->src1.stack_ofs);

    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3V(W, W, W), SLJIT_IMM, (sljit_sw)ljit_icall_len);

    if (!node->dest.is_spilled) {
        sljit_emit_op1(compiler, SLJIT_MOV, node->dest.phys_reg, 0, SLJIT_MEM1(SLJIT_S0), node->dest.stack_ofs);
    }
}

/*
 * 快速 JIT 调度: 绕过 luaD_call/ccall/luaD_precall/luaV_execute 四条链,
 * 直接设置 CallInfo 并调用目标闭包的 jit_func.
 * 仅当目标 Lua 闭包已被 JIT 编译 (p->jit_trace != NULL) 时走快速路径,
 * 否则回退到标准 luaD_call.
 */
void SLJIT_FUNC ljit_fast_dispatch(lua_State *L, StkId func, int nresults) {
    TValue *fv = s2v(func);

    if (ttypetag(fv) == LUA_VLCL) {
        LClosure *cl = clLvalue(fv);
        Proto *p = cl->p;

        if (XCLUA_JIT_ENABLED && p->jit_trace) {
            int fsize = p->maxstacksize;
            int narg = cast_int(L->top.p - func) - 1;
            int nfixparams = p->numparams;

            checkstackGCp(L, fsize, func);

            L->nCcalls++;
            if (l_unlikely(getCcalls(L) >= LUAI_MAXCCALLS)) {
                checkstackp(L, 0, func);
                luaE_checkcstack(L);
            }

            CallInfo *ci = L->ci->next ? L->ci->next : luaE_extendCI(L);
            L->ci = ci;
            ci->func.p = func;
            ci->nresults = nresults;
            ci->callstatus = CIST_FRESH;
            ci->top.p = func + 1 + fsize;
            ci->u.l.savedpc = p->code;

            for (; narg < nfixparams; narg++)
                setnilvalue(s2v(L->top.p++));

            lua_assert(ci->top.p <= L->stack_last.p);

            typedef int (*jit_func_t)(StkId);
            jit_func_t jit = (jit_func_t)p->jit_trace;
            StkId base = func + 1;

            int jit_done = jit(base);

            L->nCcalls--;

            if (jit_done) {
                return;
            }

            L->top.p = func + 1 + narg;
            L->ci = ci->previous;
        }
    }

    luaD_call(L, func, nresults);
}

void SLJIT_FUNC ljit_jitcall(lua_State *L, StkId func, int nresults, Proto *p) {
    int fsize = p->maxstacksize;
    int narg = cast_int(L->top.p - func) - 1;
    int nfixparams = p->numparams;

    checkstackGCp(L, fsize, func);

    L->nCcalls++;
    if (l_unlikely(getCcalls(L) >= LUAI_MAXCCALLS)) {
        checkstackp(L, 0, func);
        luaE_checkcstack(L);
    }

    CallInfo *ci = L->ci->next ? L->ci->next : luaE_extendCI(L);
    L->ci = ci;
    ci->func.p = func;
    ci->nresults = nresults;
    ci->callstatus = CIST_FRESH;
    ci->top.p = func + 1 + fsize;
    ci->u.l.savedpc = p->code;

    for (; narg < nfixparams; narg++)
        setnilvalue(s2v(L->top.p++));

    lua_assert(ci->top.p <= L->stack_last.p);

    typedef int (*jit_func_t)(StkId);
    jit_func_t jit = (jit_func_t)p->jit_trace;
    StkId base = func + 1;

    int jit_done = jit(base);

    L->nCcalls--;

    if (jit_done) {
        return;
    }

    L->top.p = func + 1 + narg;
    L->ci = ci->previous;

    luaD_call(L, func, nresults);
}

void *ljit_codegen(void *ctx_ptr) {
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    if (!ctx) return NULL;

    struct sljit_compiler *compiler = sljit_create_compiler(NULL);
    if (!compiler) return NULL;

    ctx->compiler = compiler;
    int max_labels = ctx->proto->sizecode + ctx->next_label_id + 1;
    ctx->labels = (struct sljit_label **)calloc(max_labels, sizeof(struct sljit_label *));

    ctx->jumps = (struct sljit_jump **)calloc(max_labels, sizeof(struct sljit_jump *));
    ctx->jump_targets = (int *)calloc(max_labels, sizeof(int));
    ctx->num_jumps = 0;

    /*
     * Enter function arguments mapping:
     * jit_func_t(StkId base) -> SLJIT_ARGS1(32, W) -> base in SLJIT_S0.
     * Returns int: 1 = fully handled (CALL+RET executed), 0 = interpreter fallback.
     * SLJIT_S0 holds the Lua virtual register base address.
     * SLJIT_S1 serves as the return flag (0 = fallback, 1 = done).
     * Requesting 6 saved regs (S0-S5), 5 scratch regs (R0-R4), 0 fregs.
     */
    sljit_emit_enter(compiler, 0, SLJIT_ARGS1(32, W), 5, 6, 0);
sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_S1, 0, SLJIT_IMM, 0);

    /*
     * 加载live-in参数: 扫描IR, 找出作为src使用但从未作为dest定义过的虚拟寄存器,
     * 将这些"活入"参数从Lua栈加载到分配的物理寄存器中.
     * spilled寄存器不需要显式加载, 因为stack_ofs = reg*sizeof(TValue)直接指向Lua栈内存.
     */
    {
        int num_vregs = ctx->proto->maxstacksize;
        int tvalue_size = sizeof(TValue);

        /*
         * 第一遍: 记录每个寄存器的首次定义和首次使用的IR序列号.
         * 序列号反映IR指令的执行顺序, 用于判断"先使用后定义"(需要live-in加载).
         */
        int sentinel = ctx->proto->sizecode + 1;
        int *first_def = (int *)malloc(num_vregs * sizeof(int));
        int *first_use = (int *)malloc(num_vregs * sizeof(int));
        for (int i = 0; i < num_vregs; i++) {
            first_def[i] = sentinel;
            first_use[i] = sentinel;
        }

        int seq = 0;
        ljit_ir_node_t *scan = ctx->ir_head;
        while (scan) {
            if (scan->dest.type == IR_VAL_REG) {
                int r = scan->dest.v.reg;
                if (r >= 0 && r < num_vregs && seq < first_def[r])
                    first_def[r] = seq;
            }
            if (scan->src1.type == IR_VAL_REG) {
                int r = scan->src1.v.reg;
                if (r >= 0 && r < num_vregs && seq < first_use[r])
                    first_use[r] = seq;
            }
            if (scan->src2.type == IR_VAL_REG) {
                int r = scan->src2.v.reg;
                if (r >= 0 && r < num_vregs && seq < first_use[r])
                    first_use[r] = seq;
            }
            seq++;
            scan = scan->next;
        }

        /*
         * 第二遍: 加载live-in寄存器.
         * first_use < first_def 表示该寄存器在首次定义前就被使用(或者从未被定义).
         * spilled寄存器直接通过Lua栈访问(stack_ofs = reg*sizeof(TValue)), 无需显式加载.
         */
        int *loaded = (int *)calloc(num_vregs, sizeof(int));
        scan = ctx->ir_head;
        while (scan) {
            if (scan->src1.type == IR_VAL_REG) {
                int r = scan->src1.v.reg;
                if (r >= 0 && r < num_vregs && first_use[r] < first_def[r] && !loaded[r]) {
                    loaded[r] = 1;
                    if (!scan->src1.is_spilled) {
                        sljit_emit_op1(compiler, SLJIT_MOV, scan->src1.phys_reg, 0,
                            SLJIT_MEM1(SLJIT_S0), r * tvalue_size);
                    }
                }
            }
            if (scan->src2.type == IR_VAL_REG) {
                int r = scan->src2.v.reg;
                if (r >= 0 && r < num_vregs && first_use[r] < first_def[r] && !loaded[r]) {
                    loaded[r] = 1;
                    if (!scan->src2.is_spilled) {
                        sljit_emit_op1(compiler, SLJIT_MOV, scan->src2.phys_reg, 0,
                            SLJIT_MEM1(SLJIT_S0), r * tvalue_size);
                    }
                }
            }
            scan = scan->next;
        }

        free(first_def);
        free(first_use);
        free(loaded);
    }

    ljit_ir_node_t *node = ctx->ir_head;
    while (node) {
        if (node->original_pc >= 0 && node->original_pc < max_labels) {
            if (!ctx->labels[node->original_pc]) {
                ctx->labels[node->original_pc] = sljit_emit_label(compiler);
            }
        }

        switch (node->op) {
            case IR_ADD: ljit_cg_emit_add(node, ctx); break;
            case IR_SUB: ljit_cg_emit_sub(node, ctx); break;
            case IR_MUL: ljit_cg_emit_mul(node, ctx); break;
            case IR_DIV: ljit_cg_emit_div(node, ctx); break;
            case IR_MOD: ljit_cg_emit_mod(node, ctx); break;
            case IR_BAND: ljit_cg_emit_band(node, ctx); break;
            case IR_BOR: ljit_cg_emit_bor(node, ctx); break;
            case IR_BXOR: ljit_cg_emit_bxor(node, ctx); break;
            case IR_SHL: ljit_cg_emit_shl(node, ctx); break;
            case IR_SHR: ljit_cg_emit_shr(node, ctx); break;
            case IR_BNOT: ljit_cg_emit_bnot(node, ctx); break;
            case IR_MOV: ljit_cg_emit_mov(node, ctx); break;
            case IR_LOADI: ljit_cg_emit_loadi(node, ctx); break;
            case IR_LOADF: ljit_cg_emit_loadf(node, ctx); break;
            case IR_LOADK: ljit_cg_emit_loadk(node, ctx); break;
            case IR_LOADNIL: ljit_cg_emit_loadnil(node, ctx); break;
            case IR_LOADBOOL: ljit_cg_emit_loadbool(node, ctx); break;
            case IR_IDIV: ljit_cg_emit_idiv(node, ctx); break;
            case IR_UNM: ljit_cg_emit_unm(node, ctx); break;
            case IR_NOT: ljit_cg_emit_not(node, ctx); break;
            case IR_JMP: ljit_cg_emit_jmp(node, ctx); break;
            case IR_CMP_LT:
            case IR_CMP_LE:
            case IR_CMP_EQ:
            case IR_CMP_GT:
            case IR_CMP_GE: ljit_cg_emit_cmp(node, ctx); break;
            case IR_RET: {
                int tvalue_size = sizeof(TValue);
                int nresults = node->src2.v.i;

                /* R0 = L */
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
                /* R1 = ra = base + src1.v.reg * tvalue_size */
                sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R1, 0, SLJIT_S0, 0,
                               SLJIT_IMM, (sljit_sw)(node->src1.v.reg * tvalue_size));

                if (nresults >= 0) {
                    /*
                     * 内联返回路径: L->top.p = ra + nresults, 直接调 luaD_poscall.
                     * 省去 ljit_icall_ret 的 C 函数包装调用.
                     */
                    int top_offset = nresults * tvalue_size;

                    /* R2 = ci = L->ci */
                    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0,
                                   SLJIT_MEM1(SLJIT_R0),
                                   (sljit_sw)offsetof(lua_State, ci));

                    /* L->top.p = ra + nresults */
                    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R3, 0,
                                   SLJIT_R1, 0, SLJIT_IMM, (sljit_sw)top_offset);
                    sljit_emit_op1(compiler, SLJIT_MOV,
                                   SLJIT_MEM1(SLJIT_R0),
                                   (sljit_sw)offsetof(lua_State, top), SLJIT_R3, 0);

                    /* R1 = ci, R2 = nresults */
                    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_R2, 0);
                    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0,
                                   SLJIT_IMM, (sljit_sw)nresults);

                    /* luaD_poscall(L, ci, nresults) */
                    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3V(W, W, 32),
                                     SLJIT_IMM, (sljit_sw)luaD_poscall);
                } else {
                    /* nresults < 0 (LUA_MULTRET): 保留原包装调用 */
                    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0,
                                   SLJIT_IMM, (sljit_sw)nresults);
                    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3V(W, W, 32),
                                     SLJIT_IMM, (sljit_sw)ljit_icall_ret);
                }

                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_S1, 0, SLJIT_IMM, 1);
                sljit_emit_return(compiler, SLJIT_MOV32, SLJIT_S1, 0);
                break;
            }
            case IR_GETTABLE: ljit_cg_emit_gettable(node, ctx); break;
            case IR_SETTABLE: ljit_cg_emit_settable(node, ctx); break;
            case IR_CALL: {
                int tvalue_size = sizeof(TValue);
                int nargs = node->src1.v.i;
                int nresults = node->src2.v.i;

                /* R0 = L */
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
                /* R1 = func = base + dest.v.reg * tvalue_size */
                sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R1, 0, SLJIT_S0, 0,
                               SLJIT_IMM, (sljit_sw)(node->dest.v.reg * tvalue_size));

                /* L->top.p = func + (nargs+1) * tvalue_size */
                sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R2, 0, SLJIT_R1, 0,
                               SLJIT_IMM, (sljit_sw)((nargs + 1) * tvalue_size));
                sljit_emit_op1(compiler, SLJIT_MOV,
                               SLJIT_MEM1(SLJIT_R0),
                               (sljit_sw)offsetof(lua_State, top), SLJIT_R2, 0);

                /*
                 * 内联类型/JIT检查: 在生成的机器码中直接检查
                 * func->tt_ == LUA_VLCL 和 cl->p->jit_trace != NULL,
                 * 减少 C 函数分派开销.
                 * TValue.tt_ 偏移 = sizeof(Value) = 8.
                 * LUA_VLCL = makevariant(LUA_TFUNCTION, 0) = 6.
                 */
                sljit_emit_op1(compiler, SLJIT_MOV32, SLJIT_R3, 0,
                               SLJIT_MEM1(SLJIT_R1), (sljit_sw)sizeof(Value));
                struct sljit_jump *jmp_not_lcl = sljit_emit_cmp(compiler, SLJIT_NOT_EQUAL,
                    SLJIT_R3, 0, SLJIT_IMM, (sljit_sw)LUA_VLCL);

                /* value_.gc 在 TValue 偏移 0 → LClosure* (GCObject==Closure union 起始) */
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R3, 0,
                               SLJIT_MEM1(SLJIT_R1), 0);
                /* cl->p → Proto* */
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R3, 0,
                               SLJIT_MEM1(SLJIT_R3), (sljit_sw)offsetof(LClosure, p));
                /* p->jit_trace 非空检查 */
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R4, 0,
                               SLJIT_MEM1(SLJIT_R3), (sljit_sw)offsetof(Proto, jit_trace));
                struct sljit_jump *jmp_no_jit = sljit_emit_cmp(compiler, SLJIT_EQUAL,
                    SLJIT_R4, 0, SLJIT_IMM, 0);

                /*
                 * 快速路径: 目标已JIT编译.
                 * ljit_jitcall(L, func, nresults, p): R0=L, R1=func, R2=nresults, R3=p.
                 */
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, (sljit_sw)nresults);
                sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4V(W, W, 32, W),
                                 SLJIT_IMM, (sljit_sw)ljit_jitcall);

                struct sljit_jump *jmp_after = sljit_emit_jump(compiler, SLJIT_JUMP);

                /*
                 * 慢速路径: 目标不是LCL或没有JIT代码, 回退到 luaD_call.
                 */
                struct sljit_label *slow_label = sljit_emit_label(compiler);
                sljit_set_label(jmp_not_lcl, slow_label);
                sljit_set_label(jmp_no_jit, slow_label);

                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, (sljit_sw)nresults);
                sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3V(W, W, 32),
                                 SLJIT_IMM, (sljit_sw)luaD_call);

                /* 两条路径汇总 */
                struct sljit_label *after_label = sljit_emit_label(compiler);
                sljit_set_label(jmp_after, after_label);

                /*
                 * 内联 reload base: S0 = L->ci->func.p + 1
                 * 直接从 L->ci 链读取, 省去 C 函数调用开销.
                 */
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, (sljit_sw)ctx->L);
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0,
                               SLJIT_MEM1(SLJIT_R2), offsetof(lua_State, ci));
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_S0, 0,
                               SLJIT_MEM1(SLJIT_R2), offsetof(CallInfo, func));
                sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_S0, 0,
                               SLJIT_S0, 0, SLJIT_IMM, sizeof(TValue));

                /*
                 * 调用后: Lua栈上的结果寄存器已被 luaD_poscall 更新,
                 * 但物理寄存器中的值已过时. 扫描后续IR节点找到结果寄存器的映射信息,
                 * 将非spilled的结果从Lua栈重新加载到物理寄存器.
                 */
                if (nresults > 0) {
                    int base_reg = node->dest.v.reg;
                    for (int res = 0; res < nresults; res++) {
                        int vreg = base_reg + res;
                        ljit_ir_node_t *next = node->next;
                        int found = 0;
                        while (next && !found) {
                            if (next->dest.type == IR_VAL_REG && next->dest.v.reg == vreg) {
                                if (!next->dest.is_spilled) {
                                    sljit_emit_op1(compiler, SLJIT_MOV, next->dest.phys_reg, 0,
                                        SLJIT_MEM1(SLJIT_S0), vreg * tvalue_size);
                                }
                                found = 1;
                            } else if (next->src1.type == IR_VAL_REG && next->src1.v.reg == vreg) {
                                if (!next->src1.is_spilled) {
                                    sljit_emit_op1(compiler, SLJIT_MOV, next->src1.phys_reg, 0,
                                        SLJIT_MEM1(SLJIT_S0), vreg * tvalue_size);
                                }
                                found = 1;
                            } else if (next->src2.type == IR_VAL_REG && next->src2.v.reg == vreg) {
                                if (!next->src2.is_spilled) {
                                    sljit_emit_op1(compiler, SLJIT_MOV, next->src2.phys_reg, 0,
                                        SLJIT_MEM1(SLJIT_S0), vreg * tvalue_size);
                                }
                                found = 1;
                            }
                            next = next->next;
                        }
                    }
                }
                break;
            }
            case IR_NEWTABLE: ljit_cg_emit_newtable(node, ctx); break;
            case IR_POW: ljit_cg_emit_pow(node, ctx); break;
            case IR_NOP: ljit_cg_emit_nop(node, ctx); break;

            case IR_LEN: ljit_cg_emit_len(node, ctx); break;

            case IR_LOADKX: {
                node->src1.type = IR_VAL_CONST;
                ljit_cg_emit_loadk(node, ctx);
                break;
            }
            case IR_SELF: {
                int tvalue_size = sizeof(TValue);
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
                sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R1, 0, SLJIT_S0, 0, SLJIT_IMM, node->dest.v.reg * tvalue_size);
                sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R2, 0, SLJIT_S0, 0, SLJIT_IMM, node->src1.v.reg * tvalue_size);
                TValue *self_kv = &ctx->proto->k[node->src2.v.k];
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R3, 0, SLJIT_IMM, (sljit_sw)self_kv);
                sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4V(W, W, W, W), SLJIT_IMM, (sljit_sw)ljit_icall_self);
                break;
            }
            case IR_CLOSE: {
                int tvalue_size = sizeof(TValue);
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
                sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R1, 0, SLJIT_S0, 0, SLJIT_IMM, node->dest.v.reg * tvalue_size);
                sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS2V(W, W), SLJIT_IMM, (sljit_sw)ljit_icall_close);
                break;
            }
            case IR_TBC: {
                int tvalue_size = sizeof(TValue);
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
                sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R1, 0, SLJIT_S0, 0, SLJIT_IMM, node->dest.v.reg * tvalue_size);
                sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS2V(W, W), SLJIT_IMM, (sljit_sw)ljit_icall_tbc);
                break;
            }
            case IR_EQK: {
                int tvalue_size = sizeof(TValue);
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
                sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R1, 0, SLJIT_S0, 0, SLJIT_IMM, node->src1.v.reg * tvalue_size);
                TValue *eqk_kv = &ctx->proto->k[node->src2.v.k];
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, (sljit_sw)eqk_kv);
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R3, 0, SLJIT_IMM, node->dest.v.i);
                sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4(32, W, W, W, 32), SLJIT_IMM, (sljit_sw)ljit_icall_eqk);
                struct sljit_jump *eqk_skip = sljit_emit_cmp(compiler, SLJIT_NOT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, 0);
                ljit_ir_node_t *eqk_next = node->next;
                if (eqk_next && eqk_next->op == IR_JMP) {
                    ljit_cg_emit_jmp(eqk_next, ctx);
                    node = eqk_next;
                }
                struct sljit_label *eqk_after = sljit_emit_label(compiler);
                sljit_set_label(eqk_skip, eqk_after);
                break;
            }
            case IR_TEST: {
                int tvalue_size = sizeof(TValue);
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
                sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R1, 0, SLJIT_S0, 0, SLJIT_IMM, node->src1.v.reg * tvalue_size);
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, node->dest.v.i);
                sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3(32, W, W, 32), SLJIT_IMM, (sljit_sw)ljit_icall_test);
                struct sljit_jump *test_skip = sljit_emit_cmp(compiler, SLJIT_NOT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, 0);
                ljit_ir_node_t *test_next = node->next;
                if (test_next && test_next->op == IR_JMP) {
                    ljit_cg_emit_jmp(test_next, ctx);
                    node = test_next;
                }
                struct sljit_label *test_after = sljit_emit_label(compiler);
                sljit_set_label(test_skip, test_after);
                break;
            }

            case IR_ADDMETHOD:
            case IR_ASYNCWRAP:
            case IR_CASE:
            case IR_CHECKTYPE:
            case IR_ERRNNIL:
            case IR_EXTRAARG:
            case IR_GENERICWRAP:
            case IR_GETCMDS:
            case IR_GETOPS:
            case IR_GETPROP:
            case IR_GETVARG:
            case IR_IMPLEMENT:
            case IR_IN:
            case IR_INSTANCEOF:
            case IR_IS:
            case IR_LINKNAMESPACE:
            case IR_NEWCONCEPT:
            case IR_NEWNAMESPACE:
            case IR_NEWSUPER:
            case IR_SETIFACEFLAG:
            case IR_SETMETHOD:
            case IR_SETPROP:
            case IR_SETSTATIC:
            case IR_SETSUPER:
            case IR_SLICE:
            case IR_SPACESHIP:
            case IR_TESTNIL: break;
            case IR_TESTSET: {
                int tvalue_size = sizeof(TValue);
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
                sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R1, 0, SLJIT_S0, 0, SLJIT_IMM, node->dest.v.reg * tvalue_size);
                sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R2, 0, SLJIT_S0, 0, SLJIT_IMM, node->src1.v.reg * tvalue_size);
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R3, 0, SLJIT_IMM, node->src2.v.i);
                sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4(32, W, W, W, 32), SLJIT_IMM, (sljit_sw)ljit_icall_testset);
                struct sljit_jump *skip_jmp = sljit_emit_cmp(compiler, SLJIT_NOT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, 0);
                ljit_ir_node_t *next = node->next;
                if (next && next->op == IR_JMP) {
                    ljit_cg_emit_jmp(next, ctx);
                    node = next;
                }
                struct sljit_label *after_label = sljit_emit_label(compiler);
                sljit_set_label(skip_jmp, after_label);
                break;
            }
            case IR_GETUPVAL: ljit_cg_emit_getupval(node, ctx); break;
            case IR_SETUPVAL: ljit_cg_emit_setupval(node, ctx); break;
            case IR_GETTABUP: ljit_cg_emit_gettabup(node, ctx); break;
            case IR_SETTABUP: ljit_cg_emit_settabup(node, ctx); break;
            case IR_SETLIST: ljit_cg_emit_setlist(node, ctx); break;
            case IR_GETI: ljit_cg_emit_geti(node, ctx); break;
            case IR_SETI: ljit_cg_emit_seti(node, ctx); break;
            case IR_GETFIELD: ljit_cg_emit_getfield(node, ctx); break;
            case IR_SETFIELD: ljit_cg_emit_setfield(node, ctx); break;
            case IR_TFORPREP: ljit_cg_emit_tforprep(node, ctx); break;
            case IR_TFORCALL: ljit_cg_emit_tforcall(node, ctx); break;
            case IR_TFORLOOP: ljit_cg_emit_tforloop(node, ctx); break;
            case IR_VARARG:
            case IR_VARARGPREP:
                // Instead of sljit_emit_return_void, abort the whole JIT compilation.
                sljit_free_compiler((struct sljit_compiler *)ctx->compiler);
                ctx->compiler = NULL;
                return NULL;
            case IR_GETSUPER: ljit_cg_emit_getsuper(node, ctx); break;
            case IR_INHERIT: ljit_cg_emit_inherit(node, ctx); break;
            case IR_NEWCLASS: ljit_cg_emit_newclass(node, ctx); break;
            case IR_NEWOBJ: ljit_cg_emit_newobj(node, ctx); break;
            case IR_CLOSURE: ljit_cg_emit_closure(node, ctx); break;

            case IR_CONCAT: /* JIT no-op; interpreter handles concat to avoid stack corruption on re-exec */ break;
            case IR_FORPREP: ljit_cg_emit_forprep(node, ctx); break;
            case IR_FORLOOP: ljit_cg_emit_forloop(node, ctx); break;

            // Additional instructions can be mapped here as they are implemented
            default: break;
        }
        node = node->next;
    }

    /* Bind jumps to labels */
    for (int i = 0; i < ctx->num_jumps; i++) {
        if (ctx->jumps[i]) {
            int target = ctx->jump_targets[i];
            if (target >= 0 && target < max_labels && ctx->labels[target]) {
                sljit_set_label(ctx->jumps[i], ctx->labels[target]);
            }
        }
    }

    sljit_emit_return(compiler, SLJIT_MOV32, SLJIT_S1, 0);

    void *code = sljit_generate_code(compiler, 0, NULL);

    sljit_free_compiler(compiler);
    ctx->compiler = NULL;

    return code;
}

void SLJIT_FUNC ljit_icall_closure(lua_State *L, Proto *p, StkId base, StkId ra) {
    if (!ttisLclosure(s2v(L->ci->func.p))) { /* handle error */ }
    LClosure *cl = clLvalue(s2v(L->ci->func.p));
    L->top.p = ra + 1;
    luaV_pushclosure(L, p, cl->upvals, base, ra);
    luaC_step(L);
}

#include "../../../stdlib/lclass.h"
void SLJIT_FUNC ljit_icall_newclass(lua_State *L, TString *classname) {
    luaC_newclass(L, classname);
}

void SLJIT_FUNC ljit_icall_newobj(lua_State *L, int nargs, StkId rb, StkId ra) {
        setobj2s(L, L->top.p, s2v(rb));
    L->top.p++;
    for (int j = 0; j < nargs; j++) {
        setobj2s(L, L->top.p, s2v(ra + 1 + j));
        L->top.p++;
    }
    luaC_newobject(L, -(nargs + 1), nargs);
    setobj2s(L, ra, s2v(L->top.p - 1));
    L->top.p -= 1;
}

void SLJIT_FUNC ljit_icall_inherit(lua_State *L, StkId rb, StkId ra) {
        setobj2s(L, L->top.p, s2v(ra));
    L->top.p++;
    setobj2s(L, L->top.p, s2v(rb));
    L->top.p++;
    luaC_inherit(L, -2, -1);
    L->top.p -= 2;
}

void SLJIT_FUNC ljit_icall_getsuper(lua_State *L, TString *key, StkId rb, StkId ra) {
        setobj2s(L, L->top.p, s2v(rb));
    L->top.p++;
    luaC_super(L, -1, key);
    setobj2s(L, ra, s2v(L->top.p - 1));
    L->top.p -= 2;
}
