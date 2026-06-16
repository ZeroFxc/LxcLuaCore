-- 调试 coroutine.yield 和 await 问题
print("coroutine type:", type(coroutine))
print("coroutine.yield type:", type(coroutine.yield))

local asyncio = require("asyncio")

-- 测试：手动 coroutine.yield Promise
local async function test_manual()
    print("  in test_manual, about to yield...")
    local p = asyncio.sleep(0.01)
    print("  promise state:", p.state)
    local result = coroutine.yield(p)
    print("  after yield, result:", result)
    return 99
end

print("calling test_manual...")
local ret = test_manual()
print("ret type:", type(ret))
print("ret state:", ret.state)
local ok, v = pcall(function() return ret:await_sync() end)
print("result:", ok, v)