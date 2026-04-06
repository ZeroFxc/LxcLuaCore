-- 直接复制 test_complete.lua 的测试4（通过的那个）
-- 然后紧接着再调用一次 wrap
local asyncio = require("asyncio")
local w = asyncio.wait

print("=== Exact copy of test_complete test4 ===")
do
    local fn = asyncio.wrap(function(x)
        w(asyncio.sleep(0.01))
        return x * 2
    end)
    local r = fn(21):await_sync(1000)
    print("r="..tostring(r).." (expect 42)")
    assert(r == 42)
    print("PASS")
end

print("\n=== Second wrap (like test_syntax_sugar T3) ===")
do
    print("calling asyncio.wrap...")
    local d = asyncio.wrap(function(x) return x * 2 end)  -- 无 sleep
    print("wrap done, type="..type(d))

    print("calling d(7)...")
    local r = d(7):await_sync(1000)
    print("r="..tostring(r).." (expect 14)")
end

print("\nDONE!")
