-- bare3_test.lua - 添加 data count section (id=12)
local wasmtime = require("wasmtime")

local bin = string.char(0x00,0x61,0x73,0x6d,0x01,0x00,0x00,0x00) -- magic+version
    .. string.char(0x01,0x07,0x01,0x60,0x02,0x7f,0x7f,0x01,0x7f) -- type
    .. string.char(0x03,0x02,0x01,0x00) -- func
    .. string.char(0x0c,0x01,0x00) -- data count section: id=12, size=1, count=0
    .. string.char(0x07,0x07,0x01,0x03) .. "add" .. string.char(0x00,0x00) -- export
    .. string.char(0x0a,0x08,0x01,0x00,0x20,0x00,0x20,0x01,0x6a,0x0b) -- code

print("size:", #bin)
local engine = wasmtime.newEngine()
local module = wasmtime.newModule(engine, bin)
print("[OK] module")
local store = wasmtime.newStore(engine)
local inst = wasmtime.newInstance(store, module)
print("[OK] instance")
local f = inst:getExport("add")
print("add(100,200):", f:call(100, 200))