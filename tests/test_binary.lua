-- 二分法定位崩溃测试
local asyncio = require("asyncio")
local w = asyncio.wait

local passed = 0
local function test(name, fn)
    local ok, err = pcall(fn)
    if ok then passed = passed + 1; print("  ✅ "..name)
    else print("  ❌ "..name..": "..tostring(err)); os.exit(1) end
end

print("--- 只跑 T1 ---")
test("T1-wrap+sleep", function()
    local f = asyncio.wrap(function(x) w(asyncio.sleep(0.01)); return x*2 end)
    assert(f(21):await_sync(1000) == 42)
end)

print("\n--- 跑 T1+T2 ---")
test("T2-wait(sleep)", function()
    local p = asyncio.run(function() local r=w(asyncio.sleep(0.01)); return r and "ok" or "err" end)
    assert(p:await_sync(1000) == "ok")
end)

print("\n--- 跑 T1+T2+T3 (arrow) ---")
test("T3-arrow", function()
    local d = asyncio.wrap(function(x) return x*2 end)
    -- 注意：这个 wrap 不含 sleep/await
    local r = d(7):await_sync(1000)
    assert(r == 14)
end)

print("\nAll "..passed.." passed!")
