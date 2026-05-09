cat << 'INNER_EOF' > fix_ljit_closure5.py
import sys

def process_file(file_path):
    with open(file_path, "r") as f:
        content = f.read()

    # setobj2s already handles the first argument being StkId
    # The error is because we did setobj2s(L, s2v(ra), ...) and setobj2s expands to setobj(L, s2v(o1), o2)
    # So we get s2v(s2v(ra))

    content = content.replace("setobj2s(L, s2v(ra), s2v(L->top.p - 1));", "setobj2s(L, ra, s2v(L->top.p - 1));")

    # Add includes to resolve warnings
    content = content.replace("void SLJIT_FUNC ljit_icall_newclass", "#include \"../../../stdlib/lclass.h\"\nvoid SLJIT_FUNC ljit_icall_newclass")

    with open(file_path, "w") as f:
        f.write(content)

process_file("src/vm/jit/codegen/ljit_codegen.c")
INNER_EOF
python3 fix_ljit_closure5.py
