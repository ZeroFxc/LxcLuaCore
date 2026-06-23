/**
 * @file ljit_cg_ext.c
 * @brief JIT codegen for extended/unsupported opcodes: OOP, Trait, Slice, etc.
 * 通过icall委托C函数实现，保证JIT对未内联操作码的正确性.
 */

#include "ljit_codegen.h"
#include "../ir/ljit_ir.h"
#include "../sljit/ljit_sljit.h"
#include "../core/ljit_debug.h"
#include "../../../core/lobject.h"
#include "../../../core/lstate.h"
#include "../../../core/lfunc.h"
#include "../../../core/ldo.h"
#include "../../../core/ldebug.h"
#include "../../../core/lstring.h"
#include "../../../stdlib/lclass.h"
#include "../../../stdlib/lsuper.h"
#include "../../../utils/lnamespace.h"

/* =====================================================================
 * 内部icall函数声明
 * ===================================================================== */
void SLJIT_FUNC ljit_icall_spaceship(lua_State *L, StkId ra, TValue *rb, TValue *rc);
void SLJIT_FUNC ljit_icall_testnil(lua_State *L, StkId rb, int k, int a, int pc);
int SLJIT_FUNC ljit_icall_is(lua_State *L, StkId ra, TValue *rb, TValue *rc);
void SLJIT_FUNC ljit_icall_errnnil(lua_State *L, TValue *ra, int bx);
void SLJIT_FUNC ljit_icall_checktype(lua_State *L, StkId ra, TValue *rb, TValue *rc);
void SLJIT_FUNC ljit_icall_getprop(lua_State *L, StkId ra, TValue *rb, TString *key);
void SLJIT_FUNC ljit_icall_setprop(lua_State *L, StkId ra, TString *key, TValue *rc);
void SLJIT_FUNC ljit_icall_setmethod(lua_State *L, StkId ra, TString *key, TValue *rc);
void SLJIT_FUNC ljit_icall_setstatic(lua_State *L, StkId ra, TString *key, TValue *rc);
int SLJIT_FUNC ljit_icall_instanceof(lua_State *L, StkId ra, TValue *rb, int k);
void SLJIT_FUNC ljit_icall_implement(lua_State *L, StkId ra, TValue *rb);
void SLJIT_FUNC ljit_icall_setifaceflag(lua_State *L, StkId ra);
void SLJIT_FUNC ljit_icall_addmethod(lua_State *L, StkId ra, TString *name, int param_count);
void SLJIT_FUNC ljit_icall_in(lua_State *L, StkId ra, TValue *rb, TValue *rc);
void SLJIT_FUNC ljit_icall_settraitflag(lua_State *L, StkId ra);
void SLJIT_FUNC ljit_icall_settraitrequire(lua_State *L, StkId ra, TString *name, int nparams);
void SLJIT_FUNC ljit_icall_usetrait(lua_State *L, StkId ra, TValue *rb);
void SLJIT_FUNC ljit_icall_getvarg(lua_State *L, StkId ra, TValue *rc);
void SLJIT_FUNC ljit_icall_case(lua_State *L, StkId ra, TValue *rb, TValue *rc);
void SLJIT_FUNC ljit_icall_newconcept(lua_State *L, StkId ra, Proto *p);
void SLJIT_FUNC ljit_icall_newnamespace(lua_State *L, StkId ra, TString *name);
void SLJIT_FUNC ljit_icall_linknamespace(lua_State *L, StkId ra, StkId rb);
void SLJIT_FUNC ljit_icall_newsuper(lua_State *L, StkId ra, TString *name);
void SLJIT_FUNC ljit_icall_setsuper(lua_State *L, StkId ra, TValue *rb, TValue *rc);
void SLJIT_FUNC ljit_icall_getcmds(lua_State *L, StkId ra);
void SLJIT_FUNC ljit_icall_getops(lua_State *L, StkId ra);
void SLJIT_FUNC ljit_icall_asyncwrap(lua_State *L, StkId base, int b);
void SLJIT_FUNC ljit_icall_await(lua_State *L, StkId ra, StkId rb);
void SLJIT_FUNC ljit_icall_genericwrap(lua_State *L, StkId ra, int b);
void SLJIT_FUNC ljit_icall_merge(lua_State *L, StkId ra, TValue *rb, TValue *rc);
void SLJIT_FUNC ljit_icall_regex(lua_State *L, StkId ra, TString *ts);
void SLJIT_FUNC ljit_icall_slice(lua_State *L, StkId ra, int b);

/* icall 辅助函数 */
extern void SLJIT_FUNC ljit_icall_fallback(lua_State *L, StkId base);
/* 从 lvm.c 引用的辅助函数 */
extern int check_subtype_internal(lua_State *L, const TValue *val, const TValue *type_obj);
extern void pushconcept(lua_State *L, Proto *p, UpVal **encup, StkId base, StkId ra);
extern void inopr(lua_State *L, StkId ra, TValue *a, TValue *b);
extern int l_strcmp(const TString *ts1, const TString *ts2);
/** 获取当前函数的Proto */
static Proto *ljit_get_current_proto(lua_State *L) {
    return clLvalue(s2v(L->ci->func.p))->p;
}

/* =====================================================================
 * icall 实现
 * ===================================================================== */

/* ---- 基础操作码 ---- */

void SLJIT_FUNC ljit_icall_spaceship(lua_State *L, StkId ra, TValue *rb, TValue *rc) {
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
        return;
    }
    setivalue(s2v(ra), result);
}

void SLJIT_FUNC ljit_icall_testnil(lua_State *L, StkId rb, int k, int a, int pc) {
    LClosure *cl = clLvalue(s2v(L->ci->func.p));
    Instruction i = cl->p->code[pc];
    if (ttisnil(s2v(rb)) != k) {
        /* skip next instruction */
        L->ci->u.l.savedpc = &cl->p->code[pc + 2];
    } else {
        if (a != MAXARG_A) {
            setobj2s(L, L->ci->func.p + 1 + a, s2v(rb));
        }
        L->ci->u.l.savedpc = &cl->p->code[pc + 1];
    }
}

int SLJIT_FUNC ljit_icall_is(lua_State *L, StkId ra, TValue *rb, TValue *rc) {
    if (ttisstring(rc)) {
        const char *tname = getstr(tsvalue(rc));
        int result = 0;
        if (strcmp(tname, "nil") == 0) result = ttisnil(rb);
        else if (strcmp(tname, "number") == 0) result = ttisnumber(rb);
        else if (strcmp(tname, "integer") == 0) result = ttisinteger(rb);
        else if (strcmp(tname, "float") == 0) result = ttisfloat(rb);
        else if (strcmp(tname, "string") == 0) result = ttisstring(rb);
        else if (strcmp(tname, "table") == 0) result = ttistable(rb);
        else if (strcmp(tname, "function") == 0) result = ttisclosure(rb);
        else if (strcmp(tname, "boolean") == 0) result = ttisboolean(rb);
        else if (strcmp(tname, "userdata") == 0) result = ttisfulluserdata(rb);
        else if (strcmp(tname, "thread") == 0) result = ttisthread(rb);
        if (result) setbtvalue(s2v(ra)); else setbfvalue(s2v(ra));
        return result;
    }
    setbfvalue(s2v(ra));
    return 0;
}

void SLJIT_FUNC ljit_icall_errnnil(lua_State *L, TValue *ra, int bx) {
    if (!ttisnil(ra)) {
        LClosure *cl = clLvalue(s2v(L->ci->func.p));
        luaG_errnnil(L, cl, bx);
    }
}

void SLJIT_FUNC ljit_icall_checktype(lua_State *L, StkId ra, TValue *rb, TValue *rc) {
    /* Delegate to interpreter - complex type check with stack manipulation */
    if (!check_subtype_internal(L, s2v(ra), rb)) {
        const char *name = getstr(tsvalue(rc));
        const char *expected = "unknown";
        if (ttisstring(rb)) expected = getstr(tsvalue(rb));
        else if (ttistable(rb)) {
            Table *h = hvalue(rb);
            TString *key_name = luaS_newliteral(L, "__name");
            const TValue *res = luaH_getstr(h, key_name);
            if (ttisstring(res)) expected = getstr(tsvalue(res));
        }
        luaG_runerror(L, "Type mismatch for argument '%s': expected %s, got %s",
                       name, expected, luaT_objtypename(L, s2v(ra)));
    }
}

/* ---- OOP 操作码 ---- */

void SLJIT_FUNC ljit_icall_getprop(lua_State *L, StkId ra, TValue *rb, TString *key) {
    setobj2s(L, L->top.p, rb);
    L->top.p++;
    luaC_getprop(L, -1, key);
    setobj2s(L, ra, s2v(L->top.p - 1));
    L->top.p -= 2;
}

void SLJIT_FUNC ljit_icall_setprop(lua_State *L, StkId ra, TString *key, TValue *rc) {
    setobj2s(L, L->top.p, s2v(ra));
    L->top.p++;
    setobj2s(L, L->top.p, rc);
    L->top.p++;
    luaC_setprop(L, -2, key, -1);
    L->top.p -= 2;
}

void SLJIT_FUNC ljit_icall_setmethod(lua_State *L, StkId ra, TString *key, TValue *rc) {
    setobj2s(L, L->top.p, s2v(ra));
    L->top.p++;
    setobj2s(L, L->top.p, rc);
    L->top.p++;
    luaC_setmethod(L, -2, key, -1);
    L->top.p -= 2;
}

void SLJIT_FUNC ljit_icall_setstatic(lua_State *L, StkId ra, TString *key, TValue *rc) {
    setobj2s(L, L->top.p, s2v(ra));
    L->top.p++;
    setobj2s(L, L->top.p, rc);
    L->top.p++;
    luaC_setstatic(L, -2, key, -1);
    L->top.p -= 2;
}

int SLJIT_FUNC ljit_icall_instanceof(lua_State *L, StkId ra, TValue *rb, int k) {
    setobj2s(L, L->top.p, s2v(ra));
    L->top.p++;
    setobj2s(L, L->top.p, rb);
    L->top.p++;
    int result = luaC_instanceof(L, -2, -1);
    L->top.p -= 2;
    return (result != k) ? 1 : 0;
}

void SLJIT_FUNC ljit_icall_implement(lua_State *L, StkId ra, TValue *rb) {
    setobj2s(L, L->top.p, s2v(ra));
    L->top.p++;
    setobj2s(L, L->top.p, rb);
    L->top.p++;
    luaC_implement(L, -2, -1);
    L->top.p -= 2;
}

void SLJIT_FUNC ljit_icall_setifaceflag(lua_State *L, StkId ra) {
    if (ttistable(s2v(ra))) {
        Table *t = hvalue(s2v(ra));
        TValue key, val;
        setsvalue(L, &key, luaS_newliteral(L, "__flags"));
        const TValue *oldflags = luaH_getstr(t, tsvalue(&key));
        lua_Integer flags = ttisinteger(oldflags) ? ivalue(oldflags) : 0;
        flags |= CLASS_FLAG_INTERFACE;
        setivalue(&val, flags);
        luaH_set(L, t, &key, &val);
    }
}

void SLJIT_FUNC ljit_icall_addmethod(lua_State *L, StkId ra, TString *method_name, int param_count) {
    if (ttistable(s2v(ra))) {
        Table *t = hvalue(s2v(ra));
        TValue key;
        setsvalue(L, &key, luaS_newliteral(L, "__methods"));
        const TValue *methods_tv = luaH_getstr(t, tsvalue(&key));
        if (ttistable(methods_tv)) {
            Table *methods = hvalue(methods_tv);
            TValue method_key, method_val;
            setsvalue(L, &method_key, method_name);
            setivalue(&method_val, param_count);
            luaH_set(L, methods, &method_key, &method_val);
        }
    }
}

void SLJIT_FUNC ljit_icall_in(lua_State *L, StkId ra, TValue *rb, TValue *rc) {
    /* 直接调用 lvm.c 的 inopr 函数 */
    inopr(L, ra, rb, rc);
}

/* ---- Trait 操作码 ---- */

void SLJIT_FUNC ljit_icall_settraitflag(lua_State *L, StkId ra) {
    setobj2s(L, L->top.p, s2v(ra));
    L->top.p++;
    luaC_settraitflag(L, -1);
    L->top.p--;
}

void SLJIT_FUNC ljit_icall_settraitrequire(lua_State *L, StkId ra, TString *name, int nparams) {
    setobj2s(L, L->top.p, s2v(ra));
    L->top.p++;
    luaC_settraitrequire(L, -1, name, nparams);
    L->top.p--;
}

void SLJIT_FUNC ljit_icall_usetrait(lua_State *L, StkId ra, TValue *rb) {
    setobj2s(L, L->top.p, s2v(ra));
    L->top.p++;
    setobj2s(L, L->top.p, rb);
    L->top.p++;
    luaC_usetrait(L, -2, -1);
    L->top.p -= 2;
}

/* ---- 扩展操作码 ---- */

void SLJIT_FUNC ljit_icall_getvarg(lua_State *L, StkId ra, TValue *rc) {
    LClosure *cl = clLvalue(s2v(L->ci->func.p));
    luaT_getvararg(L, L->ci, ra, rc);
}

void SLJIT_FUNC ljit_icall_case(lua_State *L, StkId ra, TValue *rb, TValue *rc) {
    /* 创建表 t = {rb, rc} */
    Table *t = luaH_new(L);
    sethvalue2s(L, ra, t);
    luaH_setint(L, t, 1, rb);
    luaH_setint(L, t, 2, rc);
}

void SLJIT_FUNC ljit_icall_newconcept(lua_State *L, StkId ra, Proto *p) {
    LClosure *cl = clLvalue(s2v(L->ci->func.p));
    pushconcept(L, p, cl->upvals, ra, ra);
}

void SLJIT_FUNC ljit_icall_newnamespace(lua_State *L, StkId ra, TString *name) {
    Namespace *ns = luaN_new(L, name);
    setnsvalue(L, s2v(ra), ns);
}

void SLJIT_FUNC ljit_icall_linknamespace(lua_State *L, StkId ra, StkId rb) {
    if (ttisnamespace(s2v(ra)) && ttisnamespace(s2v(rb))) {
        Namespace *ns = nsvalue(s2v(ra));
        Namespace *target = nsvalue(s2v(rb));
        ns->using_next = target;
        luaC_objbarrier(L, ns, target);
    } else if (ttistable(s2v(ra)) && ttisnamespace(s2v(rb))) {
        Table *t = hvalue(s2v(ra));
        Namespace *target = nsvalue(s2v(rb));
        t->using_next = target;
        luaC_objbarrier(L, t, target);
    }
}

void SLJIT_FUNC ljit_icall_newsuper(lua_State *L, StkId ra, TString *name) {
    SuperStruct *ss = luaS_newsuperstruct(L, name, 0);
    setsuperstructvalue(L, s2v(ra), ss);
}

void SLJIT_FUNC ljit_icall_setsuper(lua_State *L, StkId ra, TValue *rb, TValue *rc) {
    if (ttissuperstruct(s2v(ra))) {
        SuperStruct *ss = superstructvalue(s2v(ra));
        luaS_setsuperstruct(L, ss, rb, rc);
    }
}

void SLJIT_FUNC ljit_icall_getcmds(lua_State *L, StkId ra) {
    Table *reg = hvalue(&G(L)->l_registry);
    TString *key = luaS_newliteral(L, "LXC_CMDS");
    const TValue *res = luaH_getstr(reg, key);
    if (!isempty(res)) {
        setobj2s(L, ra, res);
    } else {
        Table *t = luaH_new(L);
        sethvalue2s(L, ra, t);
        TValue val; sethvalue(L, &val, t);
        if (reg->is_shared) l_rwlock_wrlock(&reg->lock);
        TValue k; setsvalue(L, &k, key);
        luaH_set(L, reg, &k, &val);
        luaC_barrierback(L, obj2gco(reg), &val);
        if (reg->is_shared) l_rwlock_unlock(&reg->lock);
    }
}

void SLJIT_FUNC ljit_icall_getops(lua_State *L, StkId ra) {
    Table *reg = hvalue(&G(L)->l_registry);
    TString *key = luaS_newliteral(L, "LXC_OPERATORS");
    const TValue *res;
    if (reg->is_shared) l_rwlock_rdlock(&reg->lock);
    res = luaH_getstr(reg, key);
    if (!isempty(res)) {
        setobj2s(L, ra, res);
        if (reg->is_shared) l_rwlock_unlock(&reg->lock);
    } else {
        if (reg->is_shared) l_rwlock_unlock(&reg->lock);
        Table *t = luaH_new(L);
        sethvalue2s(L, ra, t);
        TValue val; sethvalue(L, &val, t);
        if (reg->is_shared) l_rwlock_wrlock(&reg->lock);
        TValue k; setsvalue(L, &k, key);
        luaH_set(L, reg, &k, &val);
        luaC_barrierback(L, obj2gco(reg), &val);
        if (reg->is_shared) l_rwlock_unlock(&reg->lock);
    }
}

void SLJIT_FUNC ljit_icall_asyncwrap(lua_State *L, StkId base, int b) {
    TValue *rb = s2v(base + b);
    if (ttisLclosure(rb)) {
        clLvalue(rb)->p->flag |= PF_ASYNC;
    }
}

void SLJIT_FUNC ljit_icall_await(lua_State *L, StkId ra, StkId rb) {
    TValue *await_val = s2v(rb);
    if (ttisfulluserdata(await_val)) {
        /* Promise值：挂起协程 */
        setobj2s(L, L->top.p, await_val);
        L->top.p++;
        L->ci->callstatus |= CIST_AWAIT;
        L->status = LUA_YIELD;
        L->ci->u2.nyield = 1;
        luaD_throw(L, LUA_YIELD);
    } else {
        /* 普通值：直接存入R[A] */
        setobj2s(L, ra, await_val);
    }
}

extern int lvm_generic_call(lua_State *L);
void SLJIT_FUNC ljit_icall_genericwrap(lua_State *L, StkId ra, int b) {
    StkId base = L->ci->func.p + 1;
    StkId base_args = base + b;
    CClosure *ncl = luaF_newCclosure(L, 3);
    ncl->f = lvm_generic_call;
    setobj(L, &ncl->upvalue[0], s2v(base_args));
    setobj(L, &ncl->upvalue[1], s2v(base_args + 1));
    setobj(L, &ncl->upvalue[2], s2v(base_args + 2));
    setclCvalue(L, s2v(ra), ncl);
    setclLvalue2s(L, ra + 1, clLvalue(s2v(L->ci->func.p)));
    setobj2s(L, ra + 2, s2v(base_args));
    setobj2s(L, ra + 3, s2v(base_args + 1));
    setobj2s(L, ra + 4, s2v(base_args + 2));
}

void SLJIT_FUNC ljit_icall_merge(lua_State *L, StkId ra, TValue *rb, TValue *rc) {
    if (l_unlikely(!ttistable(rb) || !ttistable(rc))) {
        luaG_runerror(L, "attempt to merge non-table values");
        return;
    }
    Table *t1 = hvalue(rb);
    Table *t2 = hvalue(rc);
    Table *result = luaH_new(L);

    /* 复制第一个表 */
    if (t1->alimit > 0) {
        unsigned int j;
        for (j = 0; j < t1->alimit; j++) {
            TValue *v = &t1->array[j];
            if (!ttisnil(v))
                luaH_setint(L, result, (lua_Integer)(j + 1), v);
        }
    }
    if (t1->lsizenode > 0) {
        unsigned int j;
        for (j = 0; j < (1u << t1->lsizenode); j++) {
            Node *n = gnode(t1, j);
            if (!ttisnil(gval(n))) {
                TValue k;
                getnodekey(L, &k, n);
                luaH_set(L, result, &k, gval(n));
            }
        }
    }
    /* 复制第二个表（覆盖同名键） */
    if (t2->alimit > 0) {
        unsigned int j;
        for (j = 0; j < t2->alimit; j++) {
            TValue *v = &t2->array[j];
            if (!ttisnil(v))
                luaH_setint(L, result, (lua_Integer)(j + 1), v);
        }
    }
    if (t2->lsizenode > 0) {
        unsigned int j;
        for (j = 0; j < (1u << t2->lsizenode); j++) {
            Node *n = gnode(t2, j);
            if (!ttisnil(gval(n))) {
                TValue k;
                getnodekey(L, &k, n);
                luaH_set(L, result, &k, gval(n));
            }
        }
    }
    sethvalue2s(L, ra, result);
}

void SLJIT_FUNC ljit_icall_regex(lua_State *L, StkId ra, TString *ts) {
    const char *data = getstr(ts);
    size_t patlen = strlen(data);
    const char *flags = data + patlen + 1;
    Table *t = luaH_new(L);
    TValue key, val;
    TString *pat_str = luaS_newlstr(L, data, patlen);
    TString *flag_str = luaS_newlstr(L, flags, strlen(flags));
    setsvalue2n(L, &key, luaS_newliteral(L, "pattern"));
    setsvalue2n(L, &val, pat_str);
    luaH_set(L, t, &key, &val);
    setsvalue2n(L, &key, luaS_newliteral(L, "flags"));
    setsvalue2n(L, &val, flag_str);
    luaH_set(L, t, &key, &val);
    sethvalue2s(L, ra, t);
}

void SLJIT_FUNC ljit_icall_slice(lua_State *L, StkId ra, int b) {
    StkId base = L->ci->func.p + 1;
    StkId base_reg = base + b;
    TValue *src_table = s2v(base_reg);
    TValue *start_val = s2v(base_reg + 1);
    TValue *end_val = s2v(base_reg + 2);
    TValue *step_val = s2v(base_reg + 3);

    if (l_unlikely(!ttistable(src_table))) {
        luaG_typeerror(L, src_table, "slice");
        return;
    }
    Table *t = hvalue(src_table);
    lua_Integer tlen = luaH_getn(t);
    lua_Integer start_idx, end_idx, step;

    /* 解析起始索引 */
    if (ttisnil(start_val)) start_idx = 1;
    else if (ttisinteger(start_val)) start_idx = ivalue(start_val);
    else { luaG_runerror(L, "slice start must be integer or nil"); return; }
    if (start_idx < 0) start_idx = tlen + start_idx + 1;
    if (start_idx < 1) start_idx = 1;

    /* 解析结束索引 */
    if (ttisnil(end_val)) end_idx = tlen;
    else if (ttisinteger(end_val)) end_idx = ivalue(end_val);
    else { luaG_runerror(L, "slice end must be integer or nil"); return; }
    if (end_idx < 0) end_idx = tlen + end_idx + 1;
    if (end_idx > tlen) end_idx = tlen;

    /* 解析步长 */
    if (ttisnil(step_val)) step = 1;
    else if (ttisinteger(step_val)) step = ivalue(step_val);
    else { luaG_runerror(L, "slice step must be integer or nil"); return; }
    if (step == 0) { luaG_runerror(L, "slice step cannot be zero"); return; }

    Table *result_t = luaH_new(L);
    lua_Integer result_idx = 1;
    if (step > 0) {
        for (lua_Integer i = start_idx; i <= end_idx; i += step) {
            const TValue *val = luaH_getint(t, i);
            if (!ttisnil(val)) {
                TValue temp;
                setobj(L, &temp, val);
                luaH_setint(L, result_t, result_idx, &temp);
            }
            result_idx++;
        }
    } else {
        for (lua_Integer i = start_idx; i >= end_idx; i += step) {
            const TValue *val = luaH_getint(t, i);
            if (!ttisnil(val)) {
                TValue temp;
                setobj(L, &temp, val);
                luaH_setint(L, result_t, result_idx, &temp);
            }
            result_idx++;
        }
    }
    sethvalue2s(L, ra, result_t);
}

/* =====================================================================
 * Emitter 函数
 * ===================================================================== */

#define T_VALUE_SIZE ((int)sizeof(TValue))

/* 辅助宏：计算寄存器地址 */
#define ADD_REG_ADDR(dst_reg, base_reg, vreg) \
    sljit_emit_op2(compiler, SLJIT_ADD, dst_reg, 0, base_reg, 0, SLJIT_IMM, (vreg) * T_VALUE_SIZE)

/* ---- 基础操作码 Emitters ---- */

void ljit_cg_emit_spaceship(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
    ADD_REG_ADDR(SLJIT_R1, SLJIT_S0, node->dest.v.reg);
    ADD_REG_ADDR(SLJIT_R2, SLJIT_S0, node->src1.v.reg);
    ADD_REG_ADDR(SLJIT_R3, SLJIT_S0, node->src2.v.reg);
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4(W, W, W, W, W),
                     SLJIT_IMM, (sljit_sw)ljit_icall_spaceship);
}

void ljit_cg_emit_testnil(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    int k = node->dest.v.i;
    int a = node->src2.v.i;

    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
    ADD_REG_ADDR(SLJIT_R1, SLJIT_S0, node->src1.v.reg);
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, k);
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R3, 0, SLJIT_IMM, a);
    /* 第5个参数pc通过栈传递 */
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_S2, 0, SLJIT_IMM, node->original_pc);
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS5(W, W, W, W, W, W),
                     SLJIT_IMM, (sljit_sw)ljit_icall_testnil);
}

void ljit_cg_emit_is(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
    ADD_REG_ADDR(SLJIT_R1, SLJIT_S0, node->dest.v.reg);
    ADD_REG_ADDR(SLJIT_R2, SLJIT_S0, node->src1.v.reg);
    ADD_REG_ADDR(SLJIT_R3, SLJIT_S0, node->src2.v.reg);
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4(W, W, W, W, W),
                     SLJIT_IMM, (sljit_sw)ljit_icall_is);
}

void ljit_cg_emit_errnnil(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
    ADD_REG_ADDR(SLJIT_R1, SLJIT_S0, node->dest.v.reg);
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, node->src1.v.i);
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3(W, W, W, W),
                     SLJIT_IMM, (sljit_sw)ljit_icall_errnnil);
}

void ljit_cg_emit_checktype(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
    ADD_REG_ADDR(SLJIT_R1, SLJIT_S0, node->dest.v.reg);
    ADD_REG_ADDR(SLJIT_R2, SLJIT_S0, node->src1.v.reg);
    /* src2 is constant index */
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R3, 0, SLJIT_IMM, (sljit_sw)&ctx->proto->k[node->src2.v.k]);
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4(W, W, W, W, W),
                     SLJIT_IMM, (sljit_sw)ljit_icall_checktype);
}

/* ---- OOP 操作码 Emitters ---- */

void ljit_cg_emit_getprop(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    TString *key = tsvalue(&ctx->proto->k[node->src2.v.k]);

    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
    ADD_REG_ADDR(SLJIT_R1, SLJIT_S0, node->dest.v.reg);
    ADD_REG_ADDR(SLJIT_R2, SLJIT_S0, node->src1.v.reg);
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R3, 0, SLJIT_IMM, (sljit_sw)key);
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4(W, W, W, W, W),
                     SLJIT_IMM, (sljit_sw)ljit_icall_getprop);
}

void ljit_cg_emit_setprop(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    TString *key = tsvalue(&ctx->proto->k[node->src1.v.k]);

    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
    ADD_REG_ADDR(SLJIT_R1, SLJIT_S0, node->dest.v.reg);
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, (sljit_sw)key);
    ADD_REG_ADDR(SLJIT_R3, SLJIT_S0, node->src2.v.reg);
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4(W, W, W, W, W),
                     SLJIT_IMM, (sljit_sw)ljit_icall_setprop);
}

void ljit_cg_emit_setmethod(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    TString *key = tsvalue(&ctx->proto->k[node->src1.v.k]);

    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
    ADD_REG_ADDR(SLJIT_R1, SLJIT_S0, node->dest.v.reg);
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, (sljit_sw)key);
    ADD_REG_ADDR(SLJIT_R3, SLJIT_S0, node->src2.v.reg);
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4(W, W, W, W, W),
                     SLJIT_IMM, (sljit_sw)ljit_icall_setmethod);
}

void ljit_cg_emit_setstatic(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    TString *key = tsvalue(&ctx->proto->k[node->src1.v.k]);

    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
    ADD_REG_ADDR(SLJIT_R1, SLJIT_S0, node->dest.v.reg);
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, (sljit_sw)key);
    ADD_REG_ADDR(SLJIT_R3, SLJIT_S0, node->src2.v.reg);
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4(W, W, W, W, W),
                     SLJIT_IMM, (sljit_sw)ljit_icall_setstatic);
}

void ljit_cg_emit_instanceof(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    int k = node->src2.v.i;

    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
    ADD_REG_ADDR(SLJIT_R1, SLJIT_S0, node->dest.v.reg);
    ADD_REG_ADDR(SLJIT_R2, SLJIT_S0, node->src1.v.reg);
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R3, 0, SLJIT_IMM, k);
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4(W, W, W, W, W),
                     SLJIT_IMM, (sljit_sw)ljit_icall_instanceof);
    /* R0: 1 = should jump (cond != k), 0 = fall through */
    struct sljit_jump *jmp = sljit_emit_cmp(compiler, SLJIT_NOT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, 0);
    if (jmp) {
        int idx = ctx->num_jumps++;
        ctx->jumps[idx] = jmp;
        ctx->jump_targets[idx] = node->original_pc + 2;
    }
}

void ljit_cg_emit_implement(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
    ADD_REG_ADDR(SLJIT_R1, SLJIT_S0, node->dest.v.reg);
    ADD_REG_ADDR(SLJIT_R2, SLJIT_S0, node->src1.v.reg);
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3(W, W, W, W),
                     SLJIT_IMM, (sljit_sw)ljit_icall_implement);
}

void ljit_cg_emit_setifaceflag(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
    ADD_REG_ADDR(SLJIT_R1, SLJIT_S0, node->dest.v.reg);
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS2(W, W, W),
                     SLJIT_IMM, (sljit_sw)ljit_icall_setifaceflag);
}

void ljit_cg_emit_addmethod(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    TString *name = tsvalue(&ctx->proto->k[node->src1.v.k]);

    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
    ADD_REG_ADDR(SLJIT_R1, SLJIT_S0, node->dest.v.reg);
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, (sljit_sw)name);
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R3, 0, SLJIT_IMM, node->src2.v.i);
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4(W, W, W, W, W),
                     SLJIT_IMM, (sljit_sw)ljit_icall_addmethod);
}

void ljit_cg_emit_in(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
    ADD_REG_ADDR(SLJIT_R1, SLJIT_S0, node->dest.v.reg);
    ADD_REG_ADDR(SLJIT_R2, SLJIT_S0, node->src1.v.reg);
    ADD_REG_ADDR(SLJIT_R3, SLJIT_S0, node->src2.v.reg);
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4(W, W, W, W, W),
                     SLJIT_IMM, (sljit_sw)ljit_icall_in);
}

/* ---- Trait 操作码 Emitters ---- */

void ljit_cg_emit_settraitflag(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
    ADD_REG_ADDR(SLJIT_R1, SLJIT_S0, node->dest.v.reg);
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS2(W, W, W),
                     SLJIT_IMM, (sljit_sw)ljit_icall_settraitflag);
}

void ljit_cg_emit_settraitrequire(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    TString *name = tsvalue(&ctx->proto->k[node->src1.v.k]);

    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
    ADD_REG_ADDR(SLJIT_R1, SLJIT_S0, node->dest.v.reg);
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, (sljit_sw)name);
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R3, 0, SLJIT_IMM, node->src2.v.i);
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4(W, W, W, W, W),
                     SLJIT_IMM, (sljit_sw)ljit_icall_settraitrequire);
}

void ljit_cg_emit_usetrait(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
    ADD_REG_ADDR(SLJIT_R1, SLJIT_S0, node->dest.v.reg);
    ADD_REG_ADDR(SLJIT_R2, SLJIT_S0, node->src1.v.reg);
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3(W, W, W, W),
                     SLJIT_IMM, (sljit_sw)ljit_icall_usetrait);
}

/* ---- 扩展操作码 Emitters ---- */

void ljit_cg_emit_getvarg(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
    ADD_REG_ADDR(SLJIT_R1, SLJIT_S0, node->dest.v.reg);
    ADD_REG_ADDR(SLJIT_R2, SLJIT_S0, node->src1.v.reg);
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3(W, W, W, W),
                     SLJIT_IMM, (sljit_sw)ljit_icall_getvarg);
}

void ljit_cg_emit_case(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
    ADD_REG_ADDR(SLJIT_R1, SLJIT_S0, node->dest.v.reg);
    ADD_REG_ADDR(SLJIT_R2, SLJIT_S0, node->src1.v.reg);
    ADD_REG_ADDR(SLJIT_R3, SLJIT_S0, node->src2.v.reg);
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4(W, W, W, W, W),
                     SLJIT_IMM, (sljit_sw)ljit_icall_case);
}

void ljit_cg_emit_newconcept(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    Proto *p = ctx->proto->p[node->src1.v.i];

    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
    ADD_REG_ADDR(SLJIT_R1, SLJIT_S0, node->dest.v.reg);
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, (sljit_sw)p);
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3(W, W, W, W),
                     SLJIT_IMM, (sljit_sw)ljit_icall_newconcept);
}

void ljit_cg_emit_newnamespace(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    TString *name = tsvalue(&ctx->proto->k[node->src1.v.k]);

    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
    ADD_REG_ADDR(SLJIT_R1, SLJIT_S0, node->dest.v.reg);
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, (sljit_sw)name);
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3(W, W, W, W),
                     SLJIT_IMM, (sljit_sw)ljit_icall_newnamespace);
}

void ljit_cg_emit_linknamespace(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
    ADD_REG_ADDR(SLJIT_R1, SLJIT_S0, node->dest.v.reg);
    ADD_REG_ADDR(SLJIT_R2, SLJIT_S0, node->src1.v.reg);
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3(W, W, W, W),
                     SLJIT_IMM, (sljit_sw)ljit_icall_linknamespace);
}

void ljit_cg_emit_newsuper(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    TString *name = tsvalue(&ctx->proto->k[node->src1.v.k]);

    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
    ADD_REG_ADDR(SLJIT_R1, SLJIT_S0, node->dest.v.reg);
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, (sljit_sw)name);
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3(W, W, W, W),
                     SLJIT_IMM, (sljit_sw)ljit_icall_newsuper);
}

void ljit_cg_emit_setsuper(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
    ADD_REG_ADDR(SLJIT_R1, SLJIT_S0, node->dest.v.reg);
    ADD_REG_ADDR(SLJIT_R2, SLJIT_S0, node->src1.v.reg);
    ADD_REG_ADDR(SLJIT_R3, SLJIT_S0, node->src2.v.reg);
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4(W, W, W, W, W),
                     SLJIT_IMM, (sljit_sw)ljit_icall_setsuper);
}

void ljit_cg_emit_getcmds(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
    ADD_REG_ADDR(SLJIT_R1, SLJIT_S0, node->dest.v.reg);
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS2(W, W, W),
                     SLJIT_IMM, (sljit_sw)ljit_icall_getcmds);
}

void ljit_cg_emit_getops(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
    ADD_REG_ADDR(SLJIT_R1, SLJIT_S0, node->dest.v.reg);
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS2(W, W, W),
                     SLJIT_IMM, (sljit_sw)ljit_icall_getops);
}

void ljit_cg_emit_asyncwrap(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    int b = node->dest.v.i;

    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_S0, 0);
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, b);
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3(W, W, W, W),
                     SLJIT_IMM, (sljit_sw)ljit_icall_asyncwrap);
}

void ljit_cg_emit_await(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
    ADD_REG_ADDR(SLJIT_R1, SLJIT_S0, node->dest.v.reg);
    ADD_REG_ADDR(SLJIT_R2, SLJIT_S0, node->src1.v.reg);
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3(W, W, W, W),
                     SLJIT_IMM, (sljit_sw)ljit_icall_await);
}

void ljit_cg_emit_genericwrap(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    int b = node->src1.v.i;

    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
    ADD_REG_ADDR(SLJIT_R1, SLJIT_S0, node->dest.v.reg);
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, b);
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3(W, W, W, W),
                     SLJIT_IMM, (sljit_sw)ljit_icall_genericwrap);
}

void ljit_cg_emit_merge(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
    ADD_REG_ADDR(SLJIT_R1, SLJIT_S0, node->dest.v.reg);
    ADD_REG_ADDR(SLJIT_R2, SLJIT_S0, node->src1.v.reg);
    ADD_REG_ADDR(SLJIT_R3, SLJIT_S0, node->src2.v.reg);
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4(W, W, W, W, W),
                     SLJIT_IMM, (sljit_sw)ljit_icall_merge);
}

void ljit_cg_emit_regex(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    TString *ts = tsvalue(&ctx->proto->k[node->src1.v.i]);

    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
    ADD_REG_ADDR(SLJIT_R1, SLJIT_S0, node->dest.v.reg);
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, (sljit_sw)ts);
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3(W, W, W, W),
                     SLJIT_IMM, (sljit_sw)ljit_icall_regex);
}

void ljit_cg_emit_slice(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    int b = node->src1.v.i;

    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
    ADD_REG_ADDR(SLJIT_R1, SLJIT_S0, node->dest.v.reg);
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, b);
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3(W, W, W, W),
                     SLJIT_IMM, (sljit_sw)ljit_icall_slice);
}

/* EXTRAARG: 编译期标记，运行时不应出现 */
void ljit_cg_emit_extraarg(void *node_ptr, void *ctx_ptr) {
    (void)node_ptr; (void)ctx_ptr;
    /* EXTRAARG 在编译期被断言，JIT不做处理 */
}