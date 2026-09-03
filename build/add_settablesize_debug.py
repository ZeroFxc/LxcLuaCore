path = r"E:\Soft\Proje\LXCLUA-NCore\lua\src\compiler\lparser.c"
with open(path, "r", encoding="utf-8") as f:
    data = f.read()

old = '  /* 设置表大小 */\n  luaK_settablesize(fs, pc, enum_reg, 0, nh);\n  luaC_checkGC(ls->L);'
new = '  fprintf(stderr, "[enum] before settablesize nh=%d\\n", nh);\n  luaC_checkGC(ls->L);\n  /* 设置表大小 */\n  luaK_settablesize(fs, pc, enum_reg, 0, nh);\n  fprintf(stderr, "[enum] after settablesize\\n");\n  luaC_checkGC(ls->L);'

if old in data:
    data = data.replace(old, new, 1)
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write(data)
    print("OK")
else:
    print("NOT FOUND")
