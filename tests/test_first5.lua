-- 只运行 test_complete 前5个测试（含 wrap）
local asyncio = require("asyncio")
local w = asyncio.wait

local passed = 0
local function test(name, fn)
    local ok, err = pcall(fn)
    if ok then passed = passed + 1; print("  ✅ "..name)
    else print("  ❌ "..name..": "..tostring(err)) end
end

test("wait(sleep) 基础", function()
    local p = asyncio.run(function() return w(asyncio.sleep(0.01)) end)
    assert(p:await_sync(1000) == true)
end)

test("多次顺序 wait", function()
    local p = asyncio.run(function() w(asyncio.sleep(0.005)); w(asyncio.sleep(0.005)); return "ok" end)
    assert(p:await_sync(1000) == "ok")
end)

test("返回值传递 table", function()
    local p = asyncio.run(function() w(asyncio.sleep(0.01)); return {name="t", v=42} end)
    local r = p:await_sync(1000); assert(r.name == "t" and r.v == 42)
end)

test("wrap 包装器参数传递", function()
    local fn = asyncio.wrap(function(x) w(asyncio.sleep(0.01)); return x * 2 end)
    assert(fn(21):await_sync(1000) == 42)
end)

print("\nNow calling wrap OUTSIDE pcall...")
local d = asyncio.wrap(function(x) return x * 2 end)
print("wrap done! type="..type(d))
