-- 综合验证：Rust+cargo-component 生成的真实组件
local w = require("wasmtime")

local f = io.open("_rust_demo.wasm", "rb")
if not f then print("找不到 _rust_demo.wasm"); return end
local bytes = f:read("*a")
f:close()

local engine = w.newEngine()
local store = w.newStore(engine)
local c = w.newComponent(engine, bytes)
assert(type(c) == "userdata", "newComponent 失败: " .. tostring(c))

local linker = w.newComponentLinker(engine)
local inst, ierr = linker:instantiate(store, c)
assert(inst, "instantiate 失败: " .. tostring(ierr))

local npass, nfail = 0, 0
local function check(name, fn, want)
    local ok, got = pcall(fn)
    if not ok then
        nfail = nfail + 1
        print(string.format("[FAIL] %-14s error: %s", name, tostring(got)))
        return
    end
    local g = got
    if type(g) == "number" then g = string.format("%g", g) end
    if type(g) == "table" then
        local parts = {}
        for k, v in pairs(g) do
            if type(v) == "number" then v = string.format("%g", v) end
            table.insert(parts, tostring(k) .. "=" .. tostring(v))
        end
        table.sort(parts)
        g = "{" .. table.concat(parts, ",") .. "}"
    end
    local wstr = want
    if type(want) == "number" then wstr = string.format("%g", want) end
    if g == wstr then
        npass = npass + 1
        print(string.format("[OK] %-14s got=%s", name, g))
    else
        nfail = nfail + 1
        print(string.format("[FAIL] %-14s got=%s want=%s", name, g, wstr))
    end
end

check("add", function() return inst:getExport("add"):call(20, 22) end, 42)
check("sub", function() return inst:getExport("sub"):call(5, 8) end, -3)
check("mul-f", function() return inst:getExport("mul-f"):call(2.5, 4) end, 10)
check("div-d", function() return inst:getExport("div-d"):call(7.5, 2) end, 3.75)
check("not-bool", function() return inst:getExport("not-bool"):call(true) end, false)
check("greet", function() return inst:getExport("greet"):call("豆包") end, "Hello, 豆包!")
check("sum-list", function() return inst:getExport("sum-list"):call({1, 2, 3, 4}) end, 10)
check("sum-list-empty", function() return inst:getExport("sum-list"):call({}) end, 0)
check("double-list", function()
    local r = inst:getExport("double-list"):call({1, 2, 3})
    return table.concat(r, ",")
end, "2,4,6")
check("double-list-empty", function()
    local r = inst:getExport("double-list"):call({})
    return #r
end, 0)
check("make-pair", function()
    local r = inst:getExport("make-pair"):call(10, 32)
    return r.x .. "|" .. r.y
end, "10|32")
check("swap-pair", function()
    local r = inst:getExport("swap-pair"):call{x = 7, y = 9}
    return r.x .. "|" .. r.y
end, "9|7")
check("try-get-ok", function()
    local r = inst:getExport("try-get"):call(true)
    return r.ok
end, 42)
check("try-get-err", function()
    local r = inst:getExport("try-get"):call(false)
    return r.err
end, "boom")
check("pick-red", function()
    local r = inst:getExport("pick"):call("red", 5)
    return tostring(r)
end, "5")
check("pick-green", function()
    local r = inst:getExport("pick"):call("green", 5)
    return tostring(r)
end, "nil")
check("pick-blue", function()
    local r = inst:getExport("pick"):call("blue", 5)
    return tostring(r)
end, "6")
check("echo-maybe-some", function()
    local r = inst:getExport("echo-maybe"):call{some = 3}
    return r.some
end, 3)
check("echo-maybe-none", function()
    local r = inst:getExport("echo-maybe"):call("none")
    local k = next(r)
    return k
end, "none")
check("count-flags", function()
    return inst:getExport("count-flags"):call{read = true, exec = true}
end, 2)
check("count-flags-0", function()
    return inst:getExport("count-flags"):call{}
end, 0)

print(string.format("=== 结果: %d passed, %d failed, %d total ===", npass, nfail, npass + nfail))
