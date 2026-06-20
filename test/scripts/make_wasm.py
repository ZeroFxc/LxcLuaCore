# make_wasm.py - 用 Python 生成正确的 WASM 二进制
import struct

def leb128_u(n):
    parts = []
    while True:
        b = n & 0x7f
        n >>= 7
        if n != 0:
            b |= 0x80
        parts.append(b)
        if n == 0:
            break
    return bytes(parts)

# WASM binary construction
wasm = b'\x00asm\x01\x00\x00\x00'  # magic + version

# Type section
type_body = b''
type_body += leb128_u(1)  # 1 type
type_body += b'\x60'      # functype
type_body += leb128_u(2)  # 2 params
type_body += b'\x7f\x7f'  # i32, i32
type_body += leb128_u(1)  # 1 result
type_body += b'\x7f'      # i32
wasm += bytes([1]) + leb128_u(len(type_body)) + type_body

# Function section
func_body = b''
func_body += leb128_u(1)  # 1 func
func_body += leb128_u(0)  # type index 0
wasm += bytes([3]) + leb128_u(len(func_body)) + func_body

# Memory section
mem_body = b''
mem_body += leb128_u(1)  # 1 memory
mem_body += b'\x00'      # flags: no max
mem_body += leb128_u(1)  # initial = 1
wasm += bytes([5]) + leb128_u(len(mem_body)) + mem_body

# Export section
def export_entry(name, kind, idx):
    return leb128_u(len(name)) + name.encode() + bytes([kind]) + leb128_u(idx)

exp_body = b''
exp_body += leb128_u(2)  # 2 exports
exp_body += export_entry("add", 0x00, 0)      # func 0
exp_body += export_entry("memory", 0x02, 0)   # memory 0
wasm += bytes([7]) + leb128_u(len(exp_body)) + exp_body

# Code section
code_body = b''
code_body += leb128_u(1)      # 1 function body
code_body += leb128_u(0)      # 0 locals
code_body += b'\x20\x00'      # local.get 0
code_body += b'\x20\x01'      # local.get 1
code_body += b'\x6a'          # i32.add
code_body += b'\x0b'          # end
wasm += bytes([10]) + leb128_u(len(code_body)) + code_body

with open("test/py_wasm.wasm", "wb") as f:
    f.write(wasm)

print(f"WASM size: {len(wasm)}")
print(f"Hex: {wasm.hex()}")
print("Written to test/py_wasm.wasm")