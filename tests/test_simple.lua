-- 简单测试
local asyncio = require("asyncio")

print("Test 1: Basic run without await")
local p1 = asyncio.run(function()
    print("  Inside async function")
    return "OK"
end)
if p1 then
    local r = p1:await_sync(1000)
    print("  Result: " .. tostring(r))
end

print("\nTest 2: Run with sleep")
local p2 = asyncio.run(function()
    print("  Before sleep")
    local wait = asyncio.wait
    -- 不使用 await，直接返回
    return "After sleep setup"
end)
if p2 then
    local r = p2:await_sync(1000)
    print("  Result: " .. tostring(r))
end

print("\nDone!")
