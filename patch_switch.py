import sys

def process_file(file_path):
    with open(file_path, "r") as f:
        content = f.read()

    # The previous script broke the switch statement:
    # case IR_NEWCLASS:
    # case IR_NEWOBJ:
    # case IR_CLOSURE: ljit_cg_emit_closure(node, ctx); break;
    # It missed IR_GETSUPER and IR_INHERIT

    target = """            case IR_NEWCLASS:
            case IR_NEWOBJ:
            case IR_CLOSURE: ljit_cg_emit_closure(node, ctx); break;"""

    replacement = """            case IR_GETSUPER: ljit_cg_emit_getsuper(node, ctx); break;
            case IR_INHERIT: ljit_cg_emit_inherit(node, ctx); break;
            case IR_NEWCLASS: ljit_cg_emit_newclass(node, ctx); break;
            case IR_NEWOBJ: ljit_cg_emit_newobj(node, ctx); break;
            case IR_CLOSURE: ljit_cg_emit_closure(node, ctx); break;"""

    if target in content:
        content = content.replace(target, replacement)

    with open(file_path, "w") as f:
        f.write(content)

process_file("src/vm/jit/codegen/ljit_codegen.c")
