-- 最小化调试：await 语法问题
local asyncio = require("asyncio")

-- 测试1：最简单的 async + await
local async function test1()
    local lock = asyncio.Lock()
    await(lock:acquire())
    lock:release()
    return 42
end

print("calling test1...")
local result = test1():await_sync()
print("result:", result)