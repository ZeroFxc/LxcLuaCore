import sys

def process_file(file_path):
    with open(file_path, "r") as f:
        content = f.read()

    # The signature of luaF_close is: void luaF_close(lua_State *L, StkId level, int status, int yy) in lua 5.4,
    # but in our codebase: `StkId luaF_close(lua_State *L, StkId level, TStatus status, int yy);`
    # TStatus is from ltm.h or similar?
    # Let's see what CLOSEKTOP is in lvm.c

    content = content.replace("void SLJIT_FUNC ljit_icall_return(lua_State *L, StkId ra, int b, int c, int k)", "void SLJIT_FUNC ljit_icall_return(lua_State *L, StkId ra, int b, int c)")

    with open(file_path, "w") as f:
        f.write(content)

process_file("src/vm/jit/codegen/ljit_cg_ctrl.c")
