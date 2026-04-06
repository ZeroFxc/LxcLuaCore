-- 最小化测试 race
local asyncio = require("asyncio")
local w = asyncio.wait

print("step1: create fast (async 0.01s)")
local fast = asyncio.run(function()
    w(asyncio.sleep(0.01))
    return "fast"
end)
print("step2: fast created")

print("step3: create slow (async 0.1s)")
local slow = asyncio.run(function()
    w(asyncio.sleep(0.1))
    return "slow"
end)
print("step4: slow created")

print("step5: calling asyncio.race(fast, slow)")
local race_p = asyncio.race(fast, slow)
print("step6: race_p created")

print("step7: awaiting...")
local r = race_p:await_sync(2000)
print("step8: result="..tostring(r))
