#include "ljit_codegen.h"
#include "../core/ljit_internal.h"
#include "../core/ljit_debug.h"
#include "../ir/ljit_ir.h"
#include "../frontend/ljit_analyze.h"
#include "../optimize/ljit_opt.h"
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
#include "../../../stdlib/lsuper.h"
#include "../../../core/lstring.h"
#include <string.h>

/* lvm_generic_call 声明 (lvm.c 中为 static，需改为非 static 以支持 JIT codegen) */
extern int lvm_generic_call(lua_State *L);
/* l_strcmp 字符串比较 (lvm.c 中为 static，需改为非 static 以支持 JIT codegen) */
extern int l_strcmp(const TString *ts1, const TString *ts2);

/* codegen 路径性能计数器 (ljit_cg_arith.c) */
extern int ljit_stat_int_fastpath;
extern int ljit_stat_guarded_fastpath;
extern int ljit_stat_num_fastpath;
extern int ljit_stat_generic;

/* 条件测试内联计数器 */
int ljit_stat_test_inline = 0;
int ljit_stat_test_icall = 0;
/* 比较内联计数器 (定义在 ljit_cg_arith.c) */
extern int ljit_stat_cmp_inline;

/* CallInfo 分配计数器 (ljit_jitcall_self 快路径) */
volatile int ljit_ci_extend_count = 0;
volatile int ljit_ci_reuse_count = 0;

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

/*
 * ============================================================
 * Fallback IR 操作码 icall 函数: 为原本走 codegen_fallback 的
 * IR 操作码提供原生 icall 封装, 减少解释器回退.
 * ============================================================
 */

/* IR_TESTNIL: nil 测试 (用于 optional chaining 和 null coalescing) */
void SLJIT_FUNC ljit_icall_testnil(lua_State *L, StkId base, int pc, int *skip) {
    Proto *p = clLvalue(s2v(L->ci->func.p))->p;
    Instruction i = p->code[pc];
    TValue *rb = s2v(base + GETARG_B(i));
    int k = GETARG_k(i);
    /* 若 (is_nil != k) 则跳过下一条指令 (JMP) */
    *skip = (ttisnil(rb) != k) ? 1 : 0;
    if (!(*skip)) {
        int a = GETARG_A(i);
        if (a != MAXARG_A) {
            setobj2s(L, base + a, rb);
        }
    }
}

/* IR_IN: in 操作符 */
void SLJIT_FUNC ljit_icall_in(lua_State *L, StkId base, int pc) {
    Proto *p = clLvalue(s2v(L->ci->func.p))->p;
    Instruction i = p->code[pc];
    StkId ra = base + GETARG_A(i);
    TValue *a = s2v(base + GETARG_B(i));
    TValue *b = s2v(base + GETARG_C(i));
    /* 调用 lvm.c 中的 inopr (需要声明为非 static) */
    extern void inopr(lua_State *L, StkId ra, TValue *a, TValue *b);
    inopr(L, ra, a, b);
}

/* IR_IS: 类型检查 (is 操作符) */
void SLJIT_FUNC ljit_icall_is(lua_State *L, StkId base, int pc, int *cond) {
    Proto *p = clLvalue(s2v(L->ci->func.p))->p;
    Instruction i = p->code[pc];
    TValue *ra = s2v(base + GETARG_A(i));
    TValue *rb = p->k + GETARG_B(i);
    const char *typename_expected;
    const char *typename_actual;

    lua_assert(ttisstring(rb));
    typename_expected = getstr(tsvalue(rb));

    const TValue *tm = luaT_gettmbyobj(L, ra, TM_TYPE);
    if (!notm(tm) && ttisstring(tm)) {
        typename_actual = getstr(tsvalue(tm));
    } else {
        typename_actual = luaT_objtypename(L, ra);
    }

    *cond = (strcmp(typename_actual, typename_expected) == 0);
}

/* IR_INSTANCEOF: instanceof 操作符 */
void SLJIT_FUNC ljit_icall_instanceof(lua_State *L, StkId base, int pc, int *cond) {
    Proto *p = clLvalue(s2v(L->ci->func.p))->p;
    Instruction i = p->code[pc];
    StkId ra = base + GETARG_A(i);
    TValue *rb = s2v(base + GETARG_B(i));
    int k = GETARG_k(i);

    luaD_checkstack(L, 2);
    setobj2s(L, L->top.p, s2v(ra));
    L->top.p++;
    setobj2s(L, L->top.p, rb);
    L->top.p++;
    int result = luaC_instanceof(L, -2, -1);
    L->top.p -= 2;

    *cond = (result == k);
}

/* IR_SLICE: 切片操作 */
void SLJIT_FUNC ljit_icall_slice(lua_State *L, StkId base, int pc) {
    Proto *p = clLvalue(s2v(L->ci->func.p))->p;
    Instruction i = p->code[pc];
    StkId ra = base + GETARG_A(i);
    int b = GETARG_B(i);
    StkId base_reg = base + b;
    TValue *src_table = s2v(base_reg);
    TValue *start_val = s2v(base_reg + 1);
    TValue *end_val = s2v(base_reg + 2);
    TValue *step_val = s2v(base_reg + 3);

    if (l_unlikely(!ttistable(src_table))) {
        luaG_typeerror(L, src_table, "slice");
    }
    Table *t = hvalue(src_table);
    lua_Integer tlen = luaH_getn(t);

    /* 解析 start/end/step */
    lua_Integer start_idx, end_idx, step;
    if (ttisnil(start_val)) start_idx = 1;
    else if (ttisinteger(start_val)) start_idx = ivalue(start_val);
    else if (ttisfloat(start_val)) {
        lua_Number n = fltvalue(start_val);
        lua_Integer ni;
        if (luaV_flttointeger(n, &ni, F2Ieq)) start_idx = ni;
        else luaG_runerror(L, "slice start index must be integer");
    } else luaG_runerror(L, "slice start index must be integer or nil");

    if (ttisnil(end_val)) end_idx = tlen;
    else if (ttisinteger(end_val)) end_idx = ivalue(end_val);
    else if (ttisfloat(end_val)) {
        lua_Number n = fltvalue(end_val);
        lua_Integer ni;
        if (luaV_flttointeger(n, &ni, F2Ieq)) end_idx = ni;
        else luaG_runerror(L, "slice end index must be integer");
    } else luaG_runerror(L, "slice end index must be integer or nil");

    if (ttisnil(step_val)) step = 1;
    else if (ttisinteger(step_val)) step = ivalue(step_val);
    else if (ttisfloat(step_val)) {
        lua_Number n = fltvalue(step_val);
        lua_Integer ni;
        if (luaV_flttointeger(n, &ni, F2Ieq)) step = ni;
        else luaG_runerror(L, "slice step must be integer");
    } else luaG_runerror(L, "slice step must be integer or nil");

    if (step == 0) luaG_runerror(L, "slice step cannot be zero");

    /* 处理负索引 */
    if (start_idx < 0) start_idx += tlen + 1;
    if (end_idx < 0) end_idx += tlen + 1;

    /* 限制范围 */
    if (step > 0) {
        if (start_idx < 1) start_idx = 1;
        if (end_idx > tlen) end_idx = tlen;
    } else {
        if (start_idx > tlen) start_idx = tlen;
        if (end_idx < 1) end_idx = 1;
    }

    /* 创建结果表 */
    L->top.p = ra + 1;
    Table *result_t = luaH_new(L);
    sethvalue2s(L, ra, result_t);

    /* 复制元素 */
    lua_Integer result_idx = 1;
    if (step > 0) {
        for (lua_Integer idx = start_idx; idx <= end_idx; idx += step) {
            const TValue *val = luaH_getint(t, idx);
            if (!ttisnil(val)) {
                TValue temp;
                setobj(L, &temp, val);
                luaH_setint(L, result_t, result_idx, &temp);
            }
            result_idx++;
        }
    } else {
        for (lua_Integer idx = end_idx; idx >= start_idx; idx += step) {
            const TValue *val = luaH_getint(t, idx);
            if (!ttisnil(val)) {
                TValue temp;
                setobj(L, &temp, val);
                luaH_setint(L, result_t, result_idx, &temp);
            }
            result_idx++;
        }
    }
}

/* IR_GETPROP: 属性访问 icall */
void SLJIT_FUNC ljit_icall_getprop(lua_State *L, StkId base, int pc) {
    Proto *p = clLvalue(s2v(L->ci->func.p))->p;
    Instruction i = p->code[pc];
    StkId ra = base + GETARG_A(i);
    TValue *rb = s2v(base + GETARG_B(i));
    TString *key = tsvalue(&p->k[GETARG_C(i)]);
    /* 通过栈传递参数: push obj, 调用 getprop, 结果在栈顶 */
    setobj2s(L, L->top.p, rb);
    L->top.p++;
    luaC_getprop(L, -1, key);
    setobj2s(L, ra, s2v(L->top.p - 1));
    L->top.p -= 2;
}

/* IR_SETPROP: 属性设置 icall */
void SLJIT_FUNC ljit_icall_setprop(lua_State *L, StkId base, int pc) {
    Proto *p = clLvalue(s2v(L->ci->func.p))->p;
    Instruction i = p->code[pc];
    StkId ra = base + GETARG_A(i);
    TString *key = tsvalue(&p->k[GETARG_B(i)]);
    TValue *rc = s2v(base + GETARG_C(i));
    /* 通过栈传递参数: push obj, push value, 调用 setprop */
    setobj2s(L, L->top.p, s2v(ra));
    L->top.p++;
    setobj2s(L, L->top.p, rc);
    L->top.p++;
    luaC_setprop(L, -2, key, -1);
    L->top.p -= 2;
}

/* IR_SETSUPER: 父类设置 icall */
void SLJIT_FUNC ljit_icall_setsuper(lua_State *L, StkId base, int pc) {
    Proto *p = clLvalue(s2v(L->ci->func.p))->p;
    Instruction i = p->code[pc];
    StkId ra = base + GETARG_A(i);
    TValue *rb = s2v(base + GETARG_B(i));
    TValue *rc = s2v(base + GETARG_C(i));
    if (ttissuperstruct(s2v(ra))) {
        SuperStruct *ss = superstructvalue(s2v(ra));
        luaS_setsuperstruct(L, ss, rb, rc);
    }
}

/* IR_SPACESHIP: <=> 三路比较 icall */
void SLJIT_FUNC ljit_icall_spaceship(lua_State *L, StkId base, int pc) {
    Proto *p = clLvalue(s2v(L->ci->func.p))->p;
    Instruction i = p->code[pc];
    StkId ra = base + GETARG_A(i);
    TValue *rb = s2v(base + GETARG_B(i));
    TValue *rc = s2v(base + GETARG_C(i));
    lua_Integer result;

    if (ttisinteger(rb) && ttisinteger(rc)) {
        lua_Integer ib = ivalue(rb);
        lua_Integer ic = ivalue(rc);
        result = (ib < ic) ? -1 : ((ib > ic) ? 1 : 0);
    } else if (ttisnumber(rb) && ttisnumber(rc)) {
        lua_Number nb = ttisinteger(rb) ? cast_num(ivalue(rb)) : fltvalue(rb);
        lua_Number nc = ttisinteger(rc) ? cast_num(ivalue(rc)) : fltvalue(rc);
        result = (nb < nc) ? -1 : ((nb > nc) ? 1 : 0);
    } else if (ttisstring(rb) && ttisstring(rc)) {
        int cmp = l_strcmp(tsvalue(rb), tsvalue(rc));
        result = (cmp < 0) ? -1 : ((cmp > 0) ? 1 : 0);
    } else {
        luaG_ordererror(L, rb, rc);
        return;  /* unreachable */
    }
    setivalue(s2v(ra), result);
    lua_assert(ci_equal(L->ci, L->ci));
}

/* 自递归调用计数器，用于性能诊断 */
int ljit_self_call_count = 0;
/* 自递归调用总耗时（微秒），用于性能分析 */
long long ljit_self_call_time_us = 0;

/*
 * 自递归调用轻量级帧设置：跳过 checkstackGCp（栈空间已知足够），
 * 跳过 upvalue 检查，仅做最小 CallInfo 分配和 nil 填充。
 * 相比 ljit_jitcall 减少了 checkstackGCp 的 GC 检查开销，
 * 对 fib(32) 等递归密集场景有显著加速效果。
 */
void SLJIT_FUNC ljit_jitcall_self(lua_State *L, StkId func, int nresults, Proto *p) {
    ljit_self_call_count++;
    int narg = cast_int(L->top.p - func) - 1;
    int nfixparams = p->numparams;

    L->nCcalls++;

    typedef int (*jit_func_t)(StkId);
    jit_func_t jit = (jit_func_t)p->jit_trace;
    StkId base = func + 1;

    /*
     * 快路径：仅设置 ci->func.p 和 L->ci，跳过其他 CallInfo 字段设置。
     * JIT 代码 reload 依赖 L->ci->func 获取当前层级的 base 指针，
     * 因此必须设置 L->ci，但可以跳过 nresults/callstatus/top/savedpc 等字段。
     */
    /*
     * 快路径优化：预扩展 CallInfo
     * 自递归调用频繁，每次 extend 都有开销。一次性扩展2个slot，
     * 后续调用可直接复用，减少 luaE_extendCI 调用次数。
     */
    int ci_extended = (L->ci->next == NULL);
    CallInfo *ci;
    if (ci_extended) {
        ci = luaE_extendCI(L);
        /* 预扩展: 再扩展一个 CallInfo，后续自递归调用可直接复用 */
        if (L->ci->next == NULL) {
            luaE_extendCI(L);
        }
        ljit_ci_extend_count++;
    } else {
        ci = L->ci->next;
        ljit_ci_reuse_count++;
    }
    L->ci = ci;
    ci->func.p = func;

    JIT_DBG(MOD_CG, "jitcall_self: enter, narg=%d, nfixparams=%d, nresults=%d, func=%p",
        narg, nfixparams, nresults, func);

    int jit_done = jit(base);

    /* 恢复调用者的 ci */
    L->ci = ci->previous;
    L->nCcalls--;

    if (jit_done) {
        JIT_DBG(MOD_CG, "jitcall_self: fast path success, minimal CallInfo setup");
        return;
    }

    /* 回退路径：完整分配 CallInfo 并走解释器兜底 */
    JIT_DBG(MOD_CG, "jitcall_self: fallback, full CallInfo setup");

    int fsize = p->maxstacksize;
    if (l_unlikely(getCcalls(L) >= LUAI_MAXCCALLS)) {
        luaE_checkcstack(L);
    }

    ci = L->ci->next;
    if (l_unlikely(ci == NULL)) {
        ci = luaE_extendCI(L);
    }
    L->ci = ci;
    ci->func.p = func;
    ci->nresults = nresults;
    ci->callstatus = CIST_FRESH;
    ci->top.p = func + 1 + fsize;
    ci->u.l.savedpc = p->code;

    /* 填充缺失参数为 nil */
    if (narg < nfixparams) {
        for (; narg < nfixparams; narg++)
            setnilvalue(s2v(L->top.p++));
    }

    lua_assert(ci->top.p <= L->stack_last.p);

    L->top.p = func + 1 + narg;
    luaD_call(L, func, nresults);
}

/*
 * 自递归调用超轻量包装器: 仅做 CallInfo 管理和 jit_trace 调用,
 * 跳过 ljit_self_call_count、nCcalls 管理、narg/nfixparams 计算,
 * 比 ljit_jitcall_self 减少约 30% 开销。
 * 由 SELF_REC_INLINE 内联路径调用, 负责 CallInfo 分配/复用和 jit_trace 调用.
 */
void SLJIT_FUNC ljit_jitcall_self_lite(lua_State *L, StkId func, Proto *p) {
    CallInfo *ci;
    if (L->ci->next == NULL) {
        ci = luaE_extendCI(L);
        if (L->ci->next == NULL) {
            luaE_extendCI(L);
        }
        ljit_ci_extend_count++;
    } else {
        ci = L->ci->next;
        ljit_ci_reuse_count++;
    }
    L->ci = ci;
    ci->func.p = func;
    typedef int (*jit_func_t)(StkId);
    jit_func_t jit = (jit_func_t)p->jit_trace;
    jit(func + 1);
    L->ci = ci->previous;
}

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

    /*
     * IR 操作码计数器: 统计每种 IR 操作码在 codegen 中的出现次数,
     * 用于性能瓶颈分析.
     */
    int ir_op_count[IR_REGEX + 1];
    memset(ir_op_count, 0, sizeof(ir_op_count));

    /*
     * 参数类型推断: JIT编译基于首次调用的参数类型做特化.
     * 对于fib等递归函数, 参数始终为整数, 将R0标记为INT
     * 使n-1/n-2等算术操作走INT_FASTPATH.
     * 若实际调用传入非整数, 类型守卫会回退到解释器.
     */
    {
        ljit_analyze_info_t *ainfo = (ljit_analyze_info_t *)ctx->analyze_info;
        if (ainfo && ainfo->reg_types && ainfo->max_regs > 0
            && ainfo->reg_types[0] != JIT_TYPE_INT) {
            ainfo->reg_types[0] = JIT_TYPE_INT;
            fprintf(stderr, "[JIT-CODEGEN] param type inference: R[0] = INT\n");
        }
    }

    while (node) {
        node_count++;
        if (node->op <= IR_REGEX) ir_op_count[node->op]++;
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
                    JIT_DBG(MOD_CG, "IR_CALL self_rec: pc=%d, nargs=%d, nresults=%d",
                        node->original_pc, nargs, nresults);
                    fprintf(stderr, "[JIT-CODEGEN] IR_CALL pc=%d: self_rec=1, using ljit_jitcall_self icall\n",
                        node->original_pc);

                    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
                    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R1, 0, SLJIT_S0, 0,
                                   SLJIT_IMM, (sljit_sw)(node->dest.v.reg * tvalue_size));
                    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, (sljit_sw)nresults);
                    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R3, 0, SLJIT_IMM, (sljit_sw)ctx->proto);
                    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4V(W, W, 32, W),
                                     SLJIT_IMM, (sljit_sw)ljit_jitcall_self);

                    /* 调用后重载 S0 = L->ci->func + 1 */
                    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
                    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0,
                                   SLJIT_MEM1(SLJIT_R0), offsetof(lua_State, ci));
                    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_S0, 0,
                                   SLJIT_MEM1(SLJIT_R2), offsetof(CallInfo, func));
                    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_S0, 0,
                                   SLJIT_S0, 0, SLJIT_IMM, sizeof(TValue));

                    /*
                     * 调用后重载返回值: 非 spilled 的物理寄存器需要从栈上重新加载,
                     * 因为递归调用已将返回值写入栈上对应位置.
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
                    /*
                     * 自递归类型特化: 调用后更新 reg_types,
                     * 将返回值寄存器标记为 JIT_TYPE_INT,
                     * 使后续操作(如 ADD)走 INT_FASTPATH 而非 GUARDED_INT_FASTPATH.
                     */
                    {
                        ljit_analyze_info_t *ainfo = (ljit_analyze_info_t *)ctx->analyze_info;
                        if (ainfo && ainfo->reg_types && nresults > 0) {
                            int base_reg = node->dest.v.reg;
                            for (int res = 0; res < nresults && base_reg + res < ainfo->max_regs; res++) {
                                ainfo->reg_types[base_reg + res] = JIT_TYPE_INT;
                            }
                            fprintf(stderr, "[JIT-CODEGEN] IR_CALL pc=%d: self_rec, updated reg_types R[%d..%d] = INT\n",
                                node->original_pc, base_reg, base_reg + nresults - 1);
                        }
                    }
                    break;
                }

                /*
                 * 通用路径: 运行时检查函数类型和JIT状态.
                 * TValue.tt_ 偏移 = sizeof(Value) = 8.
                 * LUA_VLCL = makevariant(LUA_TFUNCTION, 0) = 6.
                 */
                fprintf(stderr, "[JIT-CODEGEN] IR_CALL pc=%d: generating runtime self-rec check, ctx->proto=%p\n",
                    node->original_pc, ctx->proto);
                /*
                 * tt_ 是 lu_byte (1字节), 必须用 SLJIT_MOV_U8 加载,
                 * SLJIT_MOV32 会多读 3 字节 padding 垃圾导致类型比较失败.
                 * 注意: TValue 中存储的 tt_ 是 ctb(LUA_VLCL) = LUA_VLCL | BIT_ISCOLLECTABLE = 70,
                 * 不是原始的 LUA_VLCL = 6.
                 */
                sljit_emit_op1(compiler, SLJIT_MOV_U8, SLJIT_R3, 0,
                               SLJIT_MEM1(SLJIT_R1), (sljit_sw)sizeof(Value));
                struct sljit_jump *jmp_not_lcl = sljit_emit_cmp(compiler, SLJIT_NOT_EQUAL,
                    SLJIT_R3, 0, SLJIT_IMM, (sljit_sw)ctb(LUA_VLCL));

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
                 * 自递归快速路径: 调用 ljit_jitcall_self (icall),
                 * 该函数负责 CallInfo 分配、ci 切换、jit_trace 调用和 fallback.
                 */
                fprintf(stderr, "[JIT-CODEGEN] IR_CALL pc=%d: runtime self-rec detected, using ljit_jitcall_self icall\n",
                    node->original_pc);

                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, (sljit_sw)nresults);
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R3, 0, SLJIT_IMM, (sljit_sw)ctx->proto);
                sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4V(W, W, 32, W),
                                 SLJIT_IMM, (sljit_sw)ljit_jitcall_self);

                struct sljit_jump *jmp_after_self = sljit_emit_jump(compiler, SLJIT_JUMP);

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
                sljit_set_label(jmp_after_self, after_label);
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
                /*
                 * 运行时路径返回值类型特化: 将 IR_CALL 的返回值寄存器
                 * 标记为 JIT_TYPE_INT, 使后续 ADD 等操作走 INT_FASTPATH.
                 * 对 fib 等递归函数, 返回值始终为整数.
                 */
                {
                    ljit_analyze_info_t *ainfo = (ljit_analyze_info_t *)ctx->analyze_info;
                    if (ainfo && ainfo->reg_types && nresults > 0) {
                        int base_reg = node->dest.v.reg;
                        for (int res = 0; res < nresults && base_reg + res < ainfo->max_regs; res++) {
                            ainfo->reg_types[base_reg + res] = JIT_TYPE_INT;
                        }
                        fprintf(stderr, "[JIT-CODEGEN] IR_CALL pc=%d: runtime path, updated reg_types R[%d..%d] = INT\n",
                            node->original_pc, base_reg, base_reg + nresults - 1);
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
                ljit_ir_node_t *test_next;
                struct sljit_label *test_after;
                struct sljit_jump *test_skip;
                struct sljit_jump *bool_skip;
                struct sljit_label *bool_after;

                /* 读取操作数类型 */
                ljit_analyze_info_t *ainfo = (ljit_analyze_info_t *)ctx->analyze_info;
                int src_reg = node->src1.v.reg;
                ljit_type_t src_type = JIT_TYPE_ANY;
                if (ainfo && ainfo->reg_types && src_reg >= 0 && src_reg < ainfo->max_regs)
                    src_type = ainfo->reg_types[src_reg];

                /* 内联快速路径: INT 类型 */
                if (src_type == JIT_TYPE_INT) {
                    /* Lua 中 INT 值始终为真 (0也是真值，只有 nil/false 为假) */
                    /* INT 类型始终为真：如果 k=1(为真时跳转)，直接跳转；k=0(为假时跳转)，fall through */
                    fprintf(stderr, "[JIT-CODEGEN] IR_TEST pc=%d: inline INT test, value always truthy\n",
                        node->original_pc);
                    ljit_stat_test_inline++;

                    if (node->dest.v.i == 1) {
                        /* 为真时跳转: INT 始终为真，直接跳转 */
                        test_next = node->next;
                        if (test_next && test_next->op == IR_JMP) {
                            ljit_cg_emit_jmp(test_next, ctx);
                            node = test_next;
                        }
                    }
                    /* k=0: 为假时跳转，INT 不可能是假，fall through */
                }
                /* 内联快速路径: BOOL 类型 */
                else if (src_type == JIT_TYPE_BOOL) {
                    /* BOOL 值: 0=false, 1=true */
                    fprintf(stderr, "[JIT-CODEGEN] IR_TEST pc=%d: inline BOOL test, src_type=%d\n",
                        node->original_pc, src_type);
                    ljit_stat_test_inline++;

                    /* 加载 src1 的值到 R0 */
                    ljit_cg_emit_load_operand(ctx, SLJIT_R0, &node->src1);
                    /* 比较: 非零即为真 */
                    sljit_s32 cond = (node->dest.v.i == 1) ? SLJIT_NOT_EQUAL : SLJIT_EQUAL;
                    bool_skip = sljit_emit_cmp(compiler, cond, SLJIT_R0, 0, SLJIT_IMM, 0);
                    test_next = node->next;
                    if (test_next && test_next->op == IR_JMP) {
                        ljit_cg_emit_jmp(test_next, ctx);
                        node = test_next;
                    }
                    bool_after = sljit_emit_label(compiler);
                    sljit_set_label(bool_skip, bool_after);
                }
                else {
                    /* 未知类型回退: 原有 icall 路径 */
                    fprintf(stderr, "[JIT-CODEGEN] IR_TEST pc=%d: unknown type, using icall\n",
                        node->original_pc);
                    ljit_stat_test_icall++;

                    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
                    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R1, 0, SLJIT_S0, 0, SLJIT_IMM, node->src1.v.reg * tvalue_size);
                    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, node->dest.v.i);
                    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3(32, W, W, 32), SLJIT_IMM, (sljit_sw)ljit_icall_test);
                    test_skip = sljit_emit_cmp(compiler, SLJIT_NOT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, 0);
                    test_next = node->next;
                    if (test_next && test_next->op == IR_JMP) {
                        ljit_cg_emit_jmp(test_next, ctx);
                        node = test_next;
                    }
                    test_after = sljit_emit_label(compiler);
                    sljit_set_label(test_skip, test_after);
                }
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
             * ============================================================
             * Fallback IR 操作码原生 icall 封装: 为原本走 codegen_fallback
             * 的 IR 操作码提供 icall 封装, 减少解释器回退.
             * ============================================================
             */

            /* IR_TESTNIL: nil 测试 - 原生 codegen (无需 icall) */
            case IR_TESTNIL: {
                JIT_DBG(MOD_CG, "TESTNIL: native codegen, pc=%d", node->original_pc);
                Instruction inst = ctx->proto->code[node->original_pc];
                int b = GETARG_B(inst);
                int k = GETARG_k(inst);
                int tvalue_size = sizeof(TValue);
                int value_size = sizeof(Value);

                /* 加载 R[B] 的 tt_ 字段 */
                sljit_emit_op1(compiler, SLJIT_MOV_U8, SLJIT_R0, 0,
                               SLJIT_MEM1(SLJIT_S0),
                               (sljit_sw)(b * tvalue_size + value_size));
                /* 比较是否为 LUA_TNIL (0) */
                struct sljit_jump *is_nil_jump = sljit_emit_cmp(compiler,
                    k ? SLJIT_EQUAL : SLJIT_NOT_EQUAL,
                    SLJIT_R0, 0, SLJIT_IMM, LUA_TNIL);
                /* 条件不满足 (跳过 JMP): 跳转到下一条 IR 指令 */
                struct sljit_jump *skip_jump = sljit_emit_jump(compiler, SLJIT_JUMP);
                if (skip_jump) {
                    int idx = ctx->num_jumps++;
                    ctx->jumps[idx] = skip_jump;
                    ctx->jump_targets[idx] = node->original_pc + 1;
                }
                /* 条件满足: 回退到通用 icall (处理 A != MAXARG_A 的复制逻辑) */
                struct sljit_label *nil_label = sljit_emit_label(compiler);
                sljit_set_label(is_nil_jump, nil_label);
                /* 调用 ljit_icall_testnil 处理条件满足时的逻辑 */
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_S0, 0);
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, node->original_pc);
                sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4V(W, W, 32, W),
                                 SLJIT_IMM, (sljit_sw)ljit_icall_testnil);
                break;
            }

            /* IR_IN: in 操作符 - icall 封装 */
            case IR_IN: {
                JIT_DBG(MOD_CG, "IN: icall, pc=%d", node->original_pc);
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_S0, 0);
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, node->original_pc);
                sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3V(W, W, 32),
                                 SLJIT_IMM, (sljit_sw)ljit_icall_in);
                break;
            }

            /* IR_IS: 类型检查 - icall 封装 */
            case IR_IS: {
                JIT_DBG(MOD_CG, "IS: icall, pc=%d", node->original_pc);
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_S0, 0);
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, node->original_pc);
                /* 分配栈空间存储 cond 结果 */
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R3, 0, SLJIT_SP, 0);
                sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4V(W, W, 32, W),
                                 SLJIT_IMM, (sljit_sw)ljit_icall_is);
                break;
            }

            /* IR_INSTANCEOF: instanceof 操作符 - icall 封装 */
            case IR_INSTANCEOF: {
                JIT_DBG(MOD_CG, "INSTANCEOF: icall, pc=%d", node->original_pc);
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_S0, 0);
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, node->original_pc);
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R3, 0, SLJIT_SP, 0);
                sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4V(W, W, 32, W),
                                 SLJIT_IMM, (sljit_sw)ljit_icall_instanceof);
                break;
            }

            /* IR_SLICE: 切片操作 - icall 封装 */
            case IR_SLICE: {
                JIT_DBG(MOD_CG, "SLICE: icall, pc=%d", node->original_pc);
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_S0, 0);
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, node->original_pc);
                sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3V(W, W, 32),
                                 SLJIT_IMM, (sljit_sw)ljit_icall_slice);
                break;
            }

            /* IR_GETPROP: 属性访问 - icall 封装 */
            case IR_GETPROP: {
                JIT_DBG(MOD_CG, "GETPROP: icall, pc=%d", node->original_pc);
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_S0, 0);
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, node->original_pc);
                sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3V(W, W, 32),
                                 SLJIT_IMM, (sljit_sw)ljit_icall_getprop);
                break;
            }

            /* IR_SETPROP: 属性设置 - icall 封装 */
            case IR_SETPROP: {
                JIT_DBG(MOD_CG, "SETPROP: icall, pc=%d", node->original_pc);
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_S0, 0);
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, node->original_pc);
                sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3V(W, W, 32),
                                 SLJIT_IMM, (sljit_sw)ljit_icall_setprop);
                break;
            }

            /* IR_SETSUPER: 父类设置 - icall 封装 */
            case IR_SETSUPER: {
                JIT_DBG(MOD_CG, "SETSUPER: icall, pc=%d", node->original_pc);
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_S0, 0);
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, node->original_pc);
                sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3V(W, W, 32),
                                 SLJIT_IMM, (sljit_sw)ljit_icall_setsuper);
                break;
            }

            /* IR_SPACESHIP: <=> 三路比较 - icall 封装 */
            case IR_SPACESHIP: {
                JIT_DBG(MOD_CG, "SPACESHIP: icall, pc=%d", node->original_pc);
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_S0, 0);
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, node->original_pc);
                sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3V(W, W, 32),
                                 SLJIT_IMM, (sljit_sw)ljit_icall_spaceship);
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
            case IR_GETVARG:
            case IR_IMPLEMENT:
            case IR_LINKNAMESPACE:
            case IR_NEWCONCEPT:
            case IR_NEWNAMESPACE:
            case IR_NEWSUPER:
            case IR_SETIFACEFLAG:
            case IR_SETMETHOD:
            case IR_SETSTATIC:
            case IR_MERGE:
            case IR_REGEX:
                JIT_DBG(MOD_CG, "NYI fallback: op=%d, pc=%d", node->op, node->original_pc);
                goto codegen_fallback;
            /*
             * IR_CJMP: 条件跳转.
             * 测试 src1 寄存器的真值性, 若条件满足则跳转到 dest.label_id.
             * src2.v.i = 0 表示"为假时跳转", 1 表示"为真时跳转".
             * 类似 IR_TEST 但直接跳转到标签, 无需通过后续 IR_JMP 中转.
             */
            case IR_CJMP: {
                int tvalue_size = sizeof(TValue);
                struct sljit_jump *cjmp_skip;
                struct sljit_jump *cjmp_jmp;
                struct sljit_label *cjmp_after;
                struct sljit_jump *cjmp_inline;
                struct sljit_jump *cjmp_bool;

                /* 读取操作数类型 */
                ljit_analyze_info_t *ainfo = (ljit_analyze_info_t *)ctx->analyze_info;
                int src_reg = node->src1.v.reg;
                ljit_type_t src_type = JIT_TYPE_ANY;
                if (ainfo && ainfo->reg_types && src_reg >= 0 && src_reg < ainfo->max_regs)
                    src_type = ainfo->reg_types[src_reg];

                int cond_k = node->src2.v.i;  /* 1=为真时跳转, 0=为假时跳转 */

                if (src_type == JIT_TYPE_INT) {
                    /* INT 始终为真 */
                    fprintf(stderr, "[JIT-CODEGEN] IR_CJMP pc=%d: inline INT test, cond=%d\n",
                        node->original_pc, cond_k);
                    ljit_stat_test_inline++;
                    if (cond_k == 1) {
                        /* INT 为真，条件成立，直接跳转 */
                        cjmp_inline = sljit_emit_jump(compiler, SLJIT_JUMP);
                        if (cjmp_inline) {
                            int idx = ctx->num_jumps++;
                            ctx->jumps[idx] = cjmp_inline;
                            ctx->jump_targets[idx] = node->dest.v.label_id;
                        }
                    }
                    /* cond_k=0: INT 不可能是假，fall through */
                }
                else if (src_type == JIT_TYPE_BOOL) {
                    fprintf(stderr, "[JIT-CODEGEN] IR_CJMP pc=%d: inline BOOL test, cond=%d\n",
                        node->original_pc, cond_k);
                    ljit_stat_test_inline++;
                    /* 加载 src1 的值 */
                    ljit_cg_emit_load_operand(ctx, SLJIT_R0, &node->src1);
                    sljit_s32 sljit_cond = (cond_k == 1) ? SLJIT_NOT_EQUAL : SLJIT_EQUAL;
                    cjmp_bool = sljit_emit_cmp(compiler, sljit_cond,
                        SLJIT_R0, 0, SLJIT_IMM, 0);
                    if (cjmp_bool) {
                        int idx = ctx->num_jumps++;
                        ctx->jumps[idx] = cjmp_bool;
                        ctx->jump_targets[idx] = node->dest.v.label_id;
                    }
                }
                else {
                    /* 未知类型回退: 原有 icall 路径 */
                    fprintf(stderr, "[JIT-CODEGEN] IR_CJMP pc=%d: unknown type, using icall\n",
                        node->original_pc);
                    ljit_stat_test_icall++;

                    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
                    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R1, 0, SLJIT_S0, 0,
                                   SLJIT_IMM, node->src1.v.reg * tvalue_size);
                    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, cond_k);
                    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3(32, W, W, 32),
                                     SLJIT_IMM, (sljit_sw)ljit_icall_test);
                    /* 若 icall 返回非零 (条件满足), 跳转到目标标签 */
                    cjmp_skip = sljit_emit_cmp(compiler, SLJIT_NOT_EQUAL,
                        SLJIT_R0, 0, SLJIT_IMM, 0);
                    /* 直接跳转到目标标签, 不依赖后续 IR_JMP */
                    cjmp_jmp = sljit_emit_jump(compiler, SLJIT_JUMP);
                    if (cjmp_jmp) {
                        int idx = ctx->num_jumps++;
                        ctx->jumps[idx] = cjmp_jmp;
                        ctx->jump_targets[idx] = node->dest.v.label_id;
                    }
                    cjmp_after = sljit_emit_label(compiler);
                    sljit_set_label(cjmp_skip, cjmp_after);
                    JIT_DBG(MOD_CG, "CJMP: src1=%d cond=%d -> label=%d", node->src1.v.reg,
                        cond_k, node->dest.v.label_id);
                }
                break;
            }
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
            default:
                /* 未识别的IR操作码：触发解释器回退，确保程序正确性 */
                JIT_DBG(MOD_CG, "UNKNOWN IR op=%d (pc=%d), falling back to interpreter",
                    node->op, node->original_pc);
                goto codegen_fallback;
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

    /* 打印 IR 操作码统计: 编译时输出每种 IR 操作码的出现次数 */
    fprintf(stderr, "[JIT-STATS] IR opcode counts for %s:\n", getstr(ctx->proto->source));
    const char *ir_op_names[] = {
        "NOP","MOV","LOADK","LOADI","LOADF","LOADNIL","LOADBOOL",
        "ADD","SUB","MUL","DIV","IDIV","MOD","POW",
        "BAND","BOR","BXOR","SHL","SHR",
        "UNM","BNOT","NOT",
        "CMP_LT","CMP_LE","CMP_EQ","CMP_GT","CMP_GE",
        "JMP","CJMP","RET",
        "NEWTABLE","GETTABLE","SETTABLE","NEWMAP","GETMAP","SETMAP","CALL",
        "CONCAT","TFORCALL","TFORLOOP","FORPREP","FORLOOP",
        "VARARG","VARARGPREP","NEWCLASS","NEWOBJ","CLOSURE",
        "GETUPVAL","SETUPVAL","GETTABUP","SETTABUP",
        "GETI","SETI","GETFIELD","SETFIELD",
        "LOADKX","SELF","ADDK","SUBK","MULK","MODK","POWK","DIVK","IDIVK","BANDK","BORK","BXORK",
        "SPACESHIP","LEN","CLOSE","TBC","EQK","TEST","TESTSET",
        "TFORPREP","SETLIST","GETVARG","ERRNNIL","IS","TESTNIL",
        "INHERIT","GETSUPER","SETMETHOD","SETSTATIC","GETPROP","SETPROP",
        "INSTANCEOF","IMPLEMENT","SETIFACEFLAG","ADDMETHOD","IN","SLICE",
        "CASE","NEWCONCEPT","NEWNAMESPACE","LINKNAMESPACE","NEWSUPER","SETSUPER",
        "GETCMDS","GETOPS","ASYNCWRAP","GENERICWRAP","CHECKTYPE","EXTRAARG",
        "SETTRAITFLAG","SETTRAITREQUIRE","USETRAIT","AWAIT","MERGE","REGEX"
    };
    for (int i = 0; i <= IR_REGEX; i++) {
        if (ir_op_count[i] > 0) {
            fprintf(stderr, "  [JIT-STATS]   %-14s: %d\n",
                i < (int)(sizeof(ir_op_names)/sizeof(ir_op_names[0])) ? ir_op_names[i] : "???",
                ir_op_count[i]);
        }
    }

    /* 打印 codegen 路径统计: 编译时各路径命中次数 */
    fprintf(stderr, "[JIT-STATS] Codegen path stats:\n");
    fprintf(stderr, "  [JIT-STATS]   INT_FASTPATH      : %d\n", ljit_stat_int_fastpath);
    fprintf(stderr, "  [JIT-STATS]   NUM_FASTPATH      : %d\n", ljit_stat_num_fastpath);
    fprintf(stderr, "  [JIT-STATS]   GUARDED_FASTPATH  : %d\n", ljit_stat_guarded_fastpath);
    fprintf(stderr, "  [JIT-STATS]   GENERIC           : %d\n", ljit_stat_generic);
    fprintf(stderr, "  [JIT-STATS]   TEST_INLINE       : %d\n", ljit_stat_test_inline);
    fprintf(stderr, "  [JIT-STATS]   TEST_ICALL        : %d\n", ljit_stat_test_icall);
    fprintf(stderr, "  [JIT-STATS]   CMP_INLINE        : %d\n", ljit_stat_cmp_inline);

    /* 打印 IR 优化统计 */
    ljit_opt_print_stats();

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
