-- 对比测试：await 语法 vs asyncio.wait 语法
local asyncio = require("asyncio")

-- 方法A: await 语法（编译后的函数体）
local async function test_a()
    await(asyncio.sleep(0.01))
    return 99
end
local _, body_a = debug.getupvalue(test_a, 1)

-- 方法B: asyncio.wait 语法（编译后的函数体）
local async function test_b()
    asyncio.wait(asyncio.sleep(0.01))
    return 99
end
local _, body_b = debug.getupvalue(test_b, 1)

-- 方法C: 手动 coroutine.yield
local function test_c()
    coroutine.yield(asyncio.sleep(0.01))
    return 99
end

-- 测试各函数体在协程中执行
print("=== A: await 语法 ===")
local co = coroutine.create(body_a)
local ok, res = coroutine.resume(co)
print("ok:", ok, "res:", res, "type:", ok and type(res) or res)

print("=== B: asyncio.wait 语法 ===")
co = coroutine.create(body_b)
ok, res = coroutine.resume(co)
print("ok:", ok, "res:", res, "type:", ok and type(res) or res)

print("=== C: 手动 coroutine.yield ===")
co = coroutine.create(test_c)
ok, res = coroutine.resume(co)
print("ok:", ok, "res:", res, "type:", ok and type(res) or res)