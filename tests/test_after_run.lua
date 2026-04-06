-- 直接运行 test_complete 但在测试4后额外加 wrap
local asyncio = require("asyncio")
local w = asyncio.wait

-- 先执行 test_complete 测试1-3（run+sleep）
print("=== T1: run+sleep ===")
local p = asyncio.run(function() return w(asyncio.sleep(0.01)) end)
assert(p:await_sync(1000) == true)
print("PASS")

print("=== T2: wrap with sleep (like test_complete T4) ===")
local fn = asyncio.wrap(function(x)
    w(asyncio.sleep(0.01))
    return x * 2
end)
assert(fn(21):await_sync(1000) == 42)
print("PASS")

print("=== T3: wrap WITHOUT sleep (THIS SHOULD CRASH) ===")
local d = asyncio.wrap(function(x) return x * 2 end)  -- 崩溃点？
print("wrap done! type="..type(d))
local r = d(7):await_sync(1000)
print("r="..tostring(r))
