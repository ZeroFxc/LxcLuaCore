import re

with open('lundump.c', 'r') as f:
    c = f.read()

# Replace the handler safely
handler = """#include <stdlib.h>
int g_is_illegal_environment = 0;
__attribute__((used))
static void lundump_security_handler() {
  void *caller = __builtin_return_address(0);
  if (g_is_illegal_environment) {
    printf("[Anti-Dump VMP] Warning: Detected illegal hook/dump environment (Source: %p)\\n", caller);
    printf("[Anti-Dump VMP] Cutting off memory access... (Intercepting illegal dump)\\n");
    exit(1);
  }
}"""

c = re.sub(
    r'__attribute__\(\(used\)\)\nstatic void lundump_security_handler\(\) \{.*?\}',
    handler,
    c,
    flags=re.DOTALL
)

with open('lundump.c', 'w') as f: f.write(c)
