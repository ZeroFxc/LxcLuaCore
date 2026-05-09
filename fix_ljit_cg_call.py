import sys

def process_file(file_path):
    with open(file_path, "r") as f:
        content = f.read()

    # Add missing header for luaD_call
    content = content.replace("#include \"ljit_codegen.h\"\n", "#include \"ljit_codegen.h\"\n#include \"../../../core/ldo.h\"\n")

    with open(file_path, "w") as f:
        f.write(content)

process_file("src/vm/jit/codegen/ljit_cg_call.c")
