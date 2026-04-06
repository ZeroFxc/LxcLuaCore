-- 测试：T1 用 run（不用 wrap），然后 T2 用 wrap
local asyncio = require("asyncio")
local w = asyncio.wait

print("=== T1: run(sleep) instead of wrap ===")
local p = asyncio.run(function()
    w(asyncio.sleep(0.01))
    return "from-run"
end)
local r = p:await_sync(1000)
print("r="..tostring(r))

print("\n=== T2: wrap (simple, no sleep) ===")
local d = asyncio.wrap(function(x) return x*2 end)
print("type="..type(d))
local r2 = d(7):await_sync(1000)
print("r2="..tostring(r2))

print("\n=== T3: wrap with sleep ===")
local f = asyncio.wrap(function() w(asyncio.sleep(0.01)); return 99 end)
local r3 = f():await_sync(1000)
print("r3="..tostring(r3))

print("\n=== T4: wrap again ===")
local g = asyncio.wrap(function() return "final" end)
print("type="..type(g))
local r4 = g():await_sync(1000)
print("r4="..tostring(r4))

print("\nALL DONE!")
