path = r"E:\Soft\Proje\LXCLUA-NCore\lua\src\compiler\lparser.c"
with open(path, "r", encoding="utf-8") as f:
    data = f.read()

# Remove all debug fprintf and luaC_checkGC in enumstat area
import re

# Remove fprintf lines
data = re.sub(r'\s*fprintf\(stderr, "\[enum\].*?\);\n', '\n', data)

# Remove luaC_checkGC lines (but keep the one after fs->f->p[bx] = mp if it's luaC_objbarrier)
data = re.sub(r'\s*luaC_checkGC\(ls->L\);\n', '\n', data)

with open(path, "w", encoding="utf-8", newline="") as f:
    f.write(data)
print("All debug removed")
