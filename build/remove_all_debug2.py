path = r"E:\Soft\Proje\LXCLUA-NCore\lua\src\compiler\lparser.c"
with open(path, "r", encoding="utf-8") as f:
    data = f.read()

import re

# Remove all fprintf lines containing [enum] or [self]
data = re.sub(r'\s*fprintf\(stderr, "\[enum\].*?\);\n', '\n', data)
data = re.sub(r'\s*fprintf\(stderr, "\[self\].*?\);\n', '\n', data)

# Remove all luaC_checkGC lines (but keep luaC_objbarrier)
data = re.sub(r'\s*luaC_checkGC\(ls->L\);\n', '\n', data)

# Remove the post-return debug in statement
old = '''    case TK_ENUM: {  /* stat -> enumstat */
      enumstat(ls, line, 0);

      break;'''
# The above might not match due to removed lines. Let's just check.

with open(path, "w", encoding="utf-8", newline="") as f:
    f.write(data)
print("All debug removed")
