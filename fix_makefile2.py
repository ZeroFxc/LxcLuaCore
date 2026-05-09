import sys

def process_file(file_path):
    with open(file_path, "r") as f:
        content = f.read()

    # CORE_O doesn't seem to include ljit_cg_oop.o because maybe patch_makefile2 didn't find the right place or we used a different variable.
    # Let's replace the whole CORE_O again

    if "ljit_cg_closure.o ljit_cg_oop.o" not in content:
        content = content.replace("ljit_cg_closure.o", "ljit_cg_closure.o ljit_cg_oop.o")

    with open(file_path, "w") as f:
        f.write(content)

process_file("Makefile")
