-- 最小化调试
local asyncio = require("asyncio")

-- 先看 asyncio 是否加载成功
print("asyncio type:", type(asyncio))
print("asyncio.run:", type(asyncio.run))
print("asyncio.sleep:", type(asyncio.sleep))

-- 测试最简单的 async 函数
local async function test_simple()
    return 42
end

print("test_simple type:", type(test_simple))

-- 尝试调用
local ret = test_simple()
print("test_simple() type:", type(ret))
print("test_simple() ret:", ret)
if type(ret) == "userdata" then
    print("  is Promise?", ret.state)
    local ok, v = pcall(function() return ret:await_sync() end)
    print("  await_sync:", ok, v)
end