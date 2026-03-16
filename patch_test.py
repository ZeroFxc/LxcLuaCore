import re

with open('lundump.c', 'r') as f:
    content = f.read()

content = re.sub(
    r'(static void lundump_security_handler\(\) {\n)',
    r'\1  void *caller = __builtin_return_address(0);\n  printf("[Security] lundump VMP 钩子被触发, 来源地址: %p\\n", caller);\n',
    content
)

with open('lundump.c', 'w') as f:
    f.write(content)


with open('lvm.c', 'r') as f:
    content = f.read()

content = re.sub(
    r'(static void lvm_security_handler\(\) {\n)',
    r'\1  void *caller = __builtin_return_address(0);\n  printf("[Security] lvm VMP 钩子被触发, 来源地址: %p\\n", caller);\n',
    content
)

with open('lvm.c', 'w') as f:
    f.write(content)
