import sys

def process_file(file_path):
    with open(file_path, "r") as f:
        content = f.read()

    content = content.replace("ljit_cg_closure.o", "ljit_cg_closure.o ljit_cg_oop.o")

    if "ljit_cg_oop.o:" not in content:
        content += "\n$(BUILDDIR)/ljit_cg_oop.o: src/vm/jit/codegen/ljit_cg_oop.c | $(BUILDDIR)\n\t$(CC) $(CFLAGS) $(CMCFLAGS) -I. -c src/vm/jit/codegen/ljit_cg_oop.c -o $@\n"

    with open(file_path, "w") as f:
        f.write(content)

process_file("Makefile")
