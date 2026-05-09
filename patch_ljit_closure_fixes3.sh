cat << 'INNER_EOF' > fix_ljit_closure6.py
import sys

def process_file(file_path):
    with open(file_path, "r") as f:
        content = f.read()

    # The macros are only in lvm.c, so we replace them with their equivalents or simply remove them for JIT ICALLs where pc is managed differently
    content = content.replace("savestate(L, L->ci);", "L->ci->u.l.savedpc = L->ci->func.p; L->top.p = L->ci->top.p;") # basic savepc
    content = content.replace("updatetrap(L->ci);", "/* updatetrap not needed for JIT ICALL */")

    # We can use lua_gettop to know how to adjust it if needed, but since we modify it directly...

    with open(file_path, "w") as f:
        f.write(content)

process_file("src/vm/jit/codegen/ljit_codegen.c")
INNER_EOF
python3 fix_ljit_closure6.py
