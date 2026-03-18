local patch = require("patch")

local arch = patch.get_arch()
print("Architecture:", arch)

if arch == "x86_64" then
    -- x86_64 shellcode for:
    -- mov rax, rdi
    -- add rax, rsi
    -- ret
    local shellcode = "\x48\x89\xf8\x48\x01\xf0\xc3"

    local res = patch.exec(shellcode, 10, 20)
    assert(res == 30, string.format("patch.exec failed: expected 30, got %s", tostring(res)))
    print("patch.exec: SUCCESS (10 + 20 = 30)")
else
    print("Skipping patch.exec test on non-x86_64 architecture")
end

print("Test passed successfully.")
