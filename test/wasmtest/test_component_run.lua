-- 测试 component：加载 wasm-tools 编译的组件并调用
local w = require("wasmtime")

local f = io.open("_comp2.wasm", "rb")
if not f then print("找不到 _comp2.wasm"); return end
local bytes = f:read("*a")
f:close()
print("组件字节数:", #bytes)

local engine = w.newEngine()
local store = w.newStore(engine)
local c = w.newComponent(engine, bytes)
print("newComponent:", type(c))
if type(c) ~= "userdata" then return end

local linker = w.newComponentLinker(engine)
print("newComponentLinker:", type(linker))

local inst, ierr = linker:instantiate(store, c)
print("instantiate:", inst ~= nil, ierr)
if not inst then return end

local fadd = inst:getExport("add")
print("getExport add:", type(fadd))
if type(fadd) == "userdata" then
    local ok, r = pcall(function() return fadd:call(20, 22) end)
    print("add(20,22) pcall:", ok, "result=", r, " expect 42")
end
