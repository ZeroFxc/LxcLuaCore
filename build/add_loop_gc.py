path = r"E:\Soft\Proje\LXCLUA-NCore\lua\src\compiler\lparser.c"
with open(path, "r", encoding="utf-8") as f:
    data = f.read()

# Remove j=3 GC debug
old_j3 = '''        if (j == 3) { fprintf(stderr, "[enum] j=3 proto created, bx=%d np=%d sizep=%d\\n", bx, fs->np, fs->f->sizep); luaC_checkGC(ls->L); fprintf(stderr, "[enum] j=3 GC passed\\n"); }
'''
data = data.replace(old_j3, '')

# Add GC after method closure loop
old = '''    }
    fs->freereg = enum_reg + 1;
  }
  luaM_freearray'''
new = '''    }
    fprintf(stderr, "[enum] after method loop, np=%d sizep=%d\\n", fs->np, fs->f->sizep);
    luaC_checkGC(ls->L);
    fprintf(stderr, "[enum] after method loop GC passed\\n");
    fs->freereg = enum_reg + 1;
  }
  luaM_freearray'''

if old in data:
    data = data.replace(old, new, 1)
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write(data)
    print("OK: GC after method loop")
else:
    print("NOT FOUND")
    # Debug: find the text
    import re
    m = re.search(r'fs->freereg = enum_reg \+ 1;\s*\n\s*\}\s*\n\s*luaM_freearray', data)
    if m:
        print(f"Found at {m.start()}")
        print(repr(data[m.start():m.start()+80]))
