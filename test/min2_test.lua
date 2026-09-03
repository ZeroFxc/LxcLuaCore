-- min2_test.lua - 直接用 hex string 构造 WASM
local wasmtime = require("wasmtime")

-- 手工构造的最小 WASM 二进制:
-- type=(i32,i32)->i32, func 0 uses type 0, memory min=1, export add+memory, code body
local function hex(str)
    return str:gsub("..", function(h) return string.char(tonumber(h, 16)) end)
end

local bin = hex(
    "0061736d"..    -- magic
    "01000000"..    -- version
    "01"..          -- type section id
    "07"..          -- size=7
    "01"..          -- 1 type
    "60"..          -- functype
    "02".."7f7f"..  -- 2 params i32 i32
    "01".."7f"..    -- 1 result i32
    "03"..          -- func section id
    "02"..          -- size=2
    "01".."00"..    -- 1 func, type index 0
    "05"..          -- memory section id
    "03"..          -- size=3
    "01"..          -- 1 memory
    "00".."01"..    -- limits: flags=0, initial=1
    "07"..          -- export section id
    "10"..          -- size=16 (0x10)
    "02"..          -- 2 exports
      "03".."616464".."00".."00"..  -- "add", func, idx=0
      "06".."6d656d6f7279".."02".."00"..  -- "memory", mem, idx=0
    "0a"..          -- code section id
    "08"..          -- size=8
    "01"..          -- 1 function
    "00"..          -- 0 locals
    "20".."00"..    -- local.get 0
    "20".."01"..    -- local.get 1
    "6a"..          -- i32.add
    "0b"            -- end
)

print("bin size:", #bin)
assert(#bin == 54, "expected 54, got " .. #bin)

-- 十六进制展示
for i = 1, #bin do
    io.write(string.format("%02X ", string.byte(bin, i)))
end
print()

-- 测试
local ok, err = wasmtime.validate(bin)
print("validate:", ok, err)

local engine = wasmtime.newEngine()
local module = wasmtime.newModule(engine, bin)
print("[OK] module created")

local store = wasmtime.newStore(engine)
local instance = wasmtime.newInstance(store, module, {})
print("[OK] instance created")

local f = instance:getExport("add")
local r = f:call(10, 20)
print("add(10,20) =", r)
assert(r == 30, "expected 30, got " .. tostring(r))

local mem = instance:getMemory("memory")
print("memory:size:", mem:size())
print("memory:dataSize:", mem:dataSize())
assert(mem:size() >= 1, "memory size too small")

print("\n======== ALL PASSED ========")