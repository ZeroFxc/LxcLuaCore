import sys

def process_file(file_path):
    with open(file_path, "r") as f:
        content = f.read()

    # Fix struct members: L->ci->func.p is StkId. The actual closure is clLvalue(s2v(L->ci->func.p)). The proto is clLvalue(...)->p.
    # Instruction i = clLvalue(s2v(L->ci->func.p))->p->code[original_pc];
    # We also need to include lobject.h, lopcodes.h

    content = content.replace("Instruction i = L->ci->func.p->c.p->code[original_pc];", "LClosure *cl = clLvalue(s2v(L->ci->func.p));\n    Instruction i = cl->p->code[original_pc];")
    content = content.replace("L->ci->u.l.savedpc = L->ci->func.p;", "")

    # Add includes
    content = content.replace("#include \"../../../core/ldo.h\"", "#include \"../../../core/ldo.h\"\n#include \"../../../core/lobject.h\"\n#include \"../../../core/lopcodes.h\"")

    with open(file_path, "w") as f:
        f.write(content)

process_file("src/vm/jit/codegen/ljit_cg_ctrl.c")
