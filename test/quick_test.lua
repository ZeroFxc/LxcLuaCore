-- quick_test.lua -- 快速验证 WASM 二进制是否能加载
local wasmtime = require("wasmtime")
local f = io.open("test/test_comprehensive.wasm", "rb")
local bin = f:read("*all")
f:close()
print("bin size:", #bin)

local engine = wasmtime.newEngine({exceptions=false, gc=false, refTypes=true})
local store = wasmtime.newStore(engine)
local ok, err = wasmtime.validate(engine, bin)
print("validate:", ok, err or "")

local ok, result = pcall(function()
    local module = wasmtime.newModule(engine, bin)
    return module
end)
if not ok then
    print("newModule FAIL:", result)
else
    print("newModule OK")
    local module = result
    local inst = wasmtime.newInstance(store, module)
    print("newInstance OK")
    
    -- 测试 exports
    local exps = inst:getExports()
    print("exports:", table.concat(exps, ", "))
    
    -- 测试 func
    local add = inst:getExport("add")
    local r = add:call(3, 4)
    print("add(3,4) =", r)
    assert(r == 7)
    
    -- 测试 memory
    local mem = inst:getMemory("mem")
    print("memory size:", mem:size(), "pages,", mem:dataSize(), "bytes")
    
    -- 直接检查 getType
    local mt = getmetatable(mem)
    local idx = mt and mt.__index or mt
    if idx then
        print("__index type:", type(idx))
        print("__index keys:")
        for k,v in pairs(idx) do print("  ", k, type(v)) end
    end
    local ok, a, b = pcall(function() return mem:getType() end)
    if ok then
        print("memory type: min="..a.." max="..b)
    else
        print("getType FAIL:", a)
    end
    
    -- 测试 global
    local g = inst:getGlobal("my_global")
    print("global get:", g:get())
    
    -- 测试 table
    local t = inst:getTable("my_table")
    print("table size:", t:size())
    
    print("[ALL QUICK TESTS OK]")
end