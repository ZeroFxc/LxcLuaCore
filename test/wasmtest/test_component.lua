-- 测试 component：用 wat2wasm 编译最小 component（无 WASI imports）
local w = require("wasmtime")

-- 最小组件：lift 一个 core module 的 add 函数
local comp_wat = [[
(component
  (core module $M
    (func (export "add") (param i32 i32) (result i32)
      local.get 0
      local.get 1
      i32.add)
    (func (export "mul") (param i32 i32) (result i32)
      local.get 0
      local.get 1
      i32.mul))
  (core instance $i (instantiate $M))
  (func (export "add")
    (param "a" u32)
    (param "b" u32)
    (result u32)
    (canon lift (core func $i "add")
      (param i32 i32) (result i32)))
  (func (export "mul")
    (param "a" u32)
    (param "b" u32)
    (result u32)
    (canon lift (core func $i "mul")
      (param i32 i32) (result i32)))
)
]]

local comp, cerr = w.wat2wasm(comp_wat)
print("wat2wasm(component):", comp ~= nil, cerr or ("bytes=" .. #comp))
if not comp then return end

local engine = w.newEngine()
local store = w.newStore(engine)
local c = w.newComponent(engine, comp)
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
    local r = fadd:call(20, 22)
    print("add(20,22) =", r, " expect 42")
end

local fmul = inst:getExport("mul")
if type(fmul) == "userdata" then
    local r = fmul:call(7, 8)
    print("mul(7,8) =", r, " expect 56")
end
