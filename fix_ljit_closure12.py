import sys

def process_file(file_path):
    with open(file_path, "r") as f:
        content = f.read()

    # The function luaC_checkGC is internal to gc?
    # Actually luaC_checkGC is a macro, and checkGC(L, ra + 1) is what lvm.c does.
    # In ljit_codegen.c we have it defined? We previously used it in newtable.
    # Wait, in lvm.c: `checkGC(L, ra + 1);`
    # Check what we have for closure...

    with open(file_path, "w") as f:
        f.write(content)

process_file("src/vm/jit/codegen/ljit_codegen.c")
