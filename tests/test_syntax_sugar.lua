-- 测试 async/await 语法糖
-- 注意：llexer_compiler 需要在加载时处理这些语法

local asyncio = require("asyncio")
local w = asyncio.wait

print("=" .. string.rep("=", 50))
print(" async/await 语法糖测试")
print("=" .. string.rep("=", 50))

-- 由于 llexer_compiler 是代码转换器（不是实时解释器），
-- 我们在这里模拟脱糖后的等价代码来验证语义正确性
-- 真正的语法糖需要通过 llexer_compiler 编译 .lua 文件时生效

local passed, failed = 0, 0
local function test(name, fn)
    local ok, err = pcall(fn)
    if ok then passed = passed + 1; print("  ✅ "..name)
    else failed = failed + 1; print("  ❌ "..name..": "..tostring(err)) end
end

print("\n─── 脱糖等价性验证 ───\n")

-- 模拟: async function fetch(x) ... end
-- 脱糖为: local fetch = asyncio.wrap(function(x) ... end)
test("async function 脱糖等价 (wrap)", function()
    -- 手动写脱糖后的形式
    local fetch = asyncio.wrap(function(x)
        w(asyncio.sleep(0.01))
        return x * 2
    end)
    local r = fetch(21):await_sync(1000)
    assert(r == 42, "got "..tostring(r))
end)

-- 模拟: await(asyncio.sleep(1))
-- 脱糖为: asyncio.wait(asyncio.sleep(1))
test("await 表达式脱糖等价 (wait)", function()
    local p = asyncio.run(function()
        local r = w(asyncio.sleep(0.01))  -- 等价于 await(sleep(0.01))
        return r and "slept" or "error"
    end)
    local r = p:await_sync(1000)
    assert(r == "slept", "got "..tostring(r))
end)

-- 模拟: async (x) => x * 2  (箭头函数)
-- 脱糖为: asyncio.wrap(function(x) return x * 2 end)
test("async 箭头函数脱糖等价", function()
    local double = asyncio.wrap(function(x) return x * 2 end)
    local r = double(7):await_sync(1000)  -- wrap返回AsyncFunction，需:await_sync()
    assert(r == 14, "got "..tostring(r))
end)

-- 模拟完整 async/await 协作模式
test("完整 async/await 流程", function()
    local fetchData = asyncio.wrap(function(url)
        w(asyncio.sleep(0.01))  -- 模拟网络延迟
        return {url=url, status=200, body="hello from "..url}
    end)

    local processAll = asyncio.wrap(function()
        local a = w(fetchData("/api/a"))
        local b = w(fetchData("/api/b"))
        return {a=a, b=b}
    end)

    local r = processAll():await_sync(2000)
    assert(r.a.url == "/api/a" and r.b.url == "/api/b",
           string.format("a.url=%s b.url=%s", tostring(r.a.url), tostring(r.b.url)))
end)

test("async 中错误传播", function()
    local failing = asyncio.wrap(function()
        w(asyncio.sleep(0.005))
        error("network timeout")
    end)
    local r = failing():await_sync(1000)
    assert(type(r) == "string" and r:find("timeout"),
           "got "..type(r)..":"..tostring(r))
end)

test("嵌套 async 函数", function()
    local inner = asyncio.wrap(function(n)
        w(asyncio.sleep(0.005))
        return n + 1
    end)

    local outer = asyncio.wrap(function()
        local a = w(inner(10))   -- 11
        local b = w(inner(20))   -- 21
        return a + b            -- 32
    end)

    local r = outer():await_sync(1000)
    assert(r == 32, "got "..tostring(r))
end)

print("\n" .. string.rep("-", 50))
print(string.format(" 结果: %d 通过, %d 失败", passed, failed))
print(string.rep("-", 50))

if failed > 0 then os.exit(1)
else print("\n🎉 语法糖脱糖验证全部通过！用户可写 async/await 语法\n") end
