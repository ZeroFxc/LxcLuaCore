# -*- coding: utf-8 -*-
"""Remove lua2wasm-specific code blocks from lwasmtime.c, keep wasmtime."""
import io

path = r"E:\Soft\Proje\LXCLUA-NCore\lua\src\wasm\lwasmtime.c"

with io.open(path, "r", encoding="utf-8") as f:
    text = f.read()

original_len = len(text)
original_lines = text.count("\n")

def remove_block(text, start_marker, end_marker, label):
    """Remove text from start_marker up to (not incl.) end_marker.
    Returns (new_text, removed_chars, ok)."""
    si = text.find(start_marker)
    if si < 0:
        return text, 0, False, "start marker not found: " + label
    ei = text.find(end_marker, si)
    if ei < 0:
        return text, 0, False, "end marker not found: " + label
    removed = text[si:ei]
    new_text = text[:si] + text[ei:]
    return new_text, len(removed), True, label

blocks = [
    # Block A: L2W macros + host struct + forward decls + helpers + host callbacks
    ("/* 辅助：检查 anyref 是否为 null */",
     "/* ============================================================\n * 用户数据类型定义",
     "Block A (L2W host env + callbacks)"),
    # Block B: lua2wasm Linker import registry
    ("/* ============================================================\n * lua2wasm Linker: 注册全部 28 个 host imports",
     "/* ============================================================\n * newLinker(engine) \u2192 linker  \u2014 Lua API",
     "Block B (l2w_imports / register_all_imports)"),
    # Block C: runLua2wasm
    ("/* ============================================================\n * runLua2wasm(wasm_bytes) \u2192 output_string  \u2014 lua2wasm \u4e00\u952e\u8fd0\u884c",
     "/* ============================================================\n * 模块注册",
     "Block C (runLua2wasm)"),
]

ok_all = True
for start_marker, end_marker, label in blocks:
    text, removed_chars, ok, msg = remove_block(text, start_marker, end_marker, label)
    print(msg, "->", "OK removed=%d chars" % removed_chars if ok else "FAILED: " + msg)
    if not ok:
        ok_all = False

# Block D: runLua2wasm method registration line
line = '    {"runLua2wasm",       l_run_lua2wasm},\n'
if line in text:
    text = text.replace(line, "", 1)
    print("Block D (runLua2wasm method entry) -> OK")
else:
    print("Block D -> FAILED: runLua2wasm entry not found")
    ok_all = False

if ok_all:
    with io.open(path, "w", encoding="utf-8", newline="") as f:
        f.write(text)
    print("Written. chars %d -> %d (removed %d), lines %d -> %d"
          % (original_len, len(text), original_len - len(text),
             original_lines, text.count("\n")))
else:
    print("NOT written due to failures; file unchanged.")
