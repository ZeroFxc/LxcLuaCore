import sys

def process_file(file_path):
    with open(file_path, "r") as f:
        content = f.read()

    content = content.replace("case IR_CLOSURE: ljit_cg_emit_closure(node, ctx); break;\n            case IR_CLOSURE: ljit_cg_emit_closure(node, ctx); break;", "case IR_CLOSURE: ljit_cg_emit_closure(node, ctx); break;")

    with open(file_path, "w") as f:
        f.write(content)

process_file("src/vm/jit/codegen/ljit_codegen.c")
