path = r"E:\Soft\Proje\LXCLUA-NCore\lua\src\compiler\lparser.c"
with open(path, "r", encoding="utf-8") as f:
    data = f.read()

old = '    case TK_ENUM: {  /* stat -> enumstat */\n      enumstat(ls, line, 0);\n      break;'
new = '''    case TK_ENUM: {  /* stat -> enumstat */
      enumstat(ls, line, 0);
      fprintf(stderr, "[enum] returned, GC check\\n");
      luaC_checkGC(ls->L);
      fprintf(stderr, "[enum] post-return GC passed\\n");
      break;'''

if old in data:
    data = data.replace(old, new, 1)
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write(data)
    print("OK")
else:
    print("NOT FOUND")
