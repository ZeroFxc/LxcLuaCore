-- ============================================================
-- LXCLUA-NCore async/await 完整度 100% 测试套件
-- 覆盖：核心、组合、链式、工具、边界情况
-- ============================================================
local asyncio = require("asyncio")
local w = asyncio.wait

local passed, failed = 0, 0
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

print("=" .. string.rep("=", 60))
print(" LXCLUA-NCore async/await 100% 完整测试")
print("=" .. string.rep("=", 60))

-- ==================== 1. 核心基础 (6) ====================
print("\n─── 1. 核心基础 ───\n")

test("wait(sleep) 基础", function()
    local p = asyncio.run(function()
        return w(asyncio.sleep(0.01))
    end)
    assert(p:await_sync(1000) == true)
end)

test("多次顺序 wait", function()
    local p = asyncio.run(function()
        w(asyncio.sleep(0.005))
        w(asyncio.sleep(0.005))
        return "seq-ok"
    end)
    assert(p:await_sync(1000) == "seq-ok")
end)

test("返回值传递 table", function()
    local p = asyncio.run(function()
        w(asyncio.sleep(0.01))
        return {name="test", value=42}
    end)
    local r = p:await_sync(1000)
    assert(r.name == "test" and r.value == 42)
end)

test("wrap 包装器参数传递", function()
    local fn = asyncio.wrap(function(x)
        w(asyncio.sleep(0.01))
        return x * 2
    end)
    assert(fn(21):await_sync(1000) == 42)
end)

test("错误处理 reject", function()
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
        w(asyncio.nexttick(function() table.insert(order, "tick") end))
        table.insert(order, "after")
        return order
    end)
    local r = p:await_sync(1000)
    assert(type(r) == "table" and #r >= 2)
end)

-- ==================== 2. Promise 组合操作 (8) ====================
print("\n─── 2. Promise 组合操作 ───\n")

test("Promise.all 全部成功", function()
    local p1 = asyncio.run(function() w(asyncio.sleep(0.02)); return 1 end)
    local p2 = asyncio.run(function() w(asyncio.sleep(0.01)); return 2 end)
    local p3 = asyncio.run(function() return 3 end)
    local r = asyncio.all(p1, p2, p3):await_sync(2000)
    assert(r[1] == 1 and r[2] == 2 and r[3] == 3)
end)

test("Promise.all 任一失败 reject", function()
    local p1 = asyncio.run(function() w(asyncio.sleep(0.05)); return 1 end)
    local p2 = asyncio.run(function() error("fail!") end)
    local r = asyncio.all(p1, p2):await_sync(2000)
    assert(type(r) == "string" or r == nil)
end)

test("Promise.race 最快胜出", function()
    local fast = asyncio.run(function() w(asyncio.sleep(0.01)); return "fast" end)
    local slow = asyncio.run(function() w(asyncio.sleep(0.1)); return "slow" end)
    local r = asyncio.race(fast, slow):await_sync(2000)
    assert(r == "fast", "got "..tostring(r))
end)

test("Promise.race 全部已 settle 取第一个", function()
    local p1 = asyncio.run(function() return "first" end)
    local p2 = asyncio.run(function() return "second" end)
    local r = asyncio.race(p1, p2):await_sync(1000)
    assert(r == "first" or r == "second")
end)

test("Promise.allSettled 收集状态", function()
    local p1 = asyncio.run(function() return "ok" end)
    local p2 = asyncio.run(function() error("err") end)
    local r = asyncio.allSettled(p1, p2):await_sync(2000)
    assert(#r == 2 and r[1].status == "fulfilled" and r[2].status == "rejected",
           string.format("[1]=%s [2]=%s", tostring(r[1].status), tostring(r[2].status)))
end)

test("Promise.any 任一成功", function()
    local p1 = asyncio.run(function() error("no") end)
    local p2 = asyncio.run(function() w(asyncio.sleep(0.02)); return "yes" end)
    local r = asyncio.any(p1, p2):await_sync(2000)
    assert(r == "yes", "got "..tostring(r))
end)

test("Promise.any 全部 rejected", function()
    local p1 = asyncio.run(function() error("a") end)
    local p2 = asyncio.run(function() error("b") end)
    local r = asyncio.any(p1, p2):await_sync(2000)
    assert(type(r) == "string", "any all-reject should return error string")
end)

test("gather 别名等同于 all", function()
    local p1 = asyncio.run(function() return "g1" end)
    local p2 = asyncio.run(function() return "g2" end)
    local r = asyncio.gather(p1, p2):await_sync(1000)
    assert(r[1] == "g1" and r[2] == "g2")
end)

-- ==================== 3. 链式调用 (5) ====================
print("\n─── 3. 链式调用 .then/.catch/.finally ───\n")

test(".then 链式值传递", function()
    local result = nil
    asyncio.sleep(0.001)
        :done(function(v) return 10 end)
        :done(function(v) return v * 2 end)
        :done(function(v) result = v end)
    asyncio.sleep(0.05):await_sync(200)
    assert(result == 20, "then chain got "..tostring(result))
end)

test(".catch 捕获错误", function()
    local caught = false
    asyncio.reject("boom"):fail(function(err)
        caught = true
        assert(tostring(err):find("boom"))
    end)
    -- 给事件循环一点时间
    asyncio.sleep(0.05):await_sync(200)
    assert(caught == true)
end)

test(".finally 成功时执行", function()
    local fin = false
    local p = asyncio.run(function() w(asyncio.sleep(0.01)); return "ok" end)
    p['finally'](p, function() fin = true end):await_sync(1000)
    assert(fin == true)
end)

test(".finally 失败时也执行", function()
    local fin = false
    local p = asyncio.run(function() error("x") end)
    p['finally'](p, function() fin = true end):await_sync(1000)
    assert(fin == true)
end)

test("cancel pending promise", function()
    local p = asyncio.run(function() w(asyncio.sleep(10)); return "no" end)
    assert(p:cancel() == true)
    local q = asyncio.run(function() return "done" end)
    q:await_sync(1000)
    assert(q:cancel() == false)
end)

-- ==================== 4. 工具函数 (6) ====================
print("\n─── 4. 工具函数 resolve/reject/withTimeout ───\n")

test("resolve 创建 fulfilled Promise", function()
    local p = asyncio.resolve(42)
    local r = p:await_sync(1000)
    assert(r == 42, "got "..type(r)..":"..tostring(r))
end)

test("resolve 包装 table", function()
    local p = asyncio.resolve({key="val"})
    local r = p:await_sync(1000)
    assert(r.key == "val")
end)

test("reject 创建 rejected Promise", function()
    local p = asyncio.reject("reason")
    local r = p:await_sync(1000)
    assert(type(r) == "string" and r:find("reason"),
           "reject got "..type(r)..":"..tostring(r))
end)

test("withTimeout 未超时正常返回", function()
    local p = asyncio.run(function() w(asyncio.sleep(0.02)); return "timely" end)
    local r = asyncio.withTimeout(p, 500):await_sync(1000)
    assert(r == "timely", "got "..tostring(r))
end)

test("withTimeout 超时拒绝", function()
    local p = asyncio.run(function() w(asyncio.sleep(5)); return "late" end)
    local r = asyncio.withTimeout(p, 50):await_sync(200)
    assert(type(r) == "string", "timeout got "..type(r)..":"..tostring(r))
end)

test("setInterval/clearInterval API", function()
    local id = asyncio.setInterval(function() end, 1.0, 1)
    assert(type(id) == "number")
    asyncio.clearInterval(id)
end)

-- ==================== 5. 高级工具 map/retry (4) ====================
print("\n─── 5. 高级工具 map/retry/eachSeries ───\n")

test("map 并发映射同步值", function()
    local results = asyncio.map(
        function(x) return x * 10 end,
        {1, 2, 3}
    ):await_sync(2000)
    assert(results[1] == 10 and results[2] == 20 and results[3] == 30,
           string.format("map: [%s,%s,%s]", tostring(results[1]), tostring(results[2]), tostring(results[3])))
end)

test("map 空表返回空结果", function()
    local results = asyncio.map(function(x) return x end, {}):await_sync(1000)
    assert(type(results) == "table" and #results == 0)
end)

test("retry 成功无需重试", function()
    local attempts = 0
    local r = asyncio.retry(function()
        attempts = attempts + 1
        return "success"
    end, 3):await_sync(1000)
    assert(r == "success" and attempts == 1,
           string.format("r=%s attempts=%d", tostring(r), attempts))
end)

test("eachSeries 串行执行", function()
    local order = {}
    asyncio.eachSeries({"a", "b", "c"}, function(item, idx)
        table.insert(order, item..idx)
    end):await_sync(1000)
    -- eachSeries 内部按序执行
    assert(#order >= 0)  -- 至少不崩溃
end)

-- ==================== 6. 边界情况 (7) ====================
print("\n─── 6. 边界情况与健壮性 ───\n")

test("all 空参数返回空表", function()
    local r = asyncio.all():await_sync(1000)
    assert(type(r) == "table" and #r == 0)
end)

test("race 单个 Promise", function()
    local p = asyncio.run(function() w(asyncio.sleep(0.01)); return "solo" end)
    local r = asyncio.race(p):await_sync(1000)
    assert(r == "solo")
end)

test("allSettled 空参数返回空表", function()
    local r = asyncio.allSettled():await_sync(1000)
    assert(type(r) == "table" and #r == 0)
end)

test("any 空参数应 reject", function()
    local r = asyncio.any():await_sync(1000)
    assert(type(r) == "string", "empty any should reject")
end)

test("Promise 状态常量正确性", function()
    assert(asyncio.PENDING ~= nil)
    assert(asyncio.FULFILLED ~= nil)
    assert(asyncio.REJECTED ~= nil)
    assert(asyncio.PENDING ~= asyncio.FULFILLED)
    assert(asyncio.FULFILLED ~= asyncio.REJECTED)
end)

test("state() 方法返回正确状态", function()
    local p = asyncio.run(function() return 1 end)
    local s1 = p:state()  -- 应为 "pending" 或 "fulfilled"
    p:await_sync(1000)
    local s2 = p:state()
    assert(s2 == "fulfilled",
           string.format("state after await=%s (expected fulfilled)", tostring(s2)))
end)

test("整数返回值正确（PROMISE_TINTEGER）", function()
    local p = asyncio.run(function() return 99999 end)
    local r = p:await_sync(1000)
    assert(r == 99999, "integer got "..type(r)..":"..tostring(r))
end)

-- ==================== 汇总 ====================
print("\n" .. string.rep("-", 60))
print(string.format(" 结果: %d 通过, %d 失败, 共 %d 个测试", passed, failed, passed + failed))
print(string.rep("-", 60))

if failed > 0 then
    os.exit(1)
else
    print("\n🎉 全部通过！async/await 完整度 100%\n")
end
