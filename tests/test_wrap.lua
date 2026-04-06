-- 只测试 wrap
local asyncio = require("asyncio")
local w = asyncio.wait

print("step1: define wrap fn")
local fn = asyncio.wrap(function(x)
    w(asyncio.sleep(0.01))
    return x * 2
end)
print("step2: wrap created, type="..type(fn))

print("step3: call fn(21)")
local p = fn(21)
print("step4: p type="..type(p))

print("step5: await_sync")
local r = p:await_sync(1000)
print("step6: result="..tostring(r).." (expect 42)")
