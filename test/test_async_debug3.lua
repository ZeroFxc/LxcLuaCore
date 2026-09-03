-- 最小调试：测试 async 函数内部的 await 行为
local asyncio = require("asyncio")

print("=== 测试: 基本协程 yield ===")
local co = coroutine.create(function()
    print("  协程内: 准备 yield")
    local p = asyncio.sleep(0.01)
    print("  sleep 返回值类型:", type(p))
    print("  sleep 返回值是否为 Promise:", asyncio.isPromise(p))
    
    -- 尝试手动 yield
    local result = coroutine.yield(p)
    print("  yield 后恢复, result:", result)
    return "done"
end)

-- 第一次 resume
local ok, yielded = coroutine.resume(co)
print("第一次 resume:", ok, type(yielded))
print("yielded isPromise:", asyncio.isPromise(yielded))

-- 如果是 Promise，等待它完成
if asyncio.isPromise(yielded) then
    print("等待 Promise 完成...")
    yielded:await_sync()
    print("Promise 完成，恢复协程")
    local ok2, result = coroutine.resume(co)
    print("第二次 resume:", ok2, result)
end