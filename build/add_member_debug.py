path = r"E:\Soft\Proje\LXCLUA-NCore\lua\src\compiler\lparser.c"
with open(path, "r", encoding="utf-8") as f:
    data = f.read()

# Remove the luaC_checkGC after member loop (we'll add more granular ones)
data = data.replace('  luaC_checkGC(ls->L);\n  \n  /* create reflection', '  \n  /* create reflection')

# Add debug after member_names/member_values store
old1 = '    member_values[nh] = rec_val;\n    \n    /* 物化值到寄存器'
new1 = '    member_values[nh] = rec_val;\n    fprintf(stderr, "[enum] after array nh=%d name=%s\\n", nh, getstr(member_name));\n    luaC_checkGC(ls->L);\n    \n    /* 物化值到寄存器'
if old1 in data:
    data = data.replace(old1, new1, 1)
    print("OK: debug after array store")
else:
    print("NOT FOUND: after array")

# Add debug after table store
old2 = '        luaK_storevar(fs, &tab, &vreg);\n      }\n      /* 非作用域枚举'
new2 = '        luaK_storevar(fs, &tab, &vreg);\n      }\n      fprintf(stderr, "[enum] after table store nh=%d\\n", nh);\n      luaC_checkGC(ls->L);\n      /* 非作用域枚举'
if old2 in data:
    data = data.replace(old2, new2, 1)
    print("OK: debug after table store")
else:
    print("NOT FOUND: after table")

# Add debug after global store
old3 = '        luaK_storevar(fs, &gv, &vreg);\n      }\n      fs->freereg = enum_reg + 1;'
new3 = '        luaK_storevar(fs, &gv, &vreg);\n      }\n      fprintf(stderr, "[enum] after global store nh=%d\\n", nh);\n      luaC_checkGC(ls->L);\n      fs->freereg = enum_reg + 1;'
if old3 in data:
    data = data.replace(old3, new3, 1)
    print("OK: debug after global store")
else:
    print("NOT FOUND: after global")

with open(path, "w", encoding="utf-8", newline="") as f:
    f.write(data)
