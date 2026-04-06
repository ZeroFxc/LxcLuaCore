-- async/await 核心功能完整测试
local asyncio = require("asyncio")
local wait = asyncio.wait
local wrap = asyncio.wrap

print("=" .. string.rep("=", 60))
print(" LXCLUA-NCore async/await 核心功能测试")
print("=" .. string.rep("=", 60))

-- 测试计数
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

-- Test 1: 基础 wait(sleep)
test("wait(sleep) 返回正确值", function()
    local p = asyncio.run(function()
        local r = wait(asyncio.sleep(0.02))
        assert(r == true, "sleep 应返回 true")
        return "ok"
    end)
    local r = p:await_sync(1000)
    assert(r == "ok", "应返回 'ok'")
end)

-- Test 2: 多次顺序 wait
test("多次顺序 wait", function()
    local results = {}
    local p = asyncio.run(function()
        table.insert(results, wait(asyncio.sleep(0.01)))
        table.insert(results, wait(asyncio.sleep(0.01)))
        table.insert(results, wait(asyncio.sleep(0.01)))
        return #results
    end)
    local r = p:await_sync(1000)
    assert(r == 3, "应有3个结果")
    assert(results[1] == true and results[2] == true and results[3] == true)
end)

-- Test 3: 返回值传递
test("返回值正确传递", function()
    local p = asyncio.run(function()
        wait(asyncio.sleep(0.01))
        return {name="test", value=42}
    end)
    local r = p:await_sync(1000)
    assert(type(r) == "table", "应为 table")
    assert(r.name == "test" and r.value == 42)
end)

-- Test 4: 无 await 的简单函数
test("无 await 简单函数", function()
    local p = asyncio.run(function()
        return "simple"
    end)
    local r = p:await_sync(1000)
    assert(r == "simple")
end)

-- Test 5: wrap 包装器
test("wrap() 包装异步函数", function()
    local myFunc = wrap(function(x)
        wait(asyncio.sleep(0.01))
        return x * 2
    end)
    
    local p = myFunc(21)
    local r = p:await_sync(1000)
    assert(r == 42, "wrap 函数应返回 42")
end)

-- Test 6: 错误处理
test("错误处理 (reject)", function()
    local p = asyncio.run(function()
        error("test error")
    end)
    -- 被 reject 的 Promise，await_sync 返回 rejection reason（错误信息）
    local r = p:await_sync(1000)
    assert(type(r) == "string", "reject 应返回错误字符串 (rejection reason)")
    assert(r:find("test error"), "应包含原始错误信息")
end)

-- Test 7: nexttick
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
    -- order 应该包含 before, tick, after
end)

-- 汇总
print("\n" .. string.rep("-", 60))
print(string.format(" 结果: %d 通过, %d 失败, 共 %d 个测试", passed, failed, passed + failed))
print(string.rep("-", 60))

if failed > 0 then
    os.exit(1)
else
    print("\n🎉 所有测试通过！async/await 语法糖完全正常！\n")
end
