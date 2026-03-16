import re

with open('lvm.c', 'r') as f:
    content = f.read()

header = """
#include "lvmprotect.h"

DECLARE_VMP_MARKER(lvm_vmp);

__attribute__((used))
static void lvm_security_handler() {
  /* Dummy handler representing the dynamic dispatcher */
}

__attribute__((constructor, used))
static void patch_lvm() {
  vmp_patch_memory(lvm_vmp_start, lvm_vmp_end, lvm_security_handler);
}
"""

content = re.sub(r'(#include "lauxlib\.h"\n)', r'\1' + header, content)

marker_code = """    VMP_MARKER(lvm_vmp);\n"""

content = re.sub(
    r'(    Instruction i;  /\* instruction being executed \*/\n    vmfetch\(\);\n)',
    r'\1' + marker_code,
    content
)

with open('lvm.c', 'w') as f:
    f.write(content)
