import sys

def process_file(file_path):
    with open(file_path, "r") as f:
        content = f.read()

    # Change to just call checkGC
    content = content.replace("luaC_checkGC(L);", "luaC_step(L);")

    with open(file_path, "w") as f:
        f.write(content)

process_file("src/vm/jit/codegen/ljit_codegen.c")
