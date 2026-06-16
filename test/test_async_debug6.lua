-- 关键对比测试
local asyncio = require("asyncio")

-- 方法A: 解析器编译的 async 函数（失败）
print("=== A: async function syntax ===")
local async function test_a()
    await(asyncio.sleep(0.01))
    return 99
end
local ok_a, result_a = pcall(function() return test_a():await_sync() end)
print("A 结果:", ok_a, result_a)

-- 方法B: 手动写等价代码 + asyncio.run（成功）
print("=== B: 手动 coroutine.yield ===")
local function test_b()
    coroutine.yield(asyncio.sleep(0.01))
    return 99
end
local promise_b = asyncio.run(test_b)
local ok_b, result_b = pcall(function() return promise_b:await_sync() end)
print("B 结果:", ok_b, result_b)

-- 方法C: async function 但手动调用 await 别名
print("=== C: async function + asyncio.wait ===")
local async function test_c()
    asyncio.wait(asyncio.sleep(0.01))
    return 99
end
local ok_c, result_c = pcall(function() return test_c():await_sync() end)
print("C 结果:", ok_c, result_c)