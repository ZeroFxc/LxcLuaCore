import sys

def process_file(file_path):
    with open(file_path, "r") as f:
        content = f.read()

    # The previous script replaced "ljit_cg_closure.o" with "ljit_cg_closure.o ljit_cg_oop.o" everywhere, causing the rule to become:
    # $(BUILDDIR)/ljit_cg_closure.o ljit_cg_oop.o: src/vm/jit/codegen/ljit_cg_closure.c | $(BUILDDIR)

    content = content.replace("$(BUILDDIR)/ljit_cg_closure.o ljit_cg_oop.o: src/vm/jit/codegen/ljit_cg_closure.c | $(BUILDDIR)\n\t$(CC) $(CFLAGS) $(CMCFLAGS) -I. -c src/vm/jit/codegen/ljit_cg_closure.c -o $@\n", "$(BUILDDIR)/ljit_cg_closure.o: src/vm/jit/codegen/ljit_cg_closure.c | $(BUILDDIR)\n\t$(CC) $(CFLAGS) $(CMCFLAGS) -I. -c src/vm/jit/codegen/ljit_cg_closure.c -o $@\n\n$(BUILDDIR)/ljit_cg_oop.o: src/vm/jit/codegen/ljit_cg_oop.c | $(BUILDDIR)\n\t$(CC) $(CFLAGS) $(CMCFLAGS) -I. -c src/vm/jit/codegen/ljit_cg_oop.c -o $@\n")

    with open(file_path, "w") as f:
        f.write(content)

process_file("Makefile")
