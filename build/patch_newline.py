# -*- coding: utf-8 -*-
"""Add newline after Nirithy== signature in both encoders (CRLF-preserving)."""
import io

def load(p):
    return io.open(p, 'r', encoding='utf-8-sig', newline='').read()

def save(p, t):
    io.open(p, 'w', encoding='utf-8', newline='').write(t)

def patch(path, old, new):
    t = load(path)
    n = t.replace('\r\n', '\n')
    assert n.count(old) == 1, f'{path}: anchor count={n.count(old)}'
    n = n.replace(old, new, 1)
    if '\r\n' in t:
        n = n.replace('\n', '\r\n')
    save(path, n)
    print('[OK]', path)

NL = '\n'
patch(
    r'E:\Soft\Proje\LXCLUA-NCore\lua\src\utils\encrypt_bytecode.c',
    'fwrite("Nirithy==", 1, 9, f);' + NL + '    nirithy_write_pretty(f, b64, strlen(b64));',
    'fwrite("Nirithy==", 1, 9, f);' + NL + '    fputc(\'\\n\', f);  /* 签名后换行，排版更规整 */' + NL + '    nirithy_write_pretty(f, b64, strlen(b64));'
)
patch(
    r'E:\Soft\Proje\LXCLUA-NCore\lua\src\stdlib\lstrlib.c',
    'luaL_addstring(&b, "Nirithy==");' + NL + '  nirithy_write_pretty(&b, encoded, strlen(encoded));',
    'luaL_addstring(&b, "Nirithy==");' + NL + '  luaL_addchar(&b, \'\\n\');  /* 签名后换行，排版更规整 */' + NL + '  nirithy_write_pretty(&b, encoded, strlen(encoded));'
)
print('done')
