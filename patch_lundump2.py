import re

with open('lundump.c', 'r') as f:
    content = f.read()

content = re.sub(
    r'(  VMP_MARKER\(lundump_vmp\);\n)',
    r'',
    content
)

content = re.sub(
    r'(LClosure \*luaU_undump\(lua_State \*L, ZIO \*Z, const char \*name, int force_standard\) {\n)',
    r'\1  VMP_MARKER(lundump_vmp);\n',
    content
)

with open('lundump.c', 'w') as f:
    f.write(content)
