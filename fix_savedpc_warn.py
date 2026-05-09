import sys

def process_file(file_path):
    with open(file_path, "r") as f:
        content = f.read()

    # The warning is: assignment to 'const Instruction *' from 'StkId'
    # Actually we just don't need to savepc because the stack frames aren't changing PC.
    # L->ci->u.l.savedpc is for error reporting. L->ci->func.p is the function.
    # Let's use lua_State *L, savepc(L) using the standard macro or simply remove the assignment.
    # Let's remove the assignment `L->ci->u.l.savedpc = L->ci->func.p;` since we don't have pc available.

    content = content.replace("L->ci->u.l.savedpc = L->ci->func.p;\n", "")

    with open(file_path, "w") as f:
        f.write(content)

process_file("src/vm/jit/codegen/ljit_codegen.c")
