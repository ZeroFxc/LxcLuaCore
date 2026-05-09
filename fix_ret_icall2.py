import sys

def process_file(file_path):
    with open(file_path, "r") as f:
        content = f.read()

    # The actual ICALL implementation missing the proper arguments and CLOSEKTOP macro.
    # In `lvm.c` OP_RETURN does:
    # int n = GETARG_B(i) - 1;
    # int nparams1 = GETARG_C(i);
    # ...
    # luaF_close(L, base, CLOSEKTOP, 1);
    # We can fetch GETARG_k from pc. Wait, in ljit we don't have pc easily.
    # We can just avoid closing upvalues if we don't know k. If k is required, we could just fall back.
    # But let's check ljit_translate.c, how IR_RET is formed.
    # OP_RETURN: src1.v.reg = A, src2.v.i = B - 1, dest.v.i = C (Wait, dest.v.i is not set for C in translate!)
    # Actually if we can't reliably implement it without full bytecode info, we should just gracefully return void from the JIT to let interpreter handle OP_RETURN, as OP_RETURN terminating the trace is perfectly fine. The interpreter will take over, execute the return, and go back to caller.
    # The prompt explicitly asks to "尽量避免陷入原生解释器" (Try to avoid falling back to the interpreter).
    # So let's implement the ICALL fully by extracting original instruction.

    icall_impl = """
void SLJIT_FUNC ljit_icall_return(lua_State *L, StkId ra, int b, int original_pc) {
    Instruction i = L->ci->func.p->c.p->code[original_pc];
    int n = b; /* b is already B - 1 or correct count */
    if (n < 0) n = cast_int(L->top.p - ra);

    L->ci->u.l.savedpc = L->ci->func.p;

    if (TESTARG_k(i)) {
        L->ci->u2.nres = n;
        if (L->top.p < L->ci->top.p) L->top.p = L->ci->top.p;
        luaF_close(L, L->ci->func.p + 1, CLOSEKTOP, 1);
    }

    int c = GETARG_C(i);
    if (c) {
        L->ci->func.p -= L->ci->u.l.nextraargs + c;
    }

    L->top.p = ra + n;
    luaD_poscall(L, L->ci, n);
}
"""

    # replace the previous ljit_icall_return
    import re
    content = re.sub(r'void SLJIT_FUNC ljit_icall_return\(.*?\).*?luaD_poscall\(L, L->ci, n\);\n}', icall_impl, content, flags=re.DOTALL)

    # Update ljit_cg_emit_ret
    cg_emit = """
void ljit_cg_emit_ret(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    int tvalue_size = sizeof(TValue);

    /* R0 = L */
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);

    /* R1 = ra (base + src1.v.reg * size if OP_RETURN, but it might be OP_RETURN0 or OP_RETURN1) */
    /* Let's just pass base and compute ra in ICALL or compute it here */
    if (node->src1.type == IR_VAL_REG) {
        sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R1, 0, SLJIT_S0, 0, SLJIT_IMM, node->src1.v.reg * tvalue_size);
    } else {
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_S0, 0); /* fallback to base */
    }

    /* R2 = B (src2.v.i) */
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, node->src2.v.i);

    /* R3 = original_pc */
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R3, 0, SLJIT_IMM, node->original_pc);

    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4V(W, W, W, W), SLJIT_IMM, (sljit_sw)ljit_icall_return);
    sljit_emit_return_void(compiler);
}
"""
    content = re.sub(r'void ljit_cg_emit_ret\(void \*node_ptr, void \*ctx_ptr\).*?sljit_emit_return_void\(compiler\);\n}', cg_emit, content, flags=re.DOTALL)

    with open(file_path, "w") as f:
        f.write(content)

process_file("src/vm/jit/codegen/ljit_cg_ctrl.c")
