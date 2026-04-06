-- 测试：no-yield wrap 是否能"修复"后续 yield-wrap 后的状态
local asyncio = require("asyncio")
local w = asyncio.wait

-- 先执行一个 no-yield wrap（像 test_yield_vs_no 的 TEST A）
print("0: no-yield warmup...")
local fw = asyncio.wrap(function(x) return x end)
local pw = fw(99)
local rw = pw:await_sync(1000)
print("warmup done r="..tostring(rw))

-- 现在做和 test_2wraps 一样的事
print("\n1: yield wrap...")
local f1 = asyncio.wrap(function() w(asyncio.sleep(0.01)); return "A" end)
local p1 = f1()
local r1 = p1:await_sync(1000)
print("r1="..tostring(r1))

print("\n2: yield wrap again...")
local f2 = asyncio.wrap(function() w(asyncio.sleep(0.01)); return "B" end)
print("f2 created!")
local p2 = f2()
local r2 = p2:await_sync(1000)
print("r2="..tostring(r2))

print("\nALL DONE!")
