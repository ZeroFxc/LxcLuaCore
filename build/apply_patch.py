# -*- coding: utf-8 -*-
"""CRLF-preserving patch helper for Nirithy pretty-shell feature."""
import sys, io, os

def load(path):
    with io.open(path, 'r', encoding='utf-8-sig', newline='') as f:
        return f.read()

def save(path, text):
    # detect dominant newline
    nl = '\r\n' if text.count('\r\n') >= text.count('\n') - text.count('\r\n') else '\n'
    with io.open(path, 'w', encoding='utf-8', newline='') as f:
        f.write(text)
    return nl

def replace(path, old, new, count=1):
    text = load(path)
    # normalize to \n for matching, then re-emit with original dominant newline
    norm = text.replace('\r\n', '\n')
    if norm.count(old) < 1:
        print(f'[FAIL] anchor not found in {path}')
        sys.exit(1)
    norm = norm.replace(old, new, count)
    # restore dominant newline
    if text.count('\r\n') > 0:
        norm = norm.replace('\n', '\r\n')
    with io.open(path, 'w', encoding='utf-8', newline='') as f:
        f.write(norm)
    print(f'[OK] patched {path}')

if __name__ == '__main__':
    mode = sys.argv[1]
    path = sys.argv[2]
    if mode == 'file':
        # replace whole content of path with content of the patch file given in argv[3]
        patchfile = sys.argv[3]
        with io.open(patchfile, 'r', encoding='utf-8', newline='') as f:
            new = f.read()
        replace(path, load(path).replace('\r\n','\n'), new.replace('\r\n','\n'))
    else:
        print('usage: apply_patch.py file <target> <newcontent>')
