-- 最小复现：只运行 wrap 一次
local asyncio = require("asyncio")
print("step1: require ok")

print("step2: calling wrap...")
local double = asyncio.wrap(function(x) return x * 2 end)
print("step3: wrap done, type="..type(double))

print("step4: calling double(7)...")
local r = double(7):await_sync(1000)
print("step5: result="..tostring(r))
