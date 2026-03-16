import re

with open('lundump.c', 'r') as f:
    content = f.read()

header = """
#include "lvmprotect.h"

DECLARE_VMP_MARKER(lundump_vmp);

__attribute__((used))
static void lundump_security_handler() {
  /* Prevent dumping */
}

__attribute__((constructor, used))
static void patch_lundump() {
  vmp_patch_memory(lundump_vmp_start, lundump_vmp_end, lundump_security_handler);
}
"""

content = re.sub(r'(#include "lobfuscate\.h"\n)', r'\1' + header, content)

marker_code = """
  VMP_MARKER(lundump_vmp);
"""

# Find LClosure *luaU_undump
# Inject VMP_MARKER
content = re.sub(
    r'(LClosure \*luaU_undump\(lua_State \*L, ZIO \*Z, const char \*name, int force_standard\) {\n  LoadState S;\n)',
    r'\1' + marker_code,
    content
)

with open('lundump.c', 'w') as f:
    f.write(content)
