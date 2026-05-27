-- minimal externref test
local wasmtime = require("wasmtime")
local engine = wasmtime.newEngine({exceptions=false, gc=false})
local store = wasmtime.newStore(engine)
print("store type:", type(store))
print("store metatable __name:", (getmetatable(store) or {}).__name or "nil")

-- test pcall
local ok, result = pcall(function()
    return wasmtime.newExternref(store, "hello")
end)
if ok then
    print("newExternref OK:", type(result))
else
    print("newExternref FAIL:", result)
end

-- test direct call
local ok2, result2 = pcall(function()
    return wasmtime.newExternref(store, "world")
end)
print("direct:", ok2, result2)