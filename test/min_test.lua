-- min_test.lua - 最简测试
local wasmtime = require("wasmtime")
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
-- 最简: type=(i32,i32)->i32, func, memory, export add+memory, code
local typesec = vec(1, lebu32(1) .. string.char(0x60, 2, 0x7f, 0x7f, 1, 0x7f))
local funcsec = vec(3, lebu32(1) .. lebu32(0))
local memsec  = vec(5, lebu32(1) .. string.char(0x00, 2)) -- min=2
-- 导出 "add"(func0), "memory"(mem0)
local function export_entry(name, kind, idx)
    return lebu32(#name) .. name .. string.char(kind) .. lebu32(idx)
end
local expbody = lebu32(2)
    .. export_entry("add", 0x00, 0)
    .. export_entry("memory", 0x02, 0)
local expsec = vec(7, expbody)
local codebody = lebu32(1) .. lebu32(0) .. string.char(0x20, 0x00, 0x20, 0x01, 0x6a, 0x0b)
local codesec = vec(10, codebody)
local bin = "\0asm\1\0\0\0" .. typesec .. funcsec .. memsec .. expsec .. codesec

print("bin size:", #bin)
print("validate:", wasmtime.validate(bin))

local engine = wasmtime.newEngine()
local module = wasmtime.newModule(engine, bin)
print("[OK] module created")

local store = wasmtime.newStore(engine)
local instance = wasmtime.newInstance(store, module, {})
print("[OK] instance created")

local add_func = instance:getExport("add")
local r = add_func:call(10, 20)
print("add(10,20) =", r)
assert(r == 30)

local memory = instance:getMemory("memory")
print("memory:size:", memory:size())
print("memory:dataSize:", memory:dataSize())

print("[ALL OK]")