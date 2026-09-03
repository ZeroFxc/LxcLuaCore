path = r"E:\Soft\Proje\LXCLUA-NCore\lua\src\compiler\lparser.c"
with open(path, "r", encoding="utf-8") as f:
    data = f.read()

old = '  luaK_storevar(fs, &v, &enum_exp);\n  \n  luaK_fixline(fs, line);\n}'
new = '''  luaK_storevar(fs, &v, &enum_exp);
  fprintf(stderr, "[enum] before final GC\\n");
  luaC_checkGC(ls->L);
  fprintf(stderr, "[enum] final GC passed\\n");

  luaK_fixline(fs, line);
}'''

if old in data:
    data = data.replace(old, new, 1)
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write(data)
    print("OK")
else:
    print("NOT FOUND")
