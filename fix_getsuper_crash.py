import sys

def process_file(file_path):
    with open(file_path, "r") as f:
        content = f.read()

    # wait, the backtrace says the crash is in ljit_cg_emit_getsuper, but why is GETSUPER even executed for test_jit_closure.lua?
    # Because ljit_translate.c doesn't set operands for OP_GETSUPER correctly? Or maybe node->next is messed up.
    # Ah, OP_GETSUPER in ljit_translate.c just appends without setting dest/src, leaving them uninitialized (IR_VAL_NONE).
    # But wait, test_jit_closure.lua does NOT have any OOP code! So OP_GETSUPER should never be translated.
    # So why does the IR have IR_GETSUPER?
    # OP_GETSUPER is 85. What opcode is 85 in lopcodes.h?

    with open(file_path, "w") as f:
        f.write(content)

process_file("src/vm/jit/codegen/ljit_codegen.c")
