import sys

def process_file(file_path):
    with open(file_path, "r") as f:
        content = f.read()

    content = content.replace("base = L->ci->func.p + 1;", "/* base handled below */")
    content = content.replace("s2v(rb)", "s2v(rb)") # rb is StkId
    content = content.replace("s2v(ra)", "s2v(ra)") # ra is StkId

    with open(file_path, "w") as f:
        f.write(content)

process_file("src/vm/jit/codegen/ljit_codegen.c")
