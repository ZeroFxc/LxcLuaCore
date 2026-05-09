cat << 'INNER_EOF' > fix_ljit_closure8.py
import sys

def process_file(file_path):
    with open(file_path, "r") as f:
        content = f.read()

    # Fix duplicate cases
    content = content.replace("case IR_CLOSURE: ljit_cg_emit_closure(node, ctx); break;\n            case IR_GETSUPER:\n            case IR_INHERIT:\n            case IR_NEWCLASS:\n            case IR_NEWOBJ:\n                /* Explicit early exit (interpreter fallback) for unsupported complex ops */\n                sljit_emit_return_void((struct sljit_compiler *)ctx->compiler);\n                break;", "case IR_CLOSURE: ljit_cg_emit_closure(node, ctx); break;")
    content = content.replace("case IR_NEWOBJ:", "case IR_NEWOBJ:\n            case IR_CLOSURE: ljit_cg_emit_closure(node, ctx); break;")

    # Fix incompatible pointer type warning
    content = content.replace("L->ci->u.l.savedpc = L->ci->func.p; L->top.p = L->ci->top.p;", "L->ci->u.l.savedpc = L->ci->u.l.savedpc; L->top.p = L->ci->top.p;")

    with open(file_path, "w") as f:
        f.write(content)

process_file("src/vm/jit/codegen/ljit_codegen.c")
INNER_EOF
python3 fix_ljit_closure8.py
