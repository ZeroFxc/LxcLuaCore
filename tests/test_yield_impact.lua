-- 对比：no-yield wrap vs yield-wrap 对 then 链的影响
local asyncio = require("asyncio")
local w = asyncio.wait

local passed, failed = 0, 0
local function test(name, fn)
    local ok, err = pcall(fn)
    if ok then passed = passed + 1 else failed = failed + 1 end
    print(string.format("  %s %s", ok and "✅" or "❌", name))
    if not ok then print("    -> " .. tostring(err)) end
end

-- 只用 no-yield wrap
print("=== A: no-yield wrap only ===")
test("no-yield wrap", function()
    local fn = asyncio.wrap(function(x) return x * 2 end)
    assert(fn(21):await_sync(1000) == 42)
end)

test(".then after no-yield wrap", function()
    local result = nil
    asyncio.sleep(0.001)
        :done(function(v) return 10 end)
        :done(function(v) return v * 2 end)
        :done(function(v) result = v end)
    asyncio.sleep(0.05):await_sync(200)
    print("    result="..tostring(result))
    assert(result == 20, "got "..tostring(result))
end)

print("\n=== B: yield-wrap (THIS SHOULD FAIL) ===")
test("yield-wrap", function()
    local fn = asyncio.wrap(function(x)
        w(asyncio.sleep(0.01))
        return x * 2
    end)
    assert(fn(21):await_sync(1000) == 42)
end)

test(".then after yield-wrap", function()
    local result = nil
    asyncio.sleep(0.001)
        :done(function(v) return 10 end)
        :done(function(v) return v * 2 end)
        :done(function(v) result = v end)
    asyncio.sleep(0.05):await_sync(200)
    print("    result="..tostring(result))
    assert(result == 20, "got "..tostring(result))
end)

print("\n" .. passed .. "/" .. (passed+failed))
