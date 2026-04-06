-- 精确定位：wrap+call+await 后 VM 状态
local asyncio = require("asyncio")
local w = asyncio.wait

print("1: before wrap")

print("2: wrapping...")
local fn = asyncio.wrap(function() w(asyncio.sleep(0.01)); return 99 end)
print("3: wrap done, type="..type(fn))

print("4: calling...")
local p = fn()
print("5: call done, type="..type(p))

print("6: awaiting...")
local r = p:await_sync(1000)
print("7: await done, r="..tostring(r))

print("8: pure lua...")
local t = {1,2,3}
print("9: table ok #"..#t)

print("10: wrapping AGAIN...")
local fn2 = asyncio.wrap(function(x) return x*2 end)
print("11: wrap2 done, type="..type(fn2))

print("12: ALL DONE!")
