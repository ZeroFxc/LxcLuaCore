path = r"E:\Soft\Proje\LXCLUA-NCore\lua\src\compiler\lparser.c"
with open(path, "r", encoding="utf-8") as f:
    data = f.read()

# Remove if(j!=3) wrapper around OP_CLOSURE
data = data.replace('        if (j != 3) {\n        expdesc mcl;', '        expdesc mcl;')
data = data.replace('        fs->freereg = enum_reg + 4;\n        }\n      }', '        fs->freereg = enum_reg + 4;\n      }')

# Add luaC_checkGC after luaC_objbarrier for j=3
old = '        luaC_objbarrier(ls->L, fs->f, mp);\n        expdesc mcl;'
new = '''        luaC_objbarrier(ls->L, fs->f, mp);
        if (j == 3) { fprintf(stderr, "[enum] j=3 proto created, bx=%d np=%d sizep=%d\\n", bx, fs->np, fs->f->sizep); luaC_checkGC(ls->L); fprintf(stderr, "[enum] j=3 GC passed\\n"); }
        expdesc mcl;'''

if old in data:
    data = data.replace(old, new, 1)
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write(data)
    print("OK: GC after j=3 proto")
else:
    print("NOT FOUND")
