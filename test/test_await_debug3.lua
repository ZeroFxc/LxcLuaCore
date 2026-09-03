-- 逐层调试 await 语法问题
local asyncio = require("asyncio")

-- 测试A：不用 await，直接 coroutine.yield
print("=== 测试A: 手动 coroutine.yield ===")
local async function test_a()
    local lock = asyncio.Lock()
    local p = lock:acquire()
    coroutine.yield(p)
    lock:release()
    return 42
end
local ok, result = pcall(function() return test_a():await_sync() end)
print("test_a result:", ok, result)

-- 测试B：用 asyncio.wait (非保留字方式) 
print("=== 测试B: asyncio.wait ===")
local wait = asyncio.wait
local async function test_b()
    local lock = asyncio.Lock()
    local p = lock:acquire()
    wait(p)
    lock:release()
    return 42
end
local ok, result = pcall(function() return test_b():await_sync() end)
print("test_b result:", ok, result)

-- 测试C：最简单的 async 函数，不用 await
print("=== 测试C: 不用 await ===")
local async function test_c()
    return 42
end
local ok, result = pcall(function() return test_c():await_sync() end)
print("test_c result:", ok, result)