import re

for fname in ['lpromise.c', 'laio.c']:
    with open(fname, 'r', encoding='utf-8') as f:
        c = f.read()
    
    # Fix double-paren
    c = c.replace(')); fflush(stderr);', '); fflush(stderr);')
    
    # Remove all [DBG] lines
    lines = c.split('\n')
    clean = [l for l in lines if '[DBG]' not in l]
    c = '\n'.join(clean)
    
    with open(fname, 'w', encoding='utf-8') as f:
        f.write(c)
    print(f'Cleaned {fname}')
