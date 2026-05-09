import sys

def process_file(file_path):
    with open(file_path, "r") as f:
        content = f.read()

    # The IR_GETSUPER and IR_INHERIT are grouped up with IR_EXTRAARG etc without a body.
    # We should remove them from there.

    content = content.replace("            case IR_GETSUPER:\n", "")
    content = content.replace("            case IR_INHERIT:\n", "")

    with open(file_path, "w") as f:
        f.write(content)

process_file("src/vm/jit/codegen/ljit_codegen.c")
