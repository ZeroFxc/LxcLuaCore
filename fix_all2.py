import re
with open('lundump.c', 'r') as f:
    c = f.read()

handler = """#include "lvmprotect.h"
#include <stdio.h>
#include <stdlib.h>

int g_is_illegal_environment = 0;
DECLARE_VMP_MARKER(lundump_vmp);

__attribute__((used))
static void lundump_security_handler() {
  void *caller = __builtin_return_address(0);
  if (g_is_illegal_environment) {
    printf("[Anti-Dump VMP] Warning: Detected illegal hook/dump environment (Source: %p)\\n", caller);
    printf("[Anti-Dump VMP] Cutting off memory access... (Intercepting illegal dump)\\n");
    exit(1);
  }
}

__attribute__((constructor, used))
static void patch_lundump() {
  vmp_patch_memory(lundump_vmp_start, lundump_vmp_end, lundump_security_handler);
}
"""

c = re.sub(r'#include "lvmprotect\.h"\n.*?\nstatic void patch_lundump\(\) \{\n.*?\n\}\n', handler, c, flags=re.DOTALL)

with open('lundump.c', 'w') as f: f.write(c)
