-- 二分法：逐步包含 test_complete.lua 的测试
local asyncio = require("asyncio")
local w = asyncio.wait

local passed, failed = 0, 0
local function test(name, fn)
    local ok, err = pcall(fn)
    if ok then passed = passed + 1 else failed = failed + 1 end
    print(string.format("  %s %s", ok and "✅" or "❌", name))
    if not ok then print("    -> " .. tostring(err)) end
end

print("=== Running tests 1-3 (core) ===")
test("wait(sleep) 基础", function()
    local p = asyncio.run(function() return w(asyncio.sleep(0.01)) end)
    assert(p:await_sync(1000) == true)
end)

test("多次顺序 wait", function()
    local p = asyncio.run(function()
        w(asyncio.sleep(0.005))
        w(asyncio.sleep(0.005))
        return "seq-ok"
    end)
    assert(p:await_sync(1000) == "seq-ok")
end)

test("返回值传递 table", function()
    local p = asyncio.run(function()
        w(asyncio.sleep(0.01))
        return {name="test", value=42}
    end)
    local r = p:await_sync(1000)
    assert(r.name == "test" and r.value == 42)
end)

print("\n=== Test 4: wrap (suspect!) ===")
test("wrap 包装器参数传递", function()
    local fn = asyncio.wrap(function(x)
        w(asyncio.sleep(0.01))
        return x * 2
    end)
    assert(fn(21):await_sync(1000) == 42)
end)

print("\n=== Now test .then chain ===")
test(".then chain value passing", function()
    local result = nil
    asyncio.sleep(0.001)
        :done(function(v) return 10 end)
        :done(function(v) return v * 2 end)
        :done(function(v) result = v end)
    asyncio.sleep(0.05):await_sync(200)
    print("    result=" .. tostring(result))
    assert(result == 20, "then chain got " .. tostring(result))
end)

print("\n" .. passed .. "/" .. (passed+failed) .. " passed")
