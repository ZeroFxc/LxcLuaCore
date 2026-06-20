local function lebu32(n)
    local b = {}
    repeat
        b[#b+1] = n & 0x7f
        n = n >> 7
        if n ~= 0 then b[#b] = b[#b] | 0x80 end
    until n == 0
    return string.char(table.unpack(b))
end
local function vec(sec_id, body)
    return string.char(sec_id) .. lebu32(#body) .. body
end
local function export_entry(name, kind, idx)
    return lebu32(#name) .. name .. string.char(kind) .. lebu32(idx)
end
local typesec = vec(1, lebu32(1) .. string.char(0x60, 2, 0x7f, 0x7f, 1, 0x7f))
local funcsec = vec(3, lebu32(1) .. lebu32(0))
local tabsec  = vec(4, lebu32(1) .. string.char(0x70, 0x00, 4))
local memsec  = vec(5, lebu32(1) .. string.char(0x00, 1))
local globsec = vec(6, lebu32(1) .. string.char(0x7f, 0x01) .. string.char(0x41, 0x00, 0x0b))
local expbody = lebu32(6)
    .. export_entry("add", 0x00, 0)
    .. export_entry("write_mem", 0x00, 0)
    .. export_entry("read_mem", 0x00, 0)
    .. export_entry("memory", 0x02, 0)
    .. export_entry("counter", 0x03, 0)
    .. export_entry("ftable", 0x01, 0)
local expsec = vec(7, expbody)
local codebody = lebu32(1) .. lebu32(0) .. string.char(0x20, 0x00, 0x20, 0x01, 0x6a, 0x0b)
local codesec = vec(10, codebody)
local wasm = "\0asm\1\0\0\0" .. typesec .. funcsec .. tabsec .. memsec .. globsec .. expsec .. codesec
print("Size:", #wasm)
for i = 1, #wasm do io.write(string.format("%02X ", string.byte(wasm, i))) end
print()
local s = {typesec, funcsec, tabsec, memsec, globsec, expsec, codesec}
local n = {"type", "func", "table", "mem", "global", "export", "code"}
for i = 1, 7 do
    print(string.format("  %s body=%d total=%d", n[i], #s[i] - 2, #s[i]))
end
print("expbody size:", #expbody)
print("codebody size:", #codebody)
print("expected total:", 8 + #typesec + #funcsec + #tabsec + #memsec + #globsec + #expsec + #codesec)