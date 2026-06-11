import os

os.chdir(r'e:\Soft\Proje\LXCLUA-NCore\lua')

with open('src/compiler/lparser.c', 'r', encoding='utf-8', errors='replace') as f:
    content = f.read()

if 'DEBUG primaryexp' in content:
    print("Already patched!")
else:
    old = '''    default: {
      luaX_syntaxerror(ls, "unexpected symbol");
    }'''
    new = '''    default: {
      fprintf(stderr, "DEBUG primaryexp unexpected: token=%d, line=%d\\n", ls->t.token, ls->t.linenumber);
      luaX_syntaxerror(ls, "unexpected symbol");
    }'''
    if old in content:
        content = content.replace(old, new, 1)
        with open('src/compiler/lparser.c', 'w', encoding='utf-8') as f:
            f.write(content)
        print("Patched successfully!")
    else:
        print("Old pattern not found!")