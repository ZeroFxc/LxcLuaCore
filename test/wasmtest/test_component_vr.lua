-- 测试 component variant/result 类型双向转换
local w = require("wasmtime")

local f = io.open("_vr.wasm", "rb")
if not f then print("找不到 _vr.wasm"); return end
local bytes = f:read("*a")
f:close()

local engine = w.newEngine()
local store = w.newStore(engine)
local c = w.newComponent(engine, bytes)
assert(type(c) == "userdata", "newComponent 失败")

local linker = w.newComponentLinker(engine)
local inst, ierr = linker:instantiate(store, c)
assert(inst, "instantiate 失败: " .. tostring(ierr))

local npass = 0
local function check(name, got, want)
    local g = (type(got) == "table") and ("{" .. tostring(next(got)) .. "}") or tostring(got)
    local wstr = (type(want) == "table") and ("{" .. tostring(next(want)) .. "}") or tostring(want)
    if g == wstr then
        npass = npass + 1
        print(string.format("[OK] %-18s got=%s", name, g))
    else
        print(string.format("[FAIL] %-18s got=%s want=%s", name, g, wstr))
    end
end

-- variant-echo: 输入 {name, payload} 或单键 map
local ve = inst:getExport("variant-echo")
local ok1, r1 = pcall(function() return ve:call{"some", 42} end)
print("variant-echo {'some',42}:", ok1)
if ok1 and type(r1) == "table" then
    local k, v = next(r1)
    print("  => key=", k, "val=", tostring(v))
end
-- variant none（无载荷）：传纯 string
local ok2, r2 = pcall(function() return ve:call("none") end)
print("variant-echo 'none':", ok2)
if ok2 and type(r2) == "table" then
    print("  => key=", tostring(next(r2)))
end

-- result-echo: 输入 {ok=v} / {err=v}
local re = inst:getExport("result-echo")
local ok3, r3 = pcall(function() return re:call{ok=7} end)
print("result-echo {ok=7}:", ok3)
if ok3 and type(r3) == "table" then
    local k, v = next(r3)
    print("  => key=", tostring(k), "val=", tostring(v))
end
local ok4, r4 = pcall(function() return re:call{err=9} end)
print("result-echo {err=9}:", ok4)
if ok4 and type(r4) == "table" then
    local k, v = next(r4)
    print("  => key=", tostring(k), "val=", tostring(v))
end

print("=== 完成（手动核对上述输出） ===")
