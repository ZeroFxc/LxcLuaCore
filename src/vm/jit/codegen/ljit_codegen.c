#include "ljit_codegen.h"
#include "../core/ljit_internal.h"
#include "../core/ljit_debug.h"
#include "../ir/ljit_ir.h"
#include "../sljit/ljit_sljit.h"
#include <stdlib.h>
#include "../../../core/lstate.h"
#include "../../../core/ltable.h"
#include "../../../core/lmap.h"
#include "../../../vm/lvm.h"
#include "../../../core/lobject.h"
#include "../../../core/lgc.h"
#include "../../../core/ltm.h"
#include "../../../core/lfunc.h"
#include "../../../core/ldo.h"
#include "../../../core/ldebug.h"
#include "../../../stdlib/lclass.h"
#include "../../../core/lstring.h"
#include <string.h>

/* lvm_generic_call 声明 (lvm.c 中为 static，需改为非 static 以支持 JIT codegen) */
extern int lvm_generic_call(lua_State *L);

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

/* map容器icall函数 */
void SLJIT_FUNC ljit_icall_newmap(lua_State *L, StkId ra) {
    Map *m = luaM_newmap(L);
    setmapvalue2s(L, ra, m);
}

void SLJIT_FUNC ljit_icall_getmap(lua_State *L, StkId ra, TValue *rb, TValue *rc) {
    if (ttismap(rb)) {
        const TValue *val = luaM_getval(mapvalue(rb), rc);
        if (val != NULL) {
            setobj2s(L, ra, val);
        } else {
            setnilvalue(s2v(ra));
        }
    } else {
        /* 类型错误：预期map但收到其他类型 */
        luaG_typeerror(L, rb, "map");
    }
}

void SLJIT_FUNC ljit_icall_setmap(lua_State *L, TValue *ra, TValue *rb, TValue *rc) {
    if (ttismap(ra)) {
        luaM_setval(L, mapvalue(ra), rb, rc);
    } else {
        /* 类型错误：预期map但收到其他类型 */
        luaG_typeerror(L, ra, "map");
    }
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

/* 调试: 打印 FORLOOP 后的循环变量值 */
void SLJIT_FUNC ljit_debug_forloop(lua_State *L, StkId base, int ra) {
    lua_Integer internal_idx = ivalue(s2v(base + ra));        /* ra = 内部索引 */
    lua_Integer counter = ivalue(s2v(base + ra + 1));         /* ra+1 = 计数器 */
    lua_Integer step = ivalue(s2v(base + ra + 2));            /* ra+2 = 步长 */
    lua_Integer user_i = ivalue(s2v(base + ra + 3));          /* ra+3 = 用户可见 i */
    int tt_counter = ttype(s2v(base + ra + 1));
    int tt_step = ttype(s2v(base + ra + 2));
    JIT_DBG(MOD_DBG, "FORLOOP ra=%d: idx=%lld, counter=%lld(tt=%d), step=%lld(tt=%d), user_i=%lld",
        ra, (long long)internal_idx, (long long)counter, tt_counter,
        (long long)step, tt_step, (long long)user_i);
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

/* ljit_icall_call 已废弃，IR_CALL 的 codegen 在主循环中内联实现，
 * 使用 ljit_fast_dispatch/ljit_jitcall/ljit_jitcall_self 三条路径. */

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
        L->ci = ci->previous;
        return;
    }

    L->top.p = func + 1 + narg;
    L->ci = ci->previous;

    luaD_call(L, func, nresults);
}

/* 自递归调用计数器，用于性能诊断 */
int ljit_self_call_count = 0;

/*
 * 自递归调用轻量级帧设置：跳过 checkstackGCp（栈空间已知足够），
 * 跳过 upvalue 检查，仅做最小 CallInfo 分配和 nil 填充。
 * 相比 ljit_jitcall 减少了 checkstackGCp 的 GC 检查开销，
 * 对 fib(32) 等递归密集场景有显著加速效果。
 */
void SLJIT_FUNC ljit_jitcall_self(lua_State *L, StkId func, int nresults, Proto *p) {
    ljit_self_call_count++;
    int fsize = p->maxstacksize;
    int narg = cast_int(L->top.p - func) - 1;
    int nfixparams = p->numparams;

    /* 自递归：栈空间已由外层调用保证，跳过 checkstackGCp */

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
        /* 恢复调用者的 CallInfo, 使调用者 JIT 代码 reload base 时拿到正确的栈帧 */
        L->ci = ci->previous;
        return;
    }

    /* JIT 回退，走解释器兜底 */
    L->top.p = func + 1 + narg;
    L->ci = ci->previous;

    luaD_call(L, func, nresults);
}

/*
 * 递归返回栈操作：用于自递归 ijump 路径的返回地址管理。
 * rec_ret_stack 和 rec_ret_top 定义在 ljit_ir.h 的 ljit_ctx_t 中。
 */

/*
 * VARARG 原生辅助函数: 调用 luaT_getvarargs 将变长参数复制到栈上
 * @param L Lua 状态
 * @param ra 目标寄存器栈地址
 * @param wanted 需要的结果数量 (-1 表示全部)
 */
void SLJIT_FUNC ljit_icall_vararg(lua_State *L, StkId ra, int wanted) {
    CallInfo *ci = L->ci;
    luaT_getvarargs(L, ci, ra, wanted);
}

/*
 * VARARGPREP 原生辅助函数: 调整变长参数函数的栈帧，返回新的 base 指针
 * @param L Lua 状态
 * @param nfixparams 固定参数数量
 * @return 新的 base 指针 (ci->func.p + 1)
 */
StkId SLJIT_FUNC ljit_icall_varargprep(lua_State *L, int nfixparams) {
    CallInfo *ci = L->ci;
    if (ttisLclosure(s2v(ci->func.p))) {
        LClosure *cl = clLvalue(s2v(ci->func.p));
        luaT_adjustvarargs(L, nfixparams, ci, cl->p);
        return ci->func.p + 1;
    }
    return ci->func.p + 1;
}

/*
 * ASYNCWRAP 原生辅助函数: 在函数 Proto 上设置 PF_ASYNC 标志
 * @param L Lua 状态
 * @param rb 目标函数所在的栈地址
 */
void SLJIT_FUNC ljit_icall_asyncwrap(lua_State *L, StkId rb) {
    (void)L;
    if (ttisLclosure(s2v(rb))) {
        clLvalue(s2v(rb))->p->flag |= PF_ASYNC;
    }
}

/*
 * AWAIT 原生辅助函数: 处理 await 语义
 * 非 Promise 值直接复制到目标寄存器; Promise 值则挂起协程等待
 * @param L Lua 状态
 * @param ra 结果寄存器栈地址
 * @param await_val 待 await 的值
 */
void SLJIT_FUNC ljit_icall_await(lua_State *L, StkId ra, TValue *await_val) {
    if (ttisfulluserdata(await_val)) {
        /* Promise 值: 挂起协程，等待 Promise 完成 */
        CallInfo *ci = L->ci;
        LClosure *cl = clLvalue(s2v(ci->func.p));
        setobj2s(L, L->top.p, await_val);
        L->top.p++;
        ci->u.l.savedpc = cl->p->code;
        ci->callstatus |= CIST_AWAIT;
        L->status = LUA_YIELD;
        ci->u2.nyield = 1;
        luaD_throw(L, LUA_YIELD);
    } else {
        /* 普通值: 直接复制到目标寄存器 */
        setobj2s(L, ra, await_val);
    }
}

/*
 * GENERICWRAP 原生辅助函数: 创建泛型函数包装器
 * 参考 lvm.c OP_GENERICWRAP 实现
 * @param L Lua 状态
 * @param base 当前函数栈基址
 * @param a 目标寄存器索引 (RA)
 * @param b 源寄存器起始索引 (RB)
 */
void SLJIT_FUNC ljit_icall_genericwrap(lua_State *L, StkId base, int a, int b) {
    CallInfo *ci = L->ci;
    LClosure *cl = clLvalue(s2v(ci->func.p));

    while (L->top.p < base + cl->p->maxstacksize)
         setnilvalue(s2v(L->top.p++));
    luaD_checkstack(L, 5);
    base = ci->func.p + 1;  /* 栈可能已重新分配 */

    StkId base_args = base + b;

    /* 1. 创建 Closure */
    CClosure *ncl = luaF_newCclosure(L, 3);
    ncl->f = lvm_generic_call;

    base = ci->func.p + 1;  /* 栈可能已重新分配 */
    base_args = base + b;
    setobj(L, &ncl->upvalue[0], s2v(base_args));
    setobj(L, &ncl->upvalue[1], s2v(base_args + 1));
    setobj(L, &ncl->upvalue[2], s2v(base_args + 2));

    StkId ra = base + a;
    setclCvalue(L, s2v(ra), ncl);

    /* 2. 创建 Proxy Table */
    Table *proxy = luaH_new(L);
    base = ci->func.p + 1;
    ra = base + a;
    sethvalue2s(L, L->top.p, proxy);
    L->top.p++;

    /* 3. 创建 Metatable */
    Table *mt = luaH_new(L);
    base = ci->func.p + 1;
    ra = base + a;
    sethvalue2s(L, L->top.p, mt);
    L->top.p++;

    /* 链接: proxy.mt = mt */
    proxy->metatable = obj2gco(mt);

    /* 链接: mt.__call = ncl */
    setsvalue2s(L, L->top.p, luaS_newliteral(L, "__call"));
    L->top.p++;
    luaH_set(L, mt, s2v(L->top.p - 1), s2v(ra));
    L->top.p--;

    /* 链接: mt.__is_generic = true */
    setsvalue2s(L, L->top.p, luaS_newliteral(L, "__is_generic"));
    L->top.p++;
    TValue val_true;
    setbtvalue(&val_true);
    luaH_set(L, mt, s2v(L->top.p - 1), &val_true);
    L->top.p--;

    /* 移动 proxy 到 ra */
    setobj2s(L, ra, s2v(L->top.p - 2));

    /* 弹出 proxy 和 mt */
    L->top.p -= 2;

    luaC_checkGC(L);
}

/*
 * SETTRAITFLAG 原生辅助函数: 标记一个值为 trait
 * @param L Lua 状态
 * @param ra trait 值所在栈地址
 */
void SLJIT_FUNC ljit_icall_settraitflag(lua_State *L, StkId ra) {
    setobj2s(L, L->top.p, s2v(ra));
    L->top.p++;
    luaC_settraitflag(L, -1);
    L->top.p--;
}

/*
 * SETTRAITREQUIRE 原生辅助函数: 注册 trait 的必需方法
 * @param L Lua 状态
 * @param ra trait 值所在栈地址
 * @param method_name 方法名
 * @param nparams 参数个数
 */
void SLJIT_FUNC ljit_icall_settraitrequire(lua_State *L, StkId ra, TString *method_name, int nparams) {
    setobj2s(L, L->top.p, s2v(ra));
    L->top.p++;
    luaC_settraitrequire(L, -1, method_name, nparams);
    L->top.p--;
}

/*
 * USETRAIT 原生辅助函数: 将 trait 的方法复制到 class
 * @param L Lua 状态
 * @param ra class 值所在栈地址
 * @param rb trait 值所在栈地址
 */
void SLJIT_FUNC ljit_icall_usetrait(lua_State *L, StkId ra, StkId rb) {
    setobj2s(L, L->top.p, s2v(ra));
    L->top.p++;
    setobj2s(L, L->top.p, s2v(rb));
    L->top.p++;
    luaC_usetrait(L, -2, -1);
    L->top.p -= 2;
}

/*
 * 通用回退辅助函数: 从JIT代码中调用解释器执行当前函数.
 * 用于JIT代码遇到未支持操作码时的分级回退.
 * 返回后, JIT代码的调用者会根据L->ci状态判断是否已完成.
 */
void SLJIT_FUNC ljit_icall_fallback(lua_State *L, StkId base) {
    luaJIT_record_fallback();
    CallInfo *ci = L->ci;
    if (ci && ttisLclosure(s2v(ci->func.p))) {
        luaV_execute(L, ci);
    }
}

/* 推入返回地址到递归栈，返回新的栈顶索引，-1 表示栈溢出 */
int SLJIT_FUNC ljit_rec_push_ret(void *ctx_ptr, void *ret_addr) {
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    if (ctx->rec_ret_top >= MAX_REC_DEPTH) {
        return -1;
    }
    ctx->rec_ret_stack[ctx->rec_ret_top] = ret_addr;
    return ctx->rec_ret_top++;
}

/* 弹出返回地址，返回地址指针，NULL 表示栈空 */
void *SLJIT_FUNC ljit_rec_pop_ret(void *ctx_ptr) {
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    if (ctx->rec_ret_top <= 0) {
        return NULL;
    }
    return ctx->rec_ret_stack[--ctx->rec_ret_top];
}

/* 获取递归栈顶指针，返回当前栈深度 */
int SLJIT_FUNC ljit_rec_ret_top(void *ctx_ptr) {
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    return ctx->rec_ret_top;
}

void *ljit_codegen(void *ctx_ptr) {
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    if (!ctx) return NULL;

    JIT_DBG(MOD_CG, "codegen start, ir_head=%p", ctx->ir_head);

    struct sljit_compiler *compiler = sljit_create_compiler(NULL);
    if (!compiler) { JIT_DBG(MOD_CG, "sljit_create_compiler failed"); return NULL; }

    ctx->compiler = compiler;
    int max_labels = ctx->proto->sizecode + ctx->next_label_id + 1;
    ctx->labels = (struct sljit_label **)calloc(max_labels, sizeof(struct sljit_label *));

    ctx->jumps = (struct sljit_jump **)calloc(max_labels, sizeof(struct sljit_jump *));
    ctx->jump_targets = (int *)calloc(max_labels, sizeof(int));
    ctx->num_jumps = 0;

    JIT_DBG(MOD_CG, "emit_enter...");
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

    /* 创建函数入口标签，用于自递归调用时直接跳转，跳过 C 函数调用开销 */
    ctx->rec_entry_label = sljit_emit_label(compiler);
    JIT_DBG(MOD_CG, "entry label created for self-recursion, proto=%p", ctx->proto);

    JIT_DBG(MOD_CG, "processing IR nodes...");
    ljit_ir_node_t *node = ctx->ir_head;
    int node_count = 0;
    while (node) {
        node_count++;
        JIT_DBG(MOD_CG, "node %d: op=%d, pc=%d", node_count, node->op, node->original_pc);
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
            case IR_GETMAP: ljit_cg_emit_getmap(node, ctx); break;
            case IR_SETMAP: ljit_cg_emit_setmap(node, ctx); break;
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
                 * 自递归快速路径: 若翻译阶段已标记self_rec=1,
                 * 直接调用 ljit_jitcall_self, 跳过运行时Proto比较和类型检查.
                 * 对 fib(32) 等递归密集场景, 省去每次调用的比较开销.
                 */
                if (node->self_rec) {
                    JIT_DBG(MOD_CG, "IR_CALL self_rec fast path: pc=%d, nargs=%d, nresults=%d",
                        node->original_pc, nargs, nresults);
                    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, (sljit_sw)nresults);
                    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R3, 0, SLJIT_IMM, (sljit_sw)ctx->proto);
                    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4V(W, W, 32, W),
                                     SLJIT_IMM, (sljit_sw)ljit_jitcall_self);

                    /*
                     * 内联 reload base: S0 = L->ci->func.p + 1
                     * ljit_jitcall_self 已将 L->ci 恢复为调用者,
                     * 此处重新加载 base 确保后续操作数访问正确.
                     */
                    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, (sljit_sw)ctx->L);
                    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0,
                                   SLJIT_MEM1(SLJIT_R2), offsetof(lua_State, ci));
                    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_S0, 0,
                                   SLJIT_MEM1(SLJIT_R2), offsetof(CallInfo, func));
                    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_S0, 0,
                                   SLJIT_S0, 0, SLJIT_IMM, sizeof(TValue));

                    /*
                     * 调用后重载返回值: 非 spilled 的物理寄存器需要从栈上重新加载,
                     * 因为 luaD_poscall 已将返回值写入栈上对应位置.
                     * 若跳过此步骤, 后续 IR_ADD 等操作会读取到调用前的旧值.
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

                /*
                 * 通用路径: 运行时检查函数类型和JIT状态.
                 * TValue.tt_ 偏移 = sizeof(Value) = 8.
                 * LUA_VLCL = makevariant(LUA_TFUNCTION, 0) = 6.
                 */
                sljit_emit_op1(compiler, SLJIT_MOV32, SLJIT_R3, 0,
                               SLJIT_MEM1(SLJIT_R1), (sljit_sw)sizeof(Value));
                struct sljit_jump *jmp_not_lcl = sljit_emit_cmp(compiler, SLJIT_NOT_EQUAL,
                    SLJIT_R3, 0, SLJIT_IMM, (sljit_sw)LUA_VLCL);

                /* value_.gc 在 TValue 偏移 0 → LClosure* */
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
                 * 运行时自递归检测: 比较目标 Proto* (R3) 与当前函数 Proto* (ctx->proto).
                 * 若相同则走自递归快速路径 (ljit_jitcall_self), 跳过 checkstackGCp 等开销.
                 */
                struct sljit_jump *jmp_not_self = sljit_emit_cmp(compiler, SLJIT_NOT_EQUAL,
                    SLJIT_R3, 0, SLJIT_IMM, (sljit_sw)ctx->proto);

                /*
                 * 自递归快速路径: ljit_jitcall_self(L, func, nresults, p).
                 * 跳过 checkstackGCp (栈空间已由外层调用保证),
                 * 仅做最小 CallInfo 分配和 nil 填充.
                 */
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, (sljit_sw)nresults);
                sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4V(W, W, 32, W),
                                 SLJIT_IMM, (sljit_sw)ljit_jitcall_self);

                struct sljit_jump *jmp_after = sljit_emit_jump(compiler, SLJIT_JUMP);

                /* 非自递归快速路径: ljit_jitcall(L, func, nresults, p) */
                struct sljit_label *nonself_label = sljit_emit_label(compiler);
                sljit_set_label(jmp_not_self, nonself_label);

                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, (sljit_sw)nresults);
                sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4V(W, W, 32, W),
                                 SLJIT_IMM, (sljit_sw)ljit_jitcall);

                struct sljit_jump *jmp_after2 = sljit_emit_jump(compiler, SLJIT_JUMP);

                /*
                 * 慢速路径: 目标不是LCL或没有JIT代码, 回退到 luaD_call.
                 */
                struct sljit_label *slow_label = sljit_emit_label(compiler);
                sljit_set_label(jmp_not_lcl, slow_label);
                sljit_set_label(jmp_no_jit, slow_label);

                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, (sljit_sw)nresults);
                sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3V(W, W, 32),
                                 SLJIT_IMM, (sljit_sw)luaD_call);

                /* 三条路径汇总 */
                struct sljit_label *after_label = sljit_emit_label(compiler);
                sljit_set_label(jmp_after, after_label);
                sljit_set_label(jmp_after2, after_label);

                /*
                 * 内联 reload base: S0 = L->ci->func.p + 1
                 */
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, (sljit_sw)ctx->L);
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0,
                               SLJIT_MEM1(SLJIT_R2), offsetof(lua_State, ci));
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_S0, 0,
                               SLJIT_MEM1(SLJIT_R2), offsetof(CallInfo, func));
                sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_S0, 0,
                               SLJIT_S0, 0, SLJIT_IMM, sizeof(TValue));

                /*
                 * 调用后: Lua栈上的结果寄存器已被更新,
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
            case IR_NEWMAP: ljit_cg_emit_newmap(node, ctx); break;
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

            /*
             * 异步/Trait 操作码原生 codegen
             * 包装为 C 函数调用 (ljit_icall_*)，执行后继续 JIT 流程
             */
            case IR_ASYNCWRAP: {
                Instruction i = ctx->proto->code[node->original_pc];
                int b = GETARG_B(i);
                int tvalue_size = sizeof(TValue);

                JIT_DBG(MOD_CG, "ASYNCWRAP: pc=%d, B=%d", node->original_pc, b);

                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
                sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R1, 0, SLJIT_S0, 0,
                               SLJIT_IMM, (sljit_sw)(b * tvalue_size));
                sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS2V(W, W),
                                 SLJIT_IMM, (sljit_sw)ljit_icall_asyncwrap);
                break;
            }
            case IR_GENERICWRAP: {
                Instruction i = ctx->proto->code[node->original_pc];
                int a = GETARG_A(i);
                int b = GETARG_B(i);

                JIT_DBG(MOD_CG, "GENERICWRAP: pc=%d, A=%d, B=%d", node->original_pc, a, b);

                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_S0, 0);
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, (sljit_sw)a);
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R3, 0, SLJIT_IMM, (sljit_sw)b);
                sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4V(W, W, 32, 32),
                                 SLJIT_IMM, (sljit_sw)ljit_icall_genericwrap);

                /* 重新加载 base (GENERICWRAP 可能重新分配栈) */
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, (sljit_sw)ctx->L);
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0,
                               SLJIT_MEM1(SLJIT_R2), offsetof(lua_State, ci));
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_S0, 0,
                               SLJIT_MEM1(SLJIT_R2), offsetof(CallInfo, func));
                sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_S0, 0,
                               SLJIT_S0, 0, SLJIT_IMM, sizeof(TValue));
                break;
            }
            case IR_SETTRAITFLAG: {
                Instruction i = ctx->proto->code[node->original_pc];
                int a = GETARG_A(i);
                int tvalue_size = sizeof(TValue);

                JIT_DBG(MOD_CG, "SETTRAITFLAG: pc=%d, A=%d", node->original_pc, a);

                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
                sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R1, 0, SLJIT_S0, 0,
                               SLJIT_IMM, (sljit_sw)(a * tvalue_size));
                sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS2V(W, W),
                                 SLJIT_IMM, (sljit_sw)ljit_icall_settraitflag);
                break;
            }
            case IR_SETTRAITREQUIRE: {
                Instruction i = ctx->proto->code[node->original_pc];
                int a = GETARG_A(i);
                int b = GETARG_B(i);
                int c = GETARG_C(i);
                int tvalue_size = sizeof(TValue);
                TString *method_name = tsvalue(&ctx->proto->k[b]);

                JIT_DBG(MOD_CG, "SETTRAITREQUIRE: pc=%d, A=%d, B=%d, C=%d",
                    node->original_pc, a, b, c);

                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
                sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R1, 0, SLJIT_S0, 0,
                               SLJIT_IMM, (sljit_sw)(a * tvalue_size));
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, (sljit_sw)method_name);
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R3, 0, SLJIT_IMM, (sljit_sw)c);
                sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4V(W, W, W, 32),
                                 SLJIT_IMM, (sljit_sw)ljit_icall_settraitrequire);
                break;
            }
            case IR_USETRAIT: {
                Instruction i = ctx->proto->code[node->original_pc];
                int a = GETARG_A(i);
                int b = GETARG_B(i);
                int tvalue_size = sizeof(TValue);

                JIT_DBG(MOD_CG, "USETRAIT: pc=%d, A=%d, B=%d", node->original_pc, a, b);

                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
                sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R1, 0, SLJIT_S0, 0,
                               SLJIT_IMM, (sljit_sw)(a * tvalue_size));
                sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R2, 0, SLJIT_S0, 0,
                               SLJIT_IMM, (sljit_sw)(b * tvalue_size));
                sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3V(W, W, W),
                                 SLJIT_IMM, (sljit_sw)ljit_icall_usetrait);
                break;
            }
            case IR_AWAIT: {
                Instruction i = ctx->proto->code[node->original_pc];
                int a = GETARG_A(i);
                int b = GETARG_B(i);
                int tvalue_size = sizeof(TValue);

                JIT_DBG(MOD_CG, "AWAIT: pc=%d, A=%d, B=%d", node->original_pc, a, b);

                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
                sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R1, 0, SLJIT_S0, 0,
                               SLJIT_IMM, (sljit_sw)(a * tvalue_size));
                sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R2, 0, SLJIT_S0, 0,
                               SLJIT_IMM, (sljit_sw)(b * tvalue_size));
                sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3V(W, W, W),
                                 SLJIT_IMM, (sljit_sw)ljit_icall_await);
                break;
            }

            /*
             * 未实现原生 codegen 的 IR 操作码: 统一触发解释器回退.
             * 之前这些 case 被 break 跳过, 既不生成代码也不触发 fallback,
             * 导致运行时状态不一致 (如 trait 设置、namespace 链接等被静默忽略).
             */
            case IR_ADDMETHOD:
            case IR_CASE:
            case IR_CHECKTYPE:
            case IR_ERRNNIL:
            case IR_EXTRAARG:
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
            case IR_TESTNIL:
            case IR_CJMP:
            case IR_MERGE:
            case IR_REGEX:
                JIT_DBG(MOD_CG, "NYI fallback: op=%d, pc=%d", node->op, node->original_pc);
                goto codegen_fallback;
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
            case IR_VARARG: {
                /*
                 * 原生 codegen: 调用 ljit_icall_vararg(L, ra, wanted)
                 * 将变长参数复制到栈上，然后继续 JIT 执行
                 */
                int a = node->dest.v.reg;
                int wanted = node->src2.v.i - 1;
                int tvalue_size = sizeof(TValue);
                if (wanted < -1) wanted = -1;

                JIT_DBG(MOD_CG, "VARARG native: ra=R%d, wanted=%d", a, wanted);

                /* R0 = L */
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);

                /* R1 = ra = base + a * tvalue_size */
                sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R1, 0, SLJIT_S0, 0,
                               SLJIT_IMM, (sljit_sw)(a * tvalue_size));

                /* R2 = wanted */
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, (sljit_sw)wanted);

                /* Call ljit_icall_vararg(L, ra, wanted) */
                sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3V(W, W, 32),
                                 SLJIT_IMM, (sljit_sw)ljit_icall_vararg);

                break;
            }
            case IR_VARARGPREP: {
                /*
                 * 原生 codegen: 调用 ljit_icall_varargprep(L, nfixparams)
                 * 调整变长参数函数的栈帧，返回新的 base 指针，继续 JIT 执行
                 */
                int a = node->dest.v.reg;  /* nfixparams = GETARG_A */

                JIT_DBG(MOD_CG, "VARARGPREP native: nfixparams=%d, reloading base", a);

                /* R0 = L */
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);

                /* R1 = nfixparams */
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM, (sljit_sw)a);

                /* Call ljit_icall_varargprep(L, nfixparams) -> 返回新 base 在 R0 */
                sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS2(W, W, 32),
                                 SLJIT_IMM, (sljit_sw)ljit_icall_varargprep);

                /* 关键: VARARGPREP 会移动栈帧，必须更新 S0 = 返回值(新 base) */
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_S0, 0, SLJIT_R0, 0);

                break;
            }
            case IR_GETSUPER: ljit_cg_emit_getsuper(node, ctx); break;
            case IR_INHERIT: ljit_cg_emit_inherit(node, ctx); break;
            case IR_NEWCLASS: ljit_cg_emit_newclass(node, ctx); break;
            case IR_NEWOBJ: ljit_cg_emit_newobj(node, ctx); break;
            case IR_CLOSURE: ljit_cg_emit_closure(node, ctx); break;

            case IR_CONCAT: {
                /*
                 * 字符串拼接原生 codegen:
                 * 调用 ljit_icall_concat(L, total, ra) 执行拼接，
                 * 拼接完成后从栈重新加载目标寄存器，继续执行后续 IR 节点，
                 * 不再触发全函数 fallback 返回。
                 *
                 * ljit_icall_concat 会设置 L->top = ra + total，
                 * 然后调用 luaV_concat(L, total) 将结果放在 ra 位置。
                 */
                int tvalue_size = sizeof(TValue);
                int ra = node->dest.v.reg;
                int total = node->src1.v.i;

                JIT_DBG(MOD_CG, "CONCAT native: total=%d, ra=R%d", total, ra);

                /* R0 = L (lua_State 指针) */
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);

                /* R1 = total (待拼接的值数量) */
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM, (sljit_sw)total);

                /* R2 = ra 地址 (栈基址 S0 + ra * sizeof(TValue)) */
                sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R2, 0, SLJIT_S0, 0,
                               SLJIT_IMM, (sljit_sw)(ra * tvalue_size));

                /* 调用 C 函数 ljit_icall_concat(L, total, ra) */
                sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3V(W, 32, W),
                                 SLJIT_IMM, (sljit_sw)ljit_icall_concat);

                /*
                 * 拼接后目标寄存器 ra 在物理寄存器中的值已失效，
                 * 需要从栈槽重新加载到物理寄存器（如果未溢出到栈）
                 */
                if (!node->dest.is_spilled) {
                    sljit_emit_op1(compiler, SLJIT_MOV, node->dest.phys_reg, 0,
                                   SLJIT_MEM1(SLJIT_S0), ra * tvalue_size);
                }
                break;
            }
            case IR_FORPREP: ljit_cg_emit_forprep(node, ctx); break;
            case IR_FORLOOP: ljit_cg_emit_forloop(node, ctx); break;

            // Additional instructions can be mapped here as they are implemented
            codegen_fallback:
                /*
                 * 通用回退路径: 未实现的操作码调用解释器执行当前函数剩余部分,
                 * 解释器返回后 JIT 代码返回 1 (成功).
                 * 所有 goto codegen_fallback 的 case 统一走此路径.
                 */
                JIT_DBG(MOD_CG, "fallback: calling interpreter, op=%d, pc=%d", node->op, node->original_pc);
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_S0, 0);
                sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS2V(W, W),
                                 SLJIT_IMM, (sljit_sw)ljit_icall_fallback);
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_S1, 0, SLJIT_IMM, 1);
                sljit_emit_return(compiler, SLJIT_MOV32, SLJIT_S1, 0);
                node = NULL;
                continue;
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

    JIT_DBG(MOD_CG, "processed %d nodes, generating code...", node_count);
    /* 默认返回成功: 若所有IR节点处理完毕且未遇到显式RETURN, 标记JIT执行成功 */
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_S1, 0, SLJIT_IMM, 1);
    sljit_emit_return(compiler, SLJIT_MOV32, SLJIT_S1, 0);

    void *code = sljit_generate_code(compiler, 0, NULL);

    JIT_DBG(MOD_CG, "code generated: %p, size=%zu", code, code ? sljit_get_generated_code_size(compiler) : 0);
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

void SLJIT_FUNC ljit_icall_newclass(lua_State *L, TString *classname, StkId ra) {
    CallInfo *ci = L->ci;
    Proto *p = clLvalue(s2v(ci->func.p))->p;
    StkId base = ci->func.p + 1;
    /* 保存ra的寄存器索引，用于栈重分配后重新计算 */
    ptrdiff_t ra_reg = ra - base;
    /* 填充栈顶nil到maxstacksize */
    while (L->top.p < base + p->maxstacksize)
        setnilvalue(s2v(L->top.p++));
    luaD_checkstack(L, 1);
    /* 栈可能重分配，重新获取base和ra */
    ci = L->ci;
    base = ci->func.p + 1;
    ra = base + ra_reg;
    /* 保存pc用于错误报告 */
    ci->u.l.savedpc = p->code;
    /* 调用luaC_newclass，结果在栈顶 */
    luaC_newclass(L, classname);
    /* luaC_newclass内部会调用API可能再次导致栈重分配 */
    ci = L->ci;
    base = ci->func.p + 1;
    ra = base + ra_reg;
    /* 将结果从栈顶移动到目标寄存器ra */
    setobj2s(L, ra, s2v(L->top.p - 1));
    L->top.p--;
}

void SLJIT_FUNC ljit_icall_inherit(lua_State *L, StkId ra, StkId rb) {
    CallInfo *ci = L->ci;
    /* 保存pc和top（同savestate语义） */
    L->top.p = ci->top.p;
    ci->u.l.savedpc = ci->u.l.savedpc;
    /* 压入子类和父类到栈顶 */
    setobj2s(L, L->top.p, s2v(ra));
    L->top.p++;
    setobj2s(L, L->top.p, s2v(rb));
    L->top.p++;
    /* 调用luaC_inherit设置继承关系 */
    luaC_inherit(L, -2, -1);
    /* 弹出临时压入的值 */
    L->top.p -= 2;
}

void SLJIT_FUNC ljit_icall_getsuper(lua_State *L, StkId rb, TString *key, StkId ra) {
    CallInfo *ci = L->ci;
    StkId base = ci->func.p + 1;
    /* 保存ra的寄存器索引 */
    ptrdiff_t ra_reg = ra - base;
    /* 保存pc和top */
    L->top.p = ci->top.p;
    ci->u.l.savedpc = ci->u.l.savedpc;
    /* 压入对象 */
    setobj2s(L, L->top.p, s2v(rb));
    L->top.p++;
    /* 调用luaC_super获取父类方法，结果在栈顶 */
    luaC_super(L, -1, key);
    /* 栈可能重分配，重新获取base和ra */
    ci = L->ci;
    base = ci->func.p + 1;
    ra = base + ra_reg;
    /* 将结果写入目标寄存器 */
    setobj2s(L, ra, s2v(L->top.p - 1));
    L->top.p -= 2;
}

void SLJIT_FUNC ljit_icall_newobj(lua_State *L, StkId rb, int nargs, StkId ra_args_base) {
    CallInfo *ci = L->ci;
    StkId base = ci->func.p + 1;
    /* 保存ra的寄存器索引 */
    ptrdiff_t ra_reg = ra_args_base - base;
    /* 保存pc和top */
    L->top.p = ci->top.p;
    ci->u.l.savedpc = ci->u.l.savedpc;
    /* 压入类 */
    setobj2s(L, L->top.p, s2v(rb));
    L->top.p++;
    /* 复制构造参数: ra_args_base+1 到 ra_args_base+nargs */
    {
        StkId arg_src = ra_args_base + 1;
        int j;
        for (j = 0; j < nargs; j++) {
            setobj2s(L, L->top.p, s2v(arg_src + j));
            L->top.p++;
        }
    }
    /* 调用luaC_newobject创建实例，结果在栈顶 */
    luaC_newobject(L, -(nargs + 1), nargs);
    /* 栈可能重分配，重新获取base和ra */
    ci = L->ci;
    base = ci->func.p + 1;
    ra_args_base = base + ra_reg;
    /* 将结果写入目标寄存器ra */
    setobj2s(L, ra_args_base, s2v(L->top.p - 1));
    L->top.p -= (nargs + 2);
    /* 触发GC检查（如果需要） */
    luaC_checkGC(L);
}
