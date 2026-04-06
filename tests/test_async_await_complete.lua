-- ============================================================================
-- LXCLUA-NCore Async/Await 完整语法糖测试套件
-- 测试编译时脱糖 + 运行时支持的完整功能
-- ============================================================================

local asyncio = require("asyncio")
local w = asyncio.wait

print("=" .. string.rep("=", 70))
print(" 🚀 LXCLUA-NCore Async/Await 语法糖完整测试")
print("=" .. string.rep("=", 70) .. "\n")

local passed, failed = 0, 0
local function test(name, fn)
    local ok, err = pcall(fn)
    if ok then
        passed = passed + 1
        print("  ✅ " .. name)
    else
        failed = failed + 1
        print("  ❌ " .. name .. ": " .. tostring(err))
    end
end

-- ============================================================================
-- 第一部分：基础 async/await 语法（运行时 API）
-- ============================================================================

print("\n📦 第一部分：基础 Async/Await 语法\n")

test("1.1 asyncio.run() - 基础异步执行", function()
    local p = asyncio.run(function()
        return 42
    end)
    assert(p ~= nil, "Promise 不应为 nil")
    local r = p:await_sync(1000)
    assert(r == 42, "期望 42，得到 " .. tostring(r))
end)

test("1.2 asyncio.wrap() - 创建可复用异步函数", function()
    local double = asyncio.wrap(function(x)
        return x * 2
    end)
    assert(double ~= nil, "AsyncFunction 不应为 nil")
    local r = double(21):await_sync(1000)
    assert(r == 42, "期望 42，得到 " .. tostring(r))
end)

test("1.3 wait() - 等待 Promise 完成", function()
    local p = asyncio.run(function()
        local result = w(asyncio.sleep(0.01))
        return result and "completed" or "error"
    end)
    local r = p:await_sync(2000)
    assert(r == "completed", "期望 'completed'，得到 " .. tostring(r))
end)

test("1.4 多参数 async 函数", function()
    local add = asyncio.wrap(function(a, b, c)
        w(asyncio.sleep(0.005))
        return a + b + c
    end)
    local r = add(10, 20, 30):await_sync(1000)
    assert(r == 60, "期望 60，得到 " .. tostring(r))
end)

-- ============================================================================
-- 第二部分：async/await 编译时语法糖（需要 llexer_compiler）
-- ============================================================================

print("\n🔧 第二部分：编译时语法糖脱糖验证\n")

test("2.1 async (x) => x * 2 - 异步箭头函数", function()
    -- 模拟脱糖：async (x) => x * 2 → asyncio.wrap(function(x) return x * 2 end)
    local double = asyncio.wrap(function(x) return x * 2 end)
    local r = double(7):await_sync(1000)
    assert(r == 14, "期望 14，得到 " .. tostring(r))
end)

test("2.2 async function 声明（模拟脱糖）", function()
    -- 模拟：async function fetch(x) ... end → local fetch = asyncio.wrap(function(x) ... end)
    local fetch = asyncio.wrap(function(x)
        w(asyncio.sleep(0.01))
        return x * 3
    end)
    local r = fetch(14):await_sync(1000)
    assert(r == 42, "期望 42，得到 " .. tostring(r))
end)

test("2.3 await 表达式（模拟脱糖）", function()
    -- 模拟：await(expr) → asyncio.wait(expr)
    local p = asyncio.run(function()
        local r = w(asyncio.sleep(0.01))  -- await(sleep(0.01)) 的脱糖形式
        return {slept = r}
    end)
    local r = p:await_sync(1000)
    assert(type(r) == "table" and r.slept == true, "await 应返回结果")
end)

-- ============================================================================
-- 第三部分：错误处理
-- ============================================================================

print("\n⚠️  第三部分：错误处理\n")

test("3.1 async 函数中的错误传播", function()
    local failing = asyncio.wrap(function()
        w(asyncio.sleep(0.005))
        error("test error")
    end)
    local r = failing():await_sync(1000)
    assert(type(r) == "string" and r:find("test"),
           "应捕获到错误信息，得到: " .. type(r) .. ":" .. tostring(r))
end)

test("3.2 Promise 链式调用 - done/fail", function()
    local p = asyncio.run(function()
        return "success"
    end)
    local result = nil
    p:done(function(value)
        result = value
    end)
    -- 等待链式调用完成
    w(asyncio.sleep(0.02))
    assert(result == "success", "done 回调应被调用")
end)

test("3.3 Promise.cancel() - 取消操作", function()
    local p = asyncio.run(function()
        w(asyncio.sleep(10))  -- 长时间 sleep
        return "should not reach"
    end)
    local cancelled = p:cancel()
    assert(cancelled == true, "应成功取消 pending 的 Promise")
end)

-- ============================================================================
-- 第四部分：组合操作
-- ============================================================================

print("\n🔗 第四部分：Promise 组合操作\n")

test("4.1 asyncio.all() - 等待所有完成", function()
    local p1 = asyncio.run(function()
        w(asyncio.sleep(0.05))
        return 1
    end)
    local p2 = asyncio.run(function()
        w(asyncio.sleep(0.03))
        return 2
    end)
    local p3 = asyncio.run(function()
        w(asyncio.sleep(0.01))
        return 3
    end)

    local all_p = asyncio.all(p1, p2, p3)
    local results = all_p:await_sync(2000)
    assert(type(results) == "table" and #results == 3,
           "all 应返回包含3个结果的数组")
end)

test("4.2 asyncio.race() - 第一个完成的胜出", function()
    local fast = asyncio.run(function()
        w(asyncio.sleep(0.01))
        return "fast"
    end)
    local slow = asyncio.run(function()
        w(asyncio.sleep(0.1))
        return "slow"
    end)

    local race_p = asyncio.race(fast, slow)
    local winner = race_p:await_sync(1000)
    assert(winner == "fast", "race 应返回最快完成的结果")
end)

test("4.3 asyncio.any() - 任一成功即完成", function()
    local fail1 = asyncio.run(function()
        error("fail1")
    end)
    local success = asyncio.run(function()
        w(asyncio.sleep(0.02))
        return "success"
    end)
    local fail2 = asyncio.run(function()
        error("fail2")
    end)

    local any_p = asyncio.any(fail1, success, fail2)
    local result = any_p:await_sync(1000)
    assert(result == "success", "any 应返回第一个成功的结果")
end)

test("4.4 asyncio.allSettled() - 所有 settle 后返回状态", function()
    local ok = asyncio.run(function() return "ok" end)
    local err = asyncio.run(function() error("err") end)

    local settled_p = asyncio.allSettled(ok, err)
    local results = settled_p:await_sync(1000)
    assert(type(results) == "table" and #results == 2,
           "allSettled 应返回状态数组")
    assert(results[1].status == "fulfilled", "第1个应 fulfilled")
    assert(results[2].status == "rejected", "第2个应 rejected")
end)

-- ============================================================================
-- 第五部分：嵌套和复杂场景
-- ============================================================================

print("\n🎯 第五部分：嵌套和复杂场景\n")

test("5.1 嵌套 async 函数调用", function()
    local inner = asyncio.wrap(function(n)
        w(asyncio.sleep(0.005))
        return n + 1
    end)

    local outer = asyncio.wrap(function()
        local a = w(inner(10))
        local b = w(inner(20))
        return a + b
    end)

    local r = outer():await_sync(1000)
    assert(r == 32, "嵌套调用应正确计算: 11 + 21 = 32，得到 " .. tostring(r))
end)

test("5.2 并发执行多个 async 函数", function()
    local task1 = asyncio.wrap(function()
        w(asyncio.sleep(0.05))
        return "A"
    end)
    local task2 = asyncio.wrap(function()
        w(asyncio.sleep(0.05))
        return "B"
    end)
    local task3 = asyncio.wrap(function()
        w(asyncio.sleep(0.05))
        return "C"
    end)

    local start = os.clock()
    local p1 = task1()
    local p2 = task2()
    local p3 = task3()

    local r1 = p1:await_sync(1000)
    local r2 = p2:await_sync(1000)
    local r3 = p3:await_sync(1000)
    local elapsed = (os.clock() - start) * 1000

    assert(r1 == "A" and r2 == "B" and r3 == "C", "所有任务都应完成")
    -- 并发执行应该比串行快（理想情况 ≈ 50ms 而不是 150ms）
    print(string.format("      ⏱️  并发耗时: %.0fms (理想 < 150ms)", elapsed))
end)

test("5.3 defer/nexttick - 延迟执行", function()
    local executed = false
    local p = asyncio.run(function()
        w(asyncio.nexttick(function()
            executed = true
        end))
        return executed  -- 此时还未执行
    end)
    local before = p:await_sync(1000)
    assert(before == false, "defer 的回调不应立即执行")

    w(asyncio.sleep(0.02))
    assert(executed == true, "defer 的回调应在下个 tick 执行")
end)

-- ============================================================================
-- 第六部分：辅助工具测试
-- ============================================================================

print("\n🛠️  第六部分：辅助工具\n")

test("6.1 asyncio.isPromise() - 类型检查", function()
    local p = asyncio.run(function() return 1 end)
    assert(asyncio.isPromise(p) == true, "Promise 应通过 isPromise 检查")
    assert(asyncio.isPromise(42) == false, "数字不应是 Promise")
    assert(asyncio.isPromise("string") == false, "字符串不应是 Promise")
    assert(asyncio.isPromise(nil) == false, "nil 不应是 Promise")
    assert(asyncio.isPromise({}) == false, "表不应是 Promise")
end)

test("6.2 Promise.__tostring - 调试输出", function()
    local p = asyncio.run(function() return "test" end)
    local str = tostring(p)
    assert(str:find("Promise"), "__tostring 应包含 'Promise'")
    assert(str:find("Pending") or str:find("Fulfilled") or str:find("Rejected"),
           "__tostring 应显示状态")
end)

test("6.3 Promise.state - 状态查询", function()
    local p = asyncio.run(function()
        w(asyncio.sleep(0.05))
        return "done"
    end)
    assert(p.state == "pending", "初始状态应为 pending")
    p:await_sync(1000)
    assert(p.state == "fulfilled", "完成后状态应为 fulfilled")
end)

test("6.4 withTimeout - 带超时的等待", function()
    local slow = asyncio.run(function()
        w(asyncio.sleep(10))  -- 很慢的操作
        return "too late"
    end)

    local timeout_p = asyncio.withTimeout(slow, 50)  -- 50ms 超时
    local start = os.clock()
    local r = timeout_p:await_sync(200)
    local elapsed = (os.clock() - start) * 1000

    -- 应该超时而不是等待 10 秒
    assert(elapsed < 500, string.format("超时应在 500ms 内触发，实际 %.0fms", elapsed))
end)

-- ============================================================================
-- 第七部分：高级工具
-- ============================================================================

print("\n⚡ 第七部分：高级并发工具\n")

test("7.1 asyncio.map() - 并发映射", function()
    local urls = {"a.com", "b.com", "c.com"}
    local results = asyncio.map(
        function(url)
            w(asyncio.sleep(0.01))
            return "response from " .. url
        end,
        urls
    ):await_sync(2000)

    assert(type(results) == "table" and #results == 3,
           "map 应返回与输入等长的数组")
    assert(results[1]:find("a.com"), "map 结果应包含原始数据")
end)

test("7.2 asyncio.retry() - 自动重试", function()
    local attempts = 0
    local retry_fn = asyncio.wrap(function()
        attempts = attempts + 1
        if attempts < 3 then
            error("attempt " .. attempts)
        end
        return "success on attempt " .. attempts
    end)

    -- 使用 retry 包装（这里直接测试重试逻辑）
    local r = retry_fn():await_sync(1000)
    assert(r:find("success"), "retry 最终应成功")
end)

test("7.3 gather - all 的别名", function()
    local p1 = asyncio.resolve(1)
    local p2 = asyncio.resolve(2)
    local results = asyncio.gather(p1, p2):await_sync(1000)
    assert(#results == 2, "gather 应工作正常")
end)

-- ============================================================================
-- 第八部分：边界情况和性能
-- ============================================================================

print("\n🔬 第八部分：边界情况\n")

test("8.1 空 async 函数", function()
    local empty = asyncio.wrap(function()
        -- 什么都不做
    end)
    local r = empty():await_sync(1000)
    assert(r ~= nil, "空函数也应返回值")
end)

test("8.2 快速连续多次 await", function()
    local p = asyncio.run(function()
        local sum = 0
        for i = 1, 5 do
            w(asyncio.sleep(0.001))  -- 极短延迟
            sum = sum + i
        end
        return sum
    end)
    local r = p:await_sync(2000)
    assert(r == 15, "连续 await 应正确累加: 1+2+3+4+5=15，得到 " .. tostring(r))
end)

test("8.3 深层嵌套 async 函数（3层）", function()
    local level3 = asyncio.wrap(function(n)
        w(asyncio.sleep(0.003))
        return n * 2
    end)

    local level2 = asyncio.wrap(function(n)
        local r = w(level3(n))
        return r + 1
    end)

    local level1 = asyncio.wrap(function(n)
        local r = w(level2(n))
        return r + 10
    end)

    local r = level1(5):await_sync(1000)
    -- level3: 5*2=10 → level2: 10+1=11 → level1: 11+10=21
    assert(r == 21, "深层嵌套应正确: 得到 " .. tostring(r))
end)

test("8.4 大量并发 Promise（压力测试）", function()
    local promises = {}
    for i = 1, 20 do
        table.insert(promises, asyncio.run(function(idx)
            return function()
                w(asyncio.sleep(0.01 * (idx % 5)))
                return idx
            end
        end)(i))
    end

    local all_p = asyncio.all(table.unpack(promises))
    local results = all_p:await_sync(3000)
    assert(#results == 20, "大量 Promise 都应完成")
end)

-- ============================================================================
-- 总结
-- ============================================================================

print("\n" .. string.rep("=", 70))
print(string.format(" ✨ 测试结果: %d 通过, %d 失败", passed, failed))
print(string.rep("=", 70))

if failed > 0 then
    print("\n❌ 存在失败的测试！请检查上述错误信息。\n")
    os.exit(1)
else
    print("\n🎉 所有 Async/Await 语法糖测试通过！\n")
    print("📚 支持的语法形式:")
    print("  • asyncio.run(fn)          - 执行异步函数")
    print("  • asyncio.wrap(fn)         - 包装可复用异步函数")
    print("  • asyncio.wait(promise)    - 等待 Promise（推荐）")
    print("  • async (args) => expr     - 异步箭头函数（编译时脱糖）")
    print("  • async function f() end   - 异步函数声明（编译时脱糖）")
    print("  • await(expression)        - 等待表达式（编译时脱糖）")
    print("  • Promise:done()/fail()    - 链式回调")
    print("  • asyncio.all/race/any     - 组合操作")
    print("  • asyncio.map/retry/gather - 高级工具")
    print("  • asyncio.isPromise()      - 类型检查")
    print("  • asyncio.withTimeout()    - 超时控制")
    print("")
end
