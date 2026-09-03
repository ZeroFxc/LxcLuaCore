path = r"E:\Soft\Proje\LXCLUA-NCore\lua\src\compiler\lparser.c"
with open(path, "r", encoding="utf-8") as f:
    data = f.read()

old = '''        codename(ls, &key);
        luaK_self(fs, v, &key);
        funcargs(ls, v, line);'''
new = '''        codename(ls, &key);
        fprintf(stderr, "[self] before luaK_self\\n");
        luaC_checkGC(ls->L);
        luaK_self(fs, v, &key);
        fprintf(stderr, "[self] after luaK_self, before funcargs\\n");
        luaC_checkGC(ls->L);
        funcargs(ls, v, line);
        fprintf(stderr, "[self] after funcargs\\n");
        luaC_checkGC(ls->L);'''

if old in data:
    data = data.replace(old, new, 1)
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write(data)
    print("OK")
else:
    print("NOT FOUND")
