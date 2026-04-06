-- 逐步排查
local asyncio = require("asyncio")
local w = asyncio.wait

print("=== A: 只有 wrap ===")
do
    local double = asyncio.wrap(function(x) return x * 2 end)
    local r = double(7):await_sync(1000)
    print("A: r="..tostring(r).." (expect 14)")
end

print("=== B: wrap + run(sleep) ===")
do
    local fetch = asyncio.wrap(function(x)
        w(asyncio.sleep(0.01))
        return x * 2
    end)
    local p = fetch(21):await_sync(1000)
    print("B: p="..tostring(p).." (expect 42)")
end

print("=== C: 再次 wrap (B之后) ===")
do
    print("C: calling wrap...")
    local double = asyncio.wrap(function(x) return x * 2 end)
    print("C: wrap done")

    print("C: calling double(7)...")
    local r = double(7):await_sync(1000)
    print("C: r="..tostring(r).." (expect 14)")
end

print("=== D: run + wrap ===")
do
    local p = asyncio.run(function()
        w(asyncio.sleep(0.01))
        return "run-ok"
    end)
    local r = p:await_sync(1000)
    print("D: run result="..tostring(r))

    print("D: now creating wrap after run...")
    local double = asyncio.wrap(function(x) return x * 2 end)
    local r2 = double(7):await_sync(1000)
    print("D: wrap result="..tostring(r2).." (expect 14)")
end

print("\nAll steps done!")
