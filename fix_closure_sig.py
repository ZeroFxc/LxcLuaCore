import sys

def process_file(file_path):
    with open(file_path, "r") as f:
        content = f.read()

    # The signature in ljit_cg_closure.c was not updated when we changed it in ljit_codegen.c
    content = content.replace("void SLJIT_FUNC ljit_icall_closure(lua_State *L, Proto *p, UpVal **encup, StkId base, StkId ra);", "void SLJIT_FUNC ljit_icall_closure(lua_State *L, Proto *p, StkId base, StkId ra);")

    with open(file_path, "w") as f:
        f.write(content)

process_file("src/vm/jit/codegen/ljit_cg_closure.c")
