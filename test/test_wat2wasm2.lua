-- test_wat2wasm2.lua
local wasmtime = require("wasmtime")
local f = io.open("test/simple.wasm", "rb")
local bin = f:read("*all")
f:close()
print("size:", #bin)

-- 带 exceptions 的引擎
local engine = wasmtime.newEngine({gc=true, exceptions=true, funcRef=true, refTypes=true})
local store = wasmtime.newStore(engine)
local module = wasmtime.newModule(engine, bin)
print("[OK] module")
local inst = wasmtime.newInstance(store, module)
print("[OK] instance")

local add = inst:getExport("add")
local r = add:call(1, 2)
print("add(1,2) =", r)
assert(r == 3, "expected 3")

print("function type test:")
local params, results = add:getType()
print("  params:", table.concat(params or {}, ","))
print("  results:", table.concat(results or {}, ","))

print("instance exports:")
local exp = inst:getExports()
for i = 1, #exp do
    print("  ["..i.."] "..exp[i])
end

print("[ALL OK]")