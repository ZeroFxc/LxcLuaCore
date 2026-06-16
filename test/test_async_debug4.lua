-- 测试：使用 asyncio.run 直接运行 async 函数
local asyncio = require("asyncio")

print("=== 测试: asyncio.run 直接运行 ===")
local function test_func()
    print("  函数体内: 准备 sleep")
    local p = asyncio.sleep(0.01)
    print("  sleep 返回:", type(p))
    -- 手动 yield
    coroutine.yield(p)
    print("  恢复后继续执行")
    return 99
end

-- 使用 asyncio.run 包装
local promise = asyncio.run(test_func)
print("asyncio.run 返回:", type(promise))
print("Promise state:", promise.state)

-- 等待完成
local ok, result = pcall(function() return promise:await_sync() end)
print("await_sync 结果:", ok, result)