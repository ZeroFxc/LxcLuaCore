sed -i 's/static void pushclosure/void luaV_pushclosure/g' src/vm/lvm.c
sed -i 's/pushclosure(L, p, cl->upvals, base, ra)/luaV_pushclosure(L, p, cl->upvals, base, ra)/g' src/vm/lvm.c
echo "LUAI_FUNC void luaV_pushclosure(lua_State *L, Proto *p, UpVal **encup, StkId base, StkId ra);" >> src/vm/lvm.h
