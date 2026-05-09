import sys

def process_file(file_path):
    with open(file_path, "r") as f:
        content = f.read()

    content = content.replace("case IR_GETSUPER:\n            case IR_INHERIT:\n            case IR_NEWCLASS:\n            case IR_NEWOBJ:\n                /* Explicit early exit (interpreter fallback) for unsupported complex ops */\n                sljit_emit_return_void((struct sljit_compiler *)ctx->compiler);\n                break;", "case IR_GETSUPER: ljit_cg_emit_getsuper(node, ctx); break;\n            case IR_INHERIT: ljit_cg_emit_inherit(node, ctx); break;\n            case IR_NEWCLASS: ljit_cg_emit_newclass(node, ctx); break;\n            case IR_NEWOBJ: ljit_cg_emit_newobj(node, ctx); break;")

    with open(file_path, "w") as f:
        f.write(content)

process_file("src/vm/jit/codegen/ljit_codegen.c")
