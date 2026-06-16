-- 直接测试 await 编译的字节码
local asyncio = require("asyncio")

-- 获取 async 函数体
local async function test_async()
    await(asyncio.sleep(0.01))
    return 99
end

-- 获取函数体（upvalue）
local name, body = debug.getupvalue(test_async, 1)
print("body type:", type(body))

-- 直接在新协程中尝试执行函数体
local co = coroutine.create(body)
local ok, res = coroutine.resume(co)
print("第一次 resume:", ok, res)
if ok and res then
    print("yield 的值类型:", type(res))
    -- 检查是否是 Promise
    local mt = getmetatable(res)
    print("元表:", mt)
    -- 模拟 Promise 完成后的恢复
    ok, res = coroutine.resume(co, true)
    print("第二次 resume:", ok, res)
end