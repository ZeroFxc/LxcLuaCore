-- bare_test.lua - 绝对最简 WASM: 仅 type + func + code + export
local wasmtime = require("wasmtime")
local function hex(str)
    return str:gsub("..", function(h) return string.char(tonumber(h, 16)) end)
end

-- 最简: (i32, i32) -> i32, 1 func, export "add", code = local.get 0 + local.get 1 + i32.add + end
local bin = hex(
    "0061736d01000000"..  -- magic + version
    "01070160027f7f017f".. -- type: count=1, (i32,i32)->i32
    "03020100"..           -- func: count=1, type_idx=0
    "070701036164640000".. -- export: count=1, "add", func 0
    "0a080100200020016a0b" -- code: count=1, body=8, local.get0, local.get1, i32.add, end
)

print("size:", #bin)
print("bytes:", bin:gsub(".", function(c) return string.format("%02X", c:byte()) end))

local engine = wasmtime.newEngine()
local module = wasmtime.newModule(engine, bin)
print("[OK] module created")

local store = wasmtime.newStore(engine)
local inst = wasmtime.newInstance(store, module)
print("[OK] instance")

local add = inst:getExport("add")
print("add(1,2):", add:call(1, 2))
print("add(100,200):", add:call(100, 200))
print("[ALL OK]")