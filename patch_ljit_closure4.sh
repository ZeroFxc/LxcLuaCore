cat << 'INNER_EOF' >> src/vm/jit/codegen/ljit_codegen.c

void SLJIT_FUNC ljit_icall_newclass(lua_State *L, TString *classname) {
    luaC_newclass(L, classname);
}

void SLJIT_FUNC ljit_icall_newobj(lua_State *L, int nargs, StkId rb, StkId ra) {
    savestate(L, L->ci);
    setobj2s(L, L->top.p, s2v(rb));
    L->top.p++;
    for (int j = 0; j < nargs; j++) {
        setobj2s(L, L->top.p, s2v(ra + 1 + j));
        L->top.p++;
    }
    luaC_newobject(L, -(nargs + 1), nargs);
    base = L->ci->func.p + 1;
    setobj2s(L, s2v(ra), s2v(L->top.p - 1));
    L->top.p -= 1;
    updatetrap(L->ci);
}

void SLJIT_FUNC ljit_icall_inherit(lua_State *L, StkId rb, StkId ra) {
    savestate(L, L->ci);
    setobj2s(L, L->top.p, s2v(ra));
    L->top.p++;
    setobj2s(L, L->top.p, s2v(rb));
    L->top.p++;
    luaC_inherit(L, -2, -1);
    L->top.p -= 2;
    updatetrap(L->ci);
}

void SLJIT_FUNC ljit_icall_getsuper(lua_State *L, TString *key, StkId rb, StkId ra) {
    savestate(L, L->ci);
    setobj2s(L, L->top.p, s2v(rb));
    L->top.p++;
    luaC_super(L, -1, key);
    base = L->ci->func.p + 1;
    setobj2s(L, s2v(ra), s2v(L->top.p - 1));
    L->top.p -= 2;
    updatetrap(L->ci);
}
INNER_EOF
