-- 测试 component list 返回方向
local w = require("wasmtime")

local f = io.open("_lt.wasm", "rb")
if not f then print("找不到 _lt.wasm"); return end
local bytes = f:read("*a")
f:close()

local engine = w.newEngine()
local store = w.newStore(engine)
local c = w.newComponent(engine, bytes)
assert(type(c) == "userdata", "newComponent 失败")

local linker = w.newComponentLinker(engine)
local inst, ierr = linker:instantiate(store, c)
assert(inst, "instantiate 失败: " .. tostring(ierr))

local lt = inst:getExport("list-tail")
print("getExport list-tail:", type(lt))
if type(lt) ~= "userdata" then return end

local ok, res = pcall(function() return lt:call{1, 2, 3} end)
print("list-tail pcall:", ok)
if ok then
    print("返回值类型:", type(res))
    if type(res) == "table" then
        for i, v in ipairs(res) do print(string.format("  [%d] = %s", i, tostring(v))) end
    end
end
