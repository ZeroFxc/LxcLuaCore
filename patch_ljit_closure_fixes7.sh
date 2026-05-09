cat << 'INNER_EOF' > fix_ljit_closure10.py
import sys

def process_file(file_path):
    with open(file_path, "r") as f:
        content = f.read()

    # The 5th argument needs to be passed via stack if SLJIT only supports 4 registers,
    # but sljit_emit_icall with SLJIT_ARGS5V does that for us.
    # The actual arguments are (W, W, W, W, W).
    # Oh! `SLJIT_S1` is used instead of `SLJIT_R4` for the 5th argument.
    # In sljit_emit_icall with SLJIT_ARGS5V, arguments go to R0, R1, R2, R3, and the 5th argument is put on stack (handled by emit_icall?).
    # Wait, in SLJIT_ARGS5V(W,W,W,W,W), arguments are passed in R0, R1, R2, R3, R4 (if supported). Let's check how many scratch regs we have. We should use R4.
    content = content.replace("SLJIT_S1, 0, SLJIT_S0, 0", "SLJIT_R4, 0, SLJIT_S0, 0")

    with open(file_path, "w") as f:
        f.write(content)

process_file("src/vm/jit/codegen/ljit_cg_closure.c")
INNER_EOF
python3 fix_ljit_closure10.py
