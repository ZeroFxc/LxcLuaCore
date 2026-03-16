import re

with open('lundump.c', 'r') as f:
    content = f.read()

content = re.sub(
    r'(LClosure \*luaU_undump\(lua_State \*L, ZIO \*Z, const char \*name, int force_standard\) {\n  VMP_MARKER\(lundump_vmp\);\n)',
    r'LClosure *luaU_undump(lua_State *L, ZIO *Z, const char *name, int force_standard) {\n',
    content
)

content = re.sub(
    r'(LClosure \*luaU_undump\(lua_State \*L, ZIO \*Z, const char \*name, int force_standard\) {\n  LoadState S;\n)',
    r'\1',
    content
)

content = re.sub(
    r'(  S.mem_offset = 0;\n  checkHeader\(&S\);\n)',
    r'  VMP_MARKER(lundump_vmp);\n\1',
    content
)

with open('lundump.c', 'w') as f:
    f.write(content)
