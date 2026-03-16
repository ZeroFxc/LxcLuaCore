import re

with open('lundump.c', 'r') as f:
    content = f.read()

content = re.sub(
    r'(#include "lvmprotect\.h"\n)',
    r'\1#include <stdio.h>\n',
    content
)

with open('lundump.c', 'w') as f:
    f.write(content)
