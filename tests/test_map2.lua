-- 最简测试：直接调用 aio_run_async 等效操作
local asyncio = require("asyncio")

print("step1: create async fn")
local fn = asyncio.wrap(function(x)
    return x * 10
end)
print("step2: fn created")

print("step3: call fn(1)")
local p1 = fn(1)
print("step4: p1 type="..type(p1))

print("step5: call fn(2)")
local p2 = fn(2)
print("step6: p2 type="..type(p2))

print("step7: call fn(3)")
local p3 = fn(3)
print("step8: p3 type="..type(p3))

print("step9: all")
local r = asyncio.all(p1,p2,p3):await_sync(2000)
print("step10: "..tostring(r))
