cat << 'INNER_EOF' > fix_ljit_closure11.py
import sys

def process_file(file_path):
    with open(file_path, "r") as f:
        content = f.read()

    # The current ljit_codegen.c has old implementations of ICALLs that we need to clean up and re-implement correctly based on lvm.c

    content = content.replace("void SLJIT_FUNC ljit_icall_newclass(lua_State *L, TString *classname) {\n    luaC_newclass(L, classname);\n}\n\nvoid SLJIT_FUNC ljit_icall_newobj(lua_State *L, int nargs, StkId rb, StkId ra) {\n    L->ci->u.l.savedpc = L->ci->u.l.savedpc; L->top.p = L->ci->top.p;\n    setobj2s(L, L->top.p, s2v(rb));\n    L->top.p++;\n    for (int j = 0; j < nargs; j++) {\n        setobj2s(L, L->top.p, s2v(ra + 1 + j));\n        L->top.p++;\n    }\n    luaC_newobject(L, -(nargs + 1), nargs);\n    /* base handled below */\n    setobj2s(L, ra, s2v(L->top.p - 1));\n    L->top.p -= 1;\n    /* updatetrap not needed for JIT ICALL */\n}\n\nvoid SLJIT_FUNC ljit_icall_inherit(lua_State *L, StkId rb, StkId ra) {\n    L->ci->u.l.savedpc = L->ci->u.l.savedpc; L->top.p = L->ci->top.p;\n    setobj2s(L, L->top.p, s2v(ra));\n    L->top.p++;\n    setobj2s(L, L->top.p, s2v(rb));\n    L->top.p++;\n    luaC_inherit(L, -2, -1);\n    L->top.p -= 2;\n    /* updatetrap not needed for JIT ICALL */\n}\n\nvoid SLJIT_FUNC ljit_icall_getsuper(lua_State *L, TString *key, StkId rb, StkId ra) {\n    L->ci->u.l.savedpc = L->ci->u.l.savedpc; L->top.p = L->ci->top.p;\n    setobj2s(L, L->top.p, s2v(rb));\n    L->top.p++;\n    luaC_super(L, -1, key);\n    /* base handled below */\n    setobj2s(L, ra, s2v(L->top.p - 1));\n    L->top.p -= 2;\n    /* updatetrap not needed for JIT ICALL */\n}", "void SLJIT_FUNC ljit_icall_newclass(lua_State *L, TString *classname) {\n    luaC_newclass(L, classname);\n}\n\nvoid SLJIT_FUNC ljit_icall_newobj(lua_State *L, int nargs, StkId rb, StkId ra) {\n    L->ci->u.l.savedpc = L->ci->func.p;\n    setobj2s(L, L->top.p, s2v(rb));\n    L->top.p++;\n    for (int j = 0; j < nargs; j++) {\n        setobj2s(L, L->top.p, s2v(ra + 1 + j));\n        L->top.p++;\n    }\n    luaC_newobject(L, -(nargs + 1), nargs);\n    setobj2s(L, ra, s2v(L->top.p - 1));\n    L->top.p -= 1;\n}\n\nvoid SLJIT_FUNC ljit_icall_inherit(lua_State *L, StkId rb, StkId ra) {\n    L->ci->u.l.savedpc = L->ci->func.p;\n    setobj2s(L, L->top.p, s2v(ra));\n    L->top.p++;\n    setobj2s(L, L->top.p, s2v(rb));\n    L->top.p++;\n    luaC_inherit(L, -2, -1);\n    L->top.p -= 2;\n}\n\nvoid SLJIT_FUNC ljit_icall_getsuper(lua_State *L, TString *key, StkId rb, StkId ra) {\n    L->ci->u.l.savedpc = L->ci->func.p;\n    setobj2s(L, L->top.p, s2v(rb));\n    L->top.p++;\n    luaC_super(L, -1, key);\n    setobj2s(L, ra, s2v(L->top.p - 1));\n    L->top.p -= 2;\n}")

    # Remove the extra closure include
    content = content.replace("void SLJIT_FUNC ljit_icall_closure(lua_State *L, Proto *p, UpVal **encup, StkId base, StkId ra) {\n    luaV_pushclosure(L, p, encup, base, ra);\n    luaC_checkGC(L);\n}", "void SLJIT_FUNC ljit_icall_closure(lua_State *L, Proto *p, StkId base, StkId ra) {\n    LClosure *cl = clLvalue(s2v(L->ci->func.p));\n    luaV_pushclosure(L, p, cl->upvals, base, ra);\n    luaC_checkGC(L);\n}")

    with open(file_path, "w") as f:
        f.write(content)

process_file("src/vm/jit/codegen/ljit_codegen.c")
INNER_EOF
python3 fix_ljit_closure11.py
