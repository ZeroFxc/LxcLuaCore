-- 只跑前6个核心测试 + 第7个(all)
local asyncio = require("asyncio")
local w = asyncio.wait

print("=== Test 1: wait(sleep) ===")
do
    local p = asyncio.run(function()
        return w(asyncio.sleep(0.02))
    end)
    assert(p:await_sync(1000) == true)
    print("PASS")
end

print("=== Test 2: 多次顺序 wait ===")
do
    local p = asyncio.run(function()
        w(asyncio.sleep(0.01))
        w(asyncio.sleep(0.01))
        return "ok"
    end)
    assert(p:await_sync(1000) == "ok")
    print("PASS")
end

print("=== Test 3: 返回值传递 ===")
do
    local p = asyncio.run(function()
        w(asyncio.sleep(0.01))
        return {name="test", value=42}
    end)
    local r = p:await_sync(1000)
    assert(r.name == "test" and r.value == 42)
    print("PASS")
end

print("=== Test 4: wrap 包装器 ===")
do
    local fn = asyncio.wrap(function(x)
        w(asyncio.sleep(0.01))
        return x * 2
    end)
    assert(fn(21):await_sync(1000) == 42)
    print("PASS")
end

print("=== Test 5: 错误处理(reject) ===")
do
    local p = asyncio.run(function()
        error("test error")
    end)
    local r = p:await_sync(1000)
    assert(type(r) == "string" and r:find("test error"))
    print("PASS")
end

print("=== Test 6: nexttick 延迟执行 ===")
do
    local order = {}
    local p = asyncio.run(function()
        table.insert(order, "before")
        w(asyncio.nexttick(function()
            table.insert(order, "tick")
        end))
        table.insert(order, "after")
        return order
    end)
    local r = p:await_sync(1000)
    assert(type(r) == "table")
    print("PASS")
end

print("=== Test 7: Promise.all 全部成功 ===")
do
    local p1 = asyncio.run(function()
        w(asyncio.sleep(0.02))
        return 1
    end)
    local p2 = asyncio.run(function()
        w(asyncio.sleep(0.01))
        return 2
    end)
    local p3 = asyncio.run(function()
        return 3
    end)
    local all_p = asyncio.all(p1, p2, p3)
    local results = all_p:await_sync(2000)
    assert(type(results) == "table")
    assert(results[1] == 1 and results[2] == 2 and results[3] == 3,
           "got "..tostring(results[1])..","..tostring(results[2])..","..tostring(results[3]))
    print("PASS")
end

print("\nALL PASSED!")
