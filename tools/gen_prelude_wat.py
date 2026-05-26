#!/usr/bin/env python3
"""将 prelude.wat 转换为 C 字符串头文件，替代 C23 #embed 指令"""

import sys
import os

SRC_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WAT_FILE = os.path.join(SRC_DIR, "src", "lua2wasm", "prelude.wat")
OUT_FILE = os.path.join(SRC_DIR, "src", "lua2wasm", "prelude_wat.h")

with open(WAT_FILE, "rb") as f:
    data = f.read()

# 生成 C 字符串字面量
lines = []
lines.append("// Auto-generated from prelude.wat by tools/gen_prelude_wat.py. Do not edit.")
lines.append("#ifndef LUA2WASM_PRELUDE_WAT_H")
lines.append("#define LUA2WASM_PRELUDE_WAT_H")
lines.append("static const char PRELUDE[] =")

# 将内容按 1024 字节分段，生成可读的 C 字符串
CHUNK = 1024
for i in range(0, len(data), CHUNK):
    chunk = data[i:i + CHUNK]
    escaped = []
    for b in chunk:
        if b == 0x5C:  # backslash
            escaped.append("\\\\")
        elif b == 0x22:  # double quote
            escaped.append("\\\"")
        elif b == 0x0A:  # LF
            escaped.append("\\n")
        elif b == 0x0D:  # CR
            escaped.append("\\r")
        elif b == 0x09:  # TAB
            escaped.append("\\t")
        elif b == 0x00:  # NUL
            escaped.append("\\0")
        elif 0x20 <= b <= 0x7E:  # printable ASCII
            escaped.append(chr(b))
        else:
            # 使用八进制转义（固定3位），避免 hex 转义贪婪匹配导致溢出
            escaped.append(f"\\{b:03o}")
    line = '"' + "".join(escaped) + '"'
    lines.append(line)

lines.append(";")
lines.append("#endif")

with open(OUT_FILE, "w", encoding="utf-8") as f:
    f.write("\n".join(lines) + "\n")

print(f"Generated {OUT_FILE} ({len(data)} bytes from {WAT_FILE})")