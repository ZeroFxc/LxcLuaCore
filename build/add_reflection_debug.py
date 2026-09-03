path = r"E:\Soft\Proje\LXCLUA-NCore\lua\src\compiler\lparser.c"
with open(path, "r", encoding="utf-8") as f:
    data = f.read()

# Add debug after reflection creation, before method closures
old = '''      fs->freereg = enum_reg + 4;
    }
      /* 创建方法闭包: names/values/kvmap/vkmap */
    {'''

new = '''      fs->freereg = enum_reg + 4;
    }
    fprintf(stderr, "[enum] after reflection, before methods\\n");
    luaC_checkGC(ls->L);
      /* 创建方法闭包: names/values/kvmap/vkmap */
    {'''

if old in data:
    data = data.replace(old, new, 1)
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write(data)
    print("OK: debug after reflection")
else:
    print("NOT FOUND")
    # Try to find the exact text
    import re
    m = re.search(r'fs->freereg = enum_reg \+ 4;\s*\n\s*\}\s*\n\s*/\*.*方法闭包', data)
    if m:
        print(f"Found at position {m.start()}")
        print(repr(data[m.start():m.start()+100]))
    else:
        print("Not found with regex either")
