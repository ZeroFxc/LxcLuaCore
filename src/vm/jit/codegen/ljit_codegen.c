#include "ljit_codegen.h"
#include "../core/ljit_internal.h"
#include "../ir/ljit_ir.h"
#include "../sljit/ljit_sljit.h"
#include <stdlib.h>

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

/* Fallback for newly added IR nodes */
            case IR_ADDK:
            case IR_ADDMETHOD:
            case IR_ASYNCWRAP:
            case IR_BANDK:
            case IR_BORK:
            case IR_BXORK:
            case IR_CASE:
            case IR_CHECKTYPE:
            case IR_CLOSE:
            case IR_DIVK:
            case IR_EQK:
            case IR_ERRNNIL:
            case IR_EXTRAARG:
            case IR_GENERICWRAP:
            case IR_GETCMDS:
            case IR_GETOPS:
            case IR_GETPROP:
            case IR_GETSUPER:
            case IR_GETVARG:
            case IR_IDIVK:
            case IR_IMPLEMENT:
            case IR_IN:
            case IR_INHERIT:
            case IR_INSTANCEOF:
            case IR_IS:
            case IR_LEN:
            case IR_LINKNAMESPACE:
            case IR_LOADKX:
            case IR_MODK:
            case IR_MULK:
            case IR_NEWCONCEPT:
            case IR_NEWNAMESPACE:
            case IR_NEWSUPER:
            case IR_POWK:
            case IR_SELF:
            case IR_SETIFACEFLAG:
            case IR_SETLIST:
            case IR_SETMETHOD:
            case IR_SETPROP:
            case IR_SETSTATIC:
            case IR_SETSUPER:
            case IR_SLICE:
            case IR_SPACESHIP:
            case IR_SUBK:
            case IR_TBC:
            case IR_TEST:
            case IR_TESTNIL:
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
            case IR_CONCAT:
            case IR_TFORCALL:
            case IR_TFORLOOP:
            case IR_FORPREP:
            case IR_FORLOOP:
            case IR_VARARG:
            case IR_VARARGPREP:
            case IR_NEWCLASS:
            case IR_NEWOBJ:
            case IR_CLOSURE:
                /* Explicit early exit (interpreter fallback) for unsupported complex ops */
                sljit_emit_return_void((struct sljit_compiler *)ctx->compiler);
                break;

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
