-- 强制 GC 定位是否 __gc 导致崩溃
local asyncio = require("asyncio")
local w = asyncio.wait

print("T1...")
local fn = asyncio.wrap(function() w(asyncio.sleep(0.01)); return 99 end)
local p = fn()
local r = p:await_sync(1000)
print("T1 OK r="..tostring(r))

-- p 不再需要，设为 nil 让 GC 可回收
p = nil
fn = nil

print("GC before T2...")
collectgarbage()
collectgarbage()
print("after GC")

print("T2 wrapping...")
local fn2 = asyncio.wrap(function(x) return x*2 end)
print("fn2="..type(fn2))

print("DONE!")
