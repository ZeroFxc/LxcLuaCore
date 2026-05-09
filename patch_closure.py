import sys

def process_file(file_path):
    with open(file_path, "r") as f:
        content = f.read()

    # In ljit_cg_closure.c, we need to correctly implement the ICALL for closure creation.
    replacement = """void ljit_cg_emit_closure(void *node_ptr, void *ctx_ptr) {
    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;
    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;
    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;
    if (!node || !ctx || !compiler) return;

    int tvalue_size = sizeof(TValue);

    /* R0 = L */
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)ctx->L);

    /* R1 = Proto *p */
    Proto *p = ctx->proto->p[node->src1.v.i];
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM, (sljit_sw)p);

    /* R2 = base */
    sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_S0, 0);

    /* R3 = ra (base + dest.v.reg * size) */
    sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R3, 0, SLJIT_S0, 0, SLJIT_IMM, node->dest.v.reg * tvalue_size);

    /* Call ljit_icall_closure */
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4V(W, W, W, W), SLJIT_IMM, (sljit_sw)ljit_icall_closure);
}"""

    import re
    content = re.sub(r'void ljit_cg_emit_closure\(void \*node_ptr, void \*ctx_ptr\).*?\}', replacement, content, flags=re.DOTALL)

    with open(file_path, "w") as f:
        f.write(content)

process_file("src/vm/jit/codegen/ljit_cg_closure.c")
