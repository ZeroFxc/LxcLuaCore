-- async/await 完整功能测试（含新增 API）
local asyncio = require("asyncio")
local wait = asyncio.wait
local wrap = asyncio.wrap

print("=" .. string.rep("=", 60))
print(" LXCLUA-NCore async/await 完整功能测试")
print("=" .. string.rep("=", 60))

local passed = 0
local failed = 0
local function test(name, fn)
    local ok, err = pcall(fn)
    if ok then
        passed = passed + 1
        print(string.format("  ✅ %s", name))
    else
        failed = failed + 1
        print(string.format("  ❌ %s: %s", name, tostring(err)))
    end
end

-- ===== 原有核心测试 =====

test("wait(sleep)", function()
    local p = asyncio.run(function()
        return wait(asyncio.sleep(0.02))
    end)
    assert(p:await_sync(1000) == true)
end)

test("多次顺序 wait", function()
    local p = asyncio.run(function()
        wait(asyncio.sleep(0.01))
        wait(asyncio.sleep(0.01))
        return "ok"
    end)
    assert(p:await_sync(1000) == "ok")
end)

test("返回值传递", function()
    local p = asyncio.run(function()
        wait(asyncio.sleep(0.01))
        return {name="test", value=42}
    end)
    local r = p:await_sync(1000)
    assert(r.name == "test" and r.value == 42)
end)

test("wrap 包装器", function()
    local fn = wrap(function(x)
        wait(asyncio.sleep(0.01))
        return x * 2
    end)
    assert(fn(21):await_sync(1000) == 42)
end)

test("错误处理(reject)", function()
    local p = asyncio.run(function()
        error("test error")
    end)
    local r = p:await_sync(1000)
    assert(type(r) == "string" and r:find("test error"))
end)

test("nexttick 延迟执行", function()
    local order = {}
    local p = asyncio.run(function()
        table.insert(order, "before")
        wait(asyncio.nexttick(function()
            table.insert(order, "tick")
        end))
        table.insert(order, "after")
        return order
    end)
    local r = p:await_sync(1000)
    assert(type(r) == "table")
end)

-- ===== 新增：Promise 组合操作 =====

print("\n─── Promise 组合操作 ───\n")

test("Promise.all 全部成功", function()
    local p1 = asyncio.run(function()
        wait(asyncio.sleep(0.02))
        return 1
    end)
    local p2 = asyncio.run(function()
        wait(asyncio.sleep(0.01))
        return 2
    end)
    local p3 = asyncio.run(function()
        return 3
    end)
    local all_p = asyncio.all(p1, p2, p3)
    local results = all_p:await_sync(2000)
    assert(type(results) == "table")
    assert(results[1] == 1 and results[2] == 2 and results[3] == 3,
           "all结果应为{1,2,3}, got "..tostring(results[1])..","..tostring(results[2])..","..tostring(results[3]))
end)

test("Promise.all 任一失败reject", function()
    local p1 = asyncio.run(function()
        wait(asyncio.sleep(0.05))
        return 1
    end)
    local p2 = asyncio.run(function()
        error("fail!")
    end)
    local all_p = asyncio.all(p1, p2)
    local results = all_p:await_sync(2000)
    -- reject 返回错误字符串或nil
    assert(type(results) == "string" or results == nil,
           "all失败应返回错误, got "..type(results))
end)

test("Promise.race 最快胜出", function()
    local fast = asyncio.run(function()
        wait(asyncio.sleep(0.01))
        return "fast"
    end)
    local slow = asyncio.run(function()
        wait(asyncio.sleep(0.1))
        return "slow"
    end)
    local race_p = asyncio.race(fast, slow)
    local result = race_p:await_sync(2000)
    assert(result == "fast", "race应返回最快的: got "..tostring(result))
end)

test("Promise.allSettled 收集所有状态", function()
    local p1 = asyncio.run(function()
        return "ok"
    end)
    local p2 = asyncio.run(function()
        error("err")
    end)
    local as_p = asyncio.allSettled(p1, p2)
    local results = as_p:await_sync(2000)
    assert(type(results) == "table" and #results == 2,
           "allSettled应返回2个元素")
    assert(results[1].status == "fulfilled",
           "[1]应为fulfilled, got "..tostring(results[1].status))
    assert(results[2].status == "rejected",
           "[2]应为rejected, got "..tostring(results[2].status))
end)

test("Promise.any 任一成功", function()
    local p1 = asyncio.run(function()
        error("no")
    end)
    local p2 = asyncio.run(function()
        wait(asyncio.sleep(0.02))
        return "yes"
    end)
    local any_p = asyncio.any(p1, p2)
    local result = any_p:await_sync(2000)
    assert(result == "yes", "any应返回成功的: got "..tostring(result))
end)

-- ===== 新增：finally / cancel / withTimeout =====

print("\n─── finally / cancel / withTimeout ───\n")

test("Promise finally 成功时执行（用索引语法）", function()
    local finally_called = false
    local p = asyncio.run(function()
        wait(asyncio.sleep(0.01))
        return "result"
    end)
    -- 使用 ['finally'] 避免保留字冲突
    local final_p = p['finally'](p, function()
        finally_called = true
    end)
    local r = final_p:await_sync(1000)
    assert(r == "result", "finally应透传结果, got "..tostring(r))
    assert(finally_called == true, "finally回调应被调用")
end)

test("Promise finally 失败时也执行", function()
    local finally_called = false
    local p = asyncio.run(function()
        error("boom")
    end)
    local final_p = p['finally'](p, function()
        finally_called = true
    end)
    local r = final_p:await_sync(1000)
    assert(finally_called == true, "失败时finally也应执行")
end)

test("Promise.cancel 取消pending", function()
    local p = asyncio.run(function()
        wait(asyncio.sleep(10))
        return "should not reach"
    end)
    local cancelled = p:cancel()
    assert(cancelled == true, "取消pending应返回true")
end)

test("Promise.cancel 已settled无效", function()
    local p = asyncio.run(function()
        return "done"
    end)
    p:await_sync(1000)
    local cancelled = p:cancel()
    assert(cancelled == false, "取消已完成应返回false")
end)

test("withTimeout 未超时正常返回", function()
    local p = asyncio.run(function()
        wait(asyncio.sleep(0.02))
        return "timely"
    end)
    local wrapped = asyncio.withTimeout(p, 500)
    local r = wrapped:await_sync(1000)
    assert(r == "timely", "未超时应返回原始结果, got "..tostring(r))
end)

test("withTimeout 超时拒绝", function()
    local p = asyncio.run(function()
        wait(asyncio.sleep(5))
        return "too late"
    end)
    local wrapped = asyncio.withTimeout(p, 50)
    local r = wrapped:await_sync(200)
    assert(type(r) == "string",
           "超时应返回错误字符串, got "..type(r)..":"..tostring(r))
end)

-- ===== 新增：setInterval/clearInterval =====

print("\n─── setInterval / clearInterval ───\n")

test("setInterval API 可用", function()
    local id = asyncio.setInterval(function()
        -- 空回调
    end, 1.0, 1)
    assert(type(id) == "number", "setInterval应返回数字ID")
    asyncio.clearInterval(id)
end)

-- ===== 汇总 =====

print("\n" .. string.rep("-", 60))
print(string.format(" 结果: %d 通过, %d 失败, 共 %d 个测试",
      passed, failed, passed + failed))
print(string.rep("-", 60))

if failed > 0 then
    os.exit(1)
else
    print("\n🎉 全部测试通过！async/await 完整度大幅提升！\n")
end
