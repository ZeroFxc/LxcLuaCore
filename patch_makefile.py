import sys

def process_file(file_path):
    with open(file_path, "r") as f:
        content = f.read()

    content = content.replace("ljit_cg_call.o ljit_cg_conv.o", "ljit_cg_call.o ljit_cg_conv.o ljit_cg_closure.o")

    if "ljit_cg_closure.o:" not in content:
        lines = content.split('\n')
        new_lines = []
        for line in lines:
            new_lines.append(line)
            if "$(BUILDDIR)/ljit_cg_conv.o:" in line:
                pass # just need to find a place

        content += "\n$(BUILDDIR)/ljit_cg_closure.o: src/vm/jit/codegen/ljit_cg_closure.c | $(BUILDDIR)\n\t$(CC) $(CFLAGS) $(CMCFLAGS) -I. -c src/vm/jit/codegen/ljit_cg_closure.c -o $@\n"

    with open(file_path, "w") as f:
        f.write(content)

process_file("Makefile")
