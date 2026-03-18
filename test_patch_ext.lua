local patch = require("patch")

-- Test type conversions
local num = 0x12345678
local ptr = patch.to_ptr(num)
local back = patch.to_num(ptr)
assert(back == num, string.format("Pointer conversion failed: expected %x, got %x", num, back))
print("to_num / to_ptr: SUCCESS")

-- Test memory allocation and simple reading/writing
local mem = patch.alloc(4096)
assert(mem ~= nil, "Memory allocation failed")
print("Allocated memory at:", mem)

-- Write typed values
patch.write_u8(mem, 0xAB)
patch.write_u32(mem, 0x12345678) -- overwrite
patch.write_u64(mem, 0x1122334455667788) -- overwrite

-- Read typed values
local val = patch.read_u64(mem)
assert(val == 0x1122334455667788, string.format("read_u64 failed: expected 0x1122334455667788, got 0x%x", val))
print("read_u64 / write_u64: SUCCESS")

-- Test memcpy
local mem2 = patch.alloc(4096)
assert(mem2 ~= nil, "Second memory allocation failed")
patch.memcpy(mem2, mem, 8)
local val2 = patch.read_u64(mem2)
assert(val2 == 0x1122334455667788, "memcpy failed")
print("memcpy: SUCCESS")

-- Test call_ret with a direct system function if possible, e.g. time()
-- or since we cannot easily find an address without dlopen/dlsym natively here, let's just use patch_call_ret on a tiny machine code snippet.
local arch = patch.get_arch()
if arch == "x86_64" then
    -- x86_64 shellcode for:
    -- mov rax, rdi
    -- add rax, rsi
    -- add rax, rdx
    -- add rax, rcx
    -- add rax, r8
    -- add rax, r9
    -- ret
    local shellcode = "\x48\x89\xf8\x48\x01\xf0\x48\x01\xd0\x48\x01\xc8\x4c\x01\xc0\x4c\x01\xc8\xc3"
    local sc_mem = patch.alloc(4096)
    patch.write(sc_mem, shellcode)
    patch.mprotect(sc_mem, 4096, "r-x") -- make executable

    local res = patch.call_ret(sc_mem, 1, 2, 3, 4, 5, 6)
    assert(res == 21, string.format("call_ret with args failed: expected 21, got %s", tostring(res)))
    print("call_ret with arguments: SUCCESS (via shellcode)")
    patch.free(sc_mem, 4096)
else
    print("Skipping call_ret test on non-x86_64 architecture")
end

patch.free(mem, 4096)
patch.free(mem2, 4096)
print("All extensions verified successfully.")
