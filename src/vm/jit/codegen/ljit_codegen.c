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

void *ljit_codegen(void *ctx_ptr) {
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    if (!ctx) return NULL;

    /*
     * Check for TESTSET + CALL pattern that JIT cannot handle.
     * When TESTSET in an AND/OR expression guards a CALL,
     * the IR_CALL is no-op in JIT but the subsequent IR_RET
     * pops the call frame via luaD_poscall, losing the result.
     * Fall back to interpreter for these functions.
     */
    {
        ljit_ir_node_t *n = ctx->ir_head;
        while (n) {
            if (n->op == IR_TESTSET) {
                ljit_ir_node_t *next = n->next;
                if (next && next->op == IR_JMP) next = next->next;
                ljit_ir_node_t *body = next;
                while (body) {
                    if (body->op == IR_CALL) return NULL;
                    if (body->op == IR_RET) break;
                    body = body->next;
                }
            }
            n = n->next;
        }
    }

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
     * jit_func_t(StkId base) -> SLJIT_ARGS1V(W) -> base in SLJIT_S0.
     * SLJIT_S0 will hold the Lua virtual register base address.
     * Requesting 4 saved regs (S0-S3), 4 scratch regs (R0-R3), 0 fregs.
     */
sljit_emit_enter(compiler, 0, SLJIT_ARGS1V(W), 4, 4, 0);

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
            case IR_RET: /* JIT writes values to stack; interpreter handles actual return */ break;
            case IR_GETTABLE: ljit_cg_emit_gettable(node, ctx); break;
            case IR_SETTABLE: ljit_cg_emit_settable(node, ctx); break;
            case IR_CALL: /* JIT sets up args on stack; interpreter handles actual call */ break;
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

    sljit_emit_return_void(compiler);

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
