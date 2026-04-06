-- 诊断：检查 promise_all 的参数顺序
local asyncio = require("asyncio")
local w = asyncio.wait

-- 创建3个有不同返回值的 promise
local pa = asyncio.run(function() return "A" end)
local pb = asyncio.run(function() return "B" end)
local pc = asyncio.run(function() return "C" end)

print("calling all(A, B, C)")
local r = asyncio.all(pa, pb, pc):await_sync(1000)
print("result table:")
for i,v in ipairs(r) do
    print("  ["..i.."] = "..tostring(v))
end
print("expect: [1]=A [2]=B [3]=C")
