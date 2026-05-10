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

void SLJIT_FUNC ljit_icall_newtable(lua_State *L, int b, int c, StkId ra) {
    Table *t = luaH_new(L);
    sethvalue2s(L, ra, t);
    if (b != 0 || c != 0)
        luaH_resize(L, t, c, b);
    luaC_step(L);
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


void SLJIT_FUNC ljit_icall_len(lua_State *L, StkId ra, TValue *rb) {
    luaV_objlen(L, ra, rb);
}



#include "../../../core/lopcodes.h"



void ljit_cg_emit_len(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM, node->original_pc);

    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS2V(W, W), SLJIT_IMM, (sljit_sw)ljit_icall_len);
}

void *ljit_codegen(void *ctx_ptr) {
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    if (!ctx) return NULL;

    struct sljit_compiler *compiler = sljit_create_compiler(NULL);
    if (!compiler) return NULL;

    ctx->compiler = compiler;
    int max_labels = ctx->proto->sizecode + ctx->next_label_id + 1;
    ctx->labels = (struct sljit_label **)calloc(max_labels, sizeof(struct sljit_label *));

    // Allocate space for up to max_labels jumps (worst case)
    ctx->jumps = (struct sljit_jump **)calloc(max_labels, sizeof(struct sljit_jump *));
    ctx->jump_targets = (int *)calloc(max_labels, sizeof(int));
    ctx->num_jumps = 0;

    /*
     * Enter function arguments mapping:
     * jit_func_t(StkId base) -> SLJIT_ARGS1V(W) -> base in SLJIT_S0.
     * SLJIT_S0 will hold the Lua virtual register base address.
     * Requesting 3 args, 7 scratch, 0 fregs to accommodate S1, S2, R0-R3.
     */
    sljit_emit_enter(compiler, 0, SLJIT_ARGS1V(W), 4, 3, 0);

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
            case IR_RET: ljit_cg_emit_ret(node, ctx); break;
            case IR_GETTABLE: ljit_cg_emit_gettable(node, ctx); break;
            case IR_SETTABLE: ljit_cg_emit_settable(node, ctx); break;
            case IR_CALL: ljit_cg_emit_call(node, ctx); break;
            case IR_NEWTABLE: ljit_cg_emit_newtable(node, ctx); break;
            case IR_POW: ljit_cg_emit_pow(node, ctx); break;
            case IR_NOP: ljit_cg_emit_nop(node, ctx); break;

/* Fallback for newly added IR nodes */
            case IR_ADDMETHOD:
            case IR_ASYNCWRAP:
            case IR_CASE:
            case IR_CHECKTYPE:
            case IR_CLOSE:
            case IR_EQK:
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
            case IR_LOADKX:
            case IR_NEWCONCEPT:
            case IR_NEWNAMESPACE:
            case IR_NEWSUPER:
            case IR_SELF:
            case IR_SETIFACEFLAG:
            case IR_SETLIST:
            case IR_SETMETHOD:
            case IR_SETPROP:
            case IR_SETSTATIC:
            case IR_SETSUPER:
            case IR_SLICE:
            case IR_SPACESHIP:
            case IR_TBC:
            case IR_TEST:
            case IR_TESTNIL:
            case IR_LEN: ljit_cg_emit_len(node, ctx); break;
            case IR_TESTSET:
            case IR_TFORPREP:
            case IR_GETUPVAL:
            case IR_SETUPVAL:
            case IR_GETTABUP:
            case IR_SETTABUP:
            case IR_GETI:
            case IR_SETI:
            case IR_GETFIELD:
            case IR_SETFIELD:
            case IR_TFORCALL:
            case IR_TFORLOOP:
            case IR_VARARG:
            case IR_VARARGPREP:
                sljit_emit_return_void((struct sljit_compiler *)ctx->compiler);
                break;
            case IR_GETSUPER: ljit_cg_emit_getsuper(node, ctx); break;
            case IR_INHERIT: ljit_cg_emit_inherit(node, ctx); break;
            case IR_NEWCLASS: ljit_cg_emit_newclass(node, ctx); break;
            case IR_NEWOBJ: ljit_cg_emit_newobj(node, ctx); break;
            case IR_CLOSURE: ljit_cg_emit_closure(node, ctx); break;

            case IR_CONCAT: ljit_cg_emit_concat(node, ctx); break;
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
