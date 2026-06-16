-- 测试：手动模拟 await 语法生成的字节码
local asyncio = require("asyncio")

print("=== 测试: 手动模拟 await ===")
-- 这是解析器生成的 await(asyncio.sleep(0.01)) 的等价代码
local function test_body()
    -- await(expr) 被编译为 coroutine.yield(expr)
    local result = coroutine.yield(asyncio.sleep(0.01))
    print("  协程恢复后 result:", result)
    return 99
end

local promise = asyncio.run(test_body)
print("Promise state:", promise.state)

local ok, result = pcall(function() return promise:await_sync() end)
print("await_sync 结果:", ok, result)