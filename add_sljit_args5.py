import sys

def process_file(file_path):
    with open(file_path, "r") as f:
        content = f.read()

    if "SLJIT_ARGS5" in content:
        return

    lines = content.split('\n')
    new_lines = []

    for i, line in enumerate(lines):
        new_lines.append(line)
        if "#define SLJIT_ARGS4V" in line:
            new_lines.append("")
            new_lines.append("#define SLJIT_ARGS5(ret, arg1, arg2, arg3, arg4, arg5) \\")
            new_lines.append("\t(SLJIT_ARGS4(ret, arg1, arg2, arg3, arg4) | SLJIT_ARG_VALUE(SLJIT_ARG_TO_TYPE(arg5), 5))")
            new_lines.append("#define SLJIT_ARGS5V(arg1, arg2, arg3, arg4, arg5) \\")
            new_lines.append("\t(SLJIT_ARGS4V(arg1, arg2, arg3, arg4) | SLJIT_ARG_VALUE(SLJIT_ARG_TO_TYPE(arg5), 5))")

    with open(file_path, "w") as f:
        f.write('\n'.join(new_lines))

process_file("src/jit/sljitLir.h")
