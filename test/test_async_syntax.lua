-- 测试 async/await 语法（不依赖 Lock，只用 sleep 等已有原语）
local asyncio = require("asyncio")

-- 测试1：最简单的 async 函数（无 await）
print("=== 测试1: async 函数无 await ===")
local async function test_noawait()
    return 42
end
local ok, result = pcall(function() return test_noawait():await_sync() end)
print("结果:", ok, result)
assert(ok and result == 42, "测试1失败: " .. tostring(result))

-- 测试2：async + await(sleep) 语法
print("=== 测试2: await(sleep) ===")
local async function test_sleep()
    await(asyncio.sleep(0.01))
    return 99
end
local ok, result = pcall(function() return test_sleep():await_sync() end)
print("结果:", ok, result)
assert(ok and result == 99, "测试2失败: " .. tostring(result))

-- 测试3：await 作为表达式（赋值）
print("=== 测试3: local x = await(sleep) ===")
local async function test_await_expr()
    local wait = asyncio.wait
    local x = await(asyncio.sleep(0.01))
    return x
end
local ok, result = pcall(function() return test_await_expr():await_sync() end)
print("结果:", ok, result)
assert(ok and result ~= nil, "测试3失败: " .. tostring(result))

-- 测试4：嵌套 async 调用
print("=== 测试4: 嵌套 async 调用 ===")
local async function inner(x)
    await(asyncio.sleep(0.01))
    return x * 2
end
local async function outer()
    local v = await(inner(21))
    return v + 1
end
local ok, result = pcall(function() return outer():await_sync() end)
print("结果:", ok, result)
assert(ok and result == 43, "测试4失败: " .. tostring(result))

-- 测试5：多个 await 串联
print("=== 测试5: 多个 await 串联 ===")
local async function test_chain()
    await(asyncio.sleep(0.01))
    await(asyncio.sleep(0.01))
    await(asyncio.sleep(0.01))
    return "done"
end
local ok, result = pcall(function() return test_chain():await_sync() end)
print("结果:", ok, result)
assert(ok and result == "done", "测试5失败: " .. tostring(result))

print("\n=== 全部测试通过 ===")