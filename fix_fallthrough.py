import sys

def process_file(file_path):
    with open(file_path, "r") as f:
        content = f.read()

    # The issue is the cases like IR_VARARGPREP, IR_VARARG fall through to IR_GETSUPER and then call ljit_cg_emit_getsuper!
    # Because IR_GETSUPER is right after IR_VARARGPREP without a break before it or default handler for unsupported ops.
    # Initially we had:
    # case IR_VARARGPREP:
    # case IR_NEWCLASS: ...
    # /* Explicit early exit (interpreter fallback) for unsupported complex ops */
    # sljit_emit_return_void((struct sljit_compiler *)ctx->compiler);
    # break;
    # We replaced it with break handlers for each, but we left the unsupported ops (like IR_TFORLOOP, IR_VARARG, IR_VARARGPREP, etc) falling through to IR_GETSUPER.

    content = content.replace("            case IR_VARARGPREP:\n            case IR_GETSUPER: ljit_cg_emit_getsuper(node, ctx); break;", "            case IR_VARARGPREP:\n                sljit_emit_return_void((struct sljit_compiler *)ctx->compiler);\n                break;\n            case IR_GETSUPER: ljit_cg_emit_getsuper(node, ctx); break;")

    with open(file_path, "w") as f:
        f.write(content)

process_file("src/vm/jit/codegen/ljit_codegen.c")
