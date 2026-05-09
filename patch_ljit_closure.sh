cat << 'INNER_EOF' >> src/vm/jit/codegen/ljit_codegen.c

void SLJIT_FUNC ljit_icall_closure(lua_State *L, Proto *p, UpVal **encup, StkId base, StkId ra) {
    luaV_pushclosure(L, p, encup, base, ra);
    luaC_checkGC(L);
}
INNER_EOF
