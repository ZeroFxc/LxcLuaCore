import re

with open("src/vm/jit/codegen/ljit_cg_arith.c", "r") as f:
    content = f.read()

# We need to put back `ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;` in all the functions
functions = ["ljit_cg_emit_add", "ljit_cg_emit_mul", "ljit_cg_emit_div", "ljit_cg_emit_idiv", "ljit_cg_emit_mod", "ljit_cg_emit_sub", "ljit_cg_emit_unm", "ljit_cg_emit_not", "ljit_cg_emit_band", "ljit_cg_emit_bor", "ljit_cg_emit_bxor", "ljit_cg_emit_shl", "ljit_cg_emit_shr", "ljit_cg_emit_bnot"]

for func in functions:
    content = content.replace(f"void {func}(void *node_ptr, void *ctx_ptr) {{\n    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;\n        struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;",
                              f"void {func}(void *node_ptr, void *ctx_ptr) {{\n    ljit_ir_node_t *node = (ljit_ir_node_t *)node_ptr;\n    ljit_ctx_t *ctx = (ljit_ctx_t *)ctx_ptr;\n    struct sljit_compiler *compiler = (struct sljit_compiler *)ctx->compiler;")

with open("src/vm/jit/codegen/ljit_cg_arith.c", "w") as f:
    f.write(content)
