import sys

def process_file(file_path):
    with open(file_path, "r") as f:
        content = f.read()

    # The previous script broke the switch statement even more:
    # case IR_GETSUPER:
    # ...
    # case IR_GETSUPER: ljit_cg_emit_getsuper(node, ctx); break;
    # It missed IR_GETSUPER and IR_INHERIT duplicate

    content = content.replace("            case IR_GETSUPER:\n                ljit_cg_emit_store_operand((struct ljit_ctx *)ctx, &node->dest, SLJIT_R0);\n                break;\n", "")
    content = content.replace("            case IR_INHERIT:\n                ljit_cg_emit_store_operand((struct ljit_ctx *)ctx, &node->dest, SLJIT_R0);\n                break;\n", "")

    # Actually wait, let's just find and replace the empty cases.

    with open(file_path, "w") as f:
        f.write(content)

process_file("src/vm/jit/codegen/ljit_codegen.c")
