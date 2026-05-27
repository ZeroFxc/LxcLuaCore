-- no_mem_test.lua - 最简 WASM 测试（无 memory）
local wasmtime = require("wasmtime")
local function hex(str)
    return str:gsub("..", function(h) return string.char(tonumber(h, 16)) end)
end

-- 无 memory: type=(i32,i32)->i32, func 0, export "add" func 0, code
local bin = hex(
    "0061736d01000000"..
    "01".."07".."01".."60".."02".."7f7f".."01".."7f"..  -- type section
    "03".."02".."01".."00"..                               -- func section
    "07".."07".."01".."03".."616464".."00".."00"..        -- export: count=1, "add" func 0
    "0a".."08".."01".."00".."2000".."2001".."6a".."0b"    -- code section
)

print("size:", #bin)
print("validate:", wasmtime.validate(bin))
local engine = wasmtime.newEngine()
local module = wasmtime.newModule(engine, bin)
print("[OK] module")
local store = wasmtime.newStore(engine)
local inst = wasmtime.newInstance(store, module, {})
local f = inst:getExport("add")
print("add(5,7) =", f:call(5, 7))