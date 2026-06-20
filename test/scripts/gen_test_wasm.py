#!/usr/bin/env python3
"""生成测试用的 WASM 二进制，包含 memory/global/table/func 四种导出"""

import struct

def uleb128(v):
    """编码无符号 LEB128"""
    out = bytearray()
    while v > 0x7F:
        out.append((v & 0x7F) | 0x80)
        v >>= 7
    out.append(v)
    return out

def mk_section(section_id, body):
    """构造 section: id + size(LEB128) + body"""
    return bytes([section_id]) + uleb128(len(body)) + body

def make_wasm():
    wasm = bytearray()
    # === WASM 魔数 + 版本 ===
    wasm += b'\x00asm'
    wasm += b'\x01\x00\x00\x00'

    # === Type section (1): 一个函数类型 (i32,i32)->(i32) ===
    types = bytearray()
    types.append(1)  # count = 1
    types.append(0x60)  # functype
    types.append(2)     # 2 params
    types.append(0x7F)  # i32
    types.append(0x7F)  # i32
    types.append(1)     # 1 result
    types.append(0x7F)  # i32
    wasm += mk_section(1, types)

    # === Function section (3): 一个函数引用 type[0] ===
    funcs = bytearray()
    funcs.append(1)  # count = 1
    funcs.append(0)  # type index 0
    wasm += mk_section(3, funcs)

    # === Table section (4): 一个 anyfunc table, min=10 ===
    tables = bytearray()
    tables.append(1)    # count = 1
    tables.append(0x70) # elemtype = funcref
    tables.append(0x00) # limits flag = 0 (no max)
    tables.append(10)   # min = 10
    wasm += mk_section(4, tables)

    # === Memory section (5): 一个 memory, min=1 page ===
    mems = bytearray()
    mems.append(1)    # count = 1
    mems.append(0x00) # limits flag = 0 (no max)
    mems.append(1)    # min = 1 page (64KB)
    wasm += mk_section(5, mems)

    # === Global section (6): 一个 i32 mutable global, init=42 ===
    globals = bytearray()
    globals.append(1)    # count = 1
    globals.append(0x7F) # type = i32
    globals.append(0x01) # mutable = true
    # init expression: i32.const 42, end
    globals.append(0x41) # i32.const
    globals.append(42)   # value (signed LEB = 42)
    globals.append(0x0B) # end
    wasm += mk_section(6, globals)

    # === Export section (7): 四个导出 ===
    exps = bytearray()
    exps.append(4)  # count = 4

    # Export "add" (func, index 0)
    name = b"add"
    exps.append(len(name))
    exps += name
    exps.append(0x00)  # kind = func
    exps.append(0x00)  # index = 0

    # Export "mem" (memory, index 0)
    name = b"mem"
    exps.append(len(name))
    exps += name
    exps.append(0x02)  # kind = memory
    exps.append(0x00)  # index = 0

    # Export "my_global" (global, index 0)
    name = b"my_global"
    exps.append(len(name))
    exps += name
    exps.append(0x03)  # kind = global
    exps.append(0x00)  # index = 0

    # Export "my_table" (table, index 0)
    name = b"my_table"
    exps.append(len(name))
    exps += name
    exps.append(0x01)  # kind = table
    exps.append(0x00)  # index = 0

    wasm += mk_section(7, exps)

    # === Code section (10): 函数体 "add" ===
    # local.get 0; local.get 1; i32.add; end
    code_body = bytearray()
    code_body.append(0x00)  # locals count = 0
    code_body.append(0x20)  # local.get
    code_body.append(0x00)  # index 0
    code_body.append(0x20)  # local.get
    code_body.append(0x01)  # index 1
    code_body.append(0x6A)  # i32.add
    code_body.append(0x0B)  # end

    codes = bytearray()
    codes.append(1)  # count = 1
    codes += uleb128(len(code_body))
    codes += code_body
    wasm += mk_section(10, codes)

    return bytes(wasm)

if __name__ == "__main__":
    data = make_wasm()
    with open("test/test_comprehensive.wasm", "wb") as f:
        f.write(data)
    print(f"Generated test/test_comprehensive.wasm ({len(data)} bytes)")
    print(f"Hex: {data.hex()}")