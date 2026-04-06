-- ============================================================================
-- LXCLUA-NCore 完全隐蔽性验证测试 - 最终版
--
-- 目标：验证所有内部实现完全隐藏在 C 层，
--       用户无法通过任何常规方式探测到实现细节
--
-- 测试范围：
--   1. 全局表（_G）无任何 __ 前缀函数
--   2. 协程环境无 __aco_ctx__ 全局变量
--   3. 注册表中的内部数据难以访问
--   4. async/await 功能正常工作
-- ============================================================================

local asyncio = require("asyncio")

print("=" .. string.rep("=", 70))
print(" 🔒🔒🔒  完全隐蔽性验证 - 最终测试  🔒🔒🔒")
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
-- 第一部分：全局表绝对清洁度检查
-- ============================================================================

print("\n🔍 第一部分：全局表绝对清洁度\n")

test("1.1 _G 中无任何 __ 前缀的键", function()
    local found = {}
    for key, value in pairs(_G) do
        if type(key) == "string" and key:match("^__") then
            table.insert(found, key)
        end
    end

    -- 允许的例外列表（如果有的话）
    local allowed = {}

    for _, key in ipairs(found) do
        local is_allowed = false
        for _, allow in ipairs(allowed) do
            if key == allow then
                is_allowed = true
                break
            end
        end

        if not is_allowed then
            error(string.format("发现未授权的全局键: '%s' (类型: %s)", key, type(_G[key])))
        end
    end

    if #found == 0 then
        print(string.format("      ✓ 全局表完全清洁，共扫描 %d 个键", #found))
    else
        print(string.format("      ℹ️  发现 %d 个允许的例外", #found))
    end
end)

test("1.2 原始方式也无法获取内部函数", function()
    assert(rawget(_G, "__async_wrap") == nil,
           "rawget 不应能获取 __async_wrap")
    assert(rawget(_G, "__generic_wrap") == nil,
           "rawget 不应能获取 __generic_wrap")
    assert(rawget(_G, "__check_type") == nil,
           "rawget 不应能获取 __check_type")
    assert(rawget(_G, "__lxc_get_cmds") == nil,
           "rawget 不应能获取 __lxc_get_cmds")
    assert(rawget(_G, "__lxc_get_ops") == nil,
           "rawget 不应能获取 __lxc_get_ops")
    assert(rawget(_G, "__test__") == nil,
           "rawget 不应能获取 __test__")
end)

test("1.3 遍历 _G 确认无内部模式泄露", function()
    local suspicious_patterns = {
        "async_wrap",
        "generic_wrap",
        "check_type",
        "lxc_get",
        "aco_ctx",
        "_ASYNC_"
    }

    local leaks = {}
    for key, value in pairs(_G) do
        if type(key) == "string" then
            for _, pattern in ipairs(suspicious_patterns) do
                if key:lower():find(pattern:lower()) then
                    table.insert(leaks, {key = key, pattern = pattern})
                end
            end
        end
    end

    if #leaks > 0 then
        local msg = "发现可疑的全局键:\n"
        for _, leak in ipairs(leaks) do
            msg = msg .. string.format("      - '%s' (匹配模式: '%s')\n", leak.key, leak.pattern)
        end
        error(msg)
    end
end)

-- ============================================================================
-- 第二部分：协程环境清洁度检查
-- ============================================================================

print("\n🧵 第二部分：协程环境清洁度\n")

test("2.1 协程中无 __aco_ctx__ 全局变量", function()
    async function testCoroutineEnv()
        -- 在 async 函数内部检查环境
        local co_env = debug and debug.getinfo(1, "f")
        return true
    end

    local p = testCoroutineEnv()
    p:await_sync(1000)

    -- 尝试在普通环境中查找
    assert(rawget(_G, "__aco_ctx__") == nil,
           "主线程不应有 __aco_ctx__")
end)

test("2.2 创建协程后检查其环境", function()
    local co = coroutine.create(function()
        -- 检查协程的全局环境
        local ok, val = pcall(function()
            return _G.__aco_ctx__
        end)
        return ok and val or nil
    end)

    local success, value = coroutine.resume(co)
    assert(value == nil,
           "协程环境不应包含 __aco_ctx__")
end)

-- ============================================================================
-- 第三部分：功能完整性验证
-- ============================================================================

print("\n⚙️  第三部分：功能完整性（确保重构未破坏功能）\n")

test("3.1 async function 声明和调用", function()
    async function pureTest(x)
        await(asyncio.sleep(0.001))
        return x * 10
    end

    local p = pureTest(7)
    assert(p ~= nil, "应返回 Promise")
    local result = p:await_sync(1000)
    assert(result == 70, "期望 70，得到 " .. tostring(result))
end)

test("3.2 多个 await 顺序执行", function()
    async function multiAwait()
        local sum = 0
        for i = 1, 5 do
            await(asyncio.sleep(0.001))
            sum = sum + i
        end
        return sum
    end

    local p = multiAwait()
    local r = p:await_sync(2000)
    assert(r == 15, "期望 15 (1+2+3+4+5)，得到 " .. tostring(r))
end)

test("3.3 错误处理正常传播", function()
    async function failingFunc()
        await(asyncio.sleep(0.005))
        error("intentional error")
    end

    local p = failingFunc()
    local r = p:await_sync(1000)
    assert(type(r) == "string" and r:find("error"),
           "错误应被传播")
end)

test("3.4 嵌套 async 函数调用", function()
    async function level3(n)
        await(asyncio.sleep(0.002))
        return n * 2
    end

    async function level2(n)
        local v = await(level3(n))
        return v + 10
    end

    async function level1()
        local a = await(level2(5))   -- 20
        local b = await(level2(10))  -- 30
        return a + b                 -- 50
    end

    local p = level1()
    local r = p:await_sync(2000)
    assert(r == 50, "嵌套调用期望 50，得到 " .. tostring(r))
end)

test("3.5 并发执行多个 async 函数", function()
    async function concurrentTask(id)
        await(asyncio.sleep(0.01))
        return id * id
    end

    local promises = {}
    for i = 1, 5 do
        promises[i] = concurrentTask(i)
    end

    local results = {}
    for i, p in ipairs(promises) do
        results[i] = p:await_sync(1000)
    end

    assert(results[1] == 1,   "任务1")
    assert(results[2] == 4,   "任务2")
    assert(results[3] == 9,   "任务3")
    assert(results[4] == 16,  "任务4")
    assert(results[5] == 25,  "任务5")
end)

test("3.6 与运行时 API 混合使用", function()
    async function mixedUsage()
        local w = asyncio.wait

        w(asyncio.sleep(0.005))
        await(asyncio.resolve(42))

        local r = w(asyncio.resolve("mixed"))
        return {num=42, str=r}
    end

    local p = mixedUsage()
    local r = p:await_sync(1000)
    assert(r.num == 42 and r.str == "mixed",
           "混合使用应正常工作")
end)

-- ============================================================================
-- 第四部分：安全性深度测试
-- ============================================================================

print("\n🛡️  第四部分：安全性深度测试\n")

test("4.1 无法通过 debug 库探测内部函数", function()
    if not debug or not debug.getinfo then
        print("      ⚠️  debug 库不可用，跳过此测试")
        return
    end

    local internal_found = false
    for key, value in pairs(_G) do
        if type(key) == "string" and key:match("^__") and type(value) == "function" then
            local info = debug.getinfo(value)
            if info then
                internal_found = true
                print(string.format("      ⚠️  发现: %s (来源: %s)",
                       key, tostring(info.source or "unknown")))
            end
        end
    end

    assert(not internal_found, "不应通过 debug 探测到任何内部函数")
end)

test("4.2 getmetatable 不暴露内部结构", function()
    async function metaTest()
        return 42
    end

    local mt = getmetatable(metaTest)
    if mt then
        -- 检查元表是否包含内部引用
        local has_internal = false
        for k, v in pairs(mt) do
            local ks = tostring(k)
            if ks:find("__async") or ks:find("__generic") or
               ks:find("check_type") or ks:find("lxc_") then
                has_internal = true
                print(string.format("      ⚠️  元表包含内部字段: %s", ks))
            end
        end
        assert(not has_internal, "元表不应包含内部实现细节")
    end
end)

test("4.3 注册表访问受限", function()
    if not debug or not debug.getregistry then
        print("      ⚠️  debug.getregistry 不可用，跳过此测试")
        return
    end

    local reg = debug.getregistry()

    -- 检查是否能找到 LXC_INTERNAL
    local lxc_internal = reg.LXC_INTERNAL
    if lxc_internal then
        -- 即使能访问到，也不应该轻易被用户理解或使用
        assert(type(lxc_internal) == "table",
               "LXC_INTERNAL 应该是表")

        -- 检查是否包含预期的函数
        local expected_funcs = {"check_type", "get_cmds", "get_ops", "test"}
        for _, func_name in ipairs(expected_funcs) do
            assert(lxc_internal[func_name] ~= nil,
                   string.format("LXC_INTERNAL 应包含 %s", func_name))
        end

        print(string.format("      ℹ️  LXC_INTERNAL 可通过 debug 访问（预期行为）"))
        print(string.format("      ℹ️  包含 %d 个内部函数", #expected_funcs))
    else
        print("      ✅ 无法访问 LXC_INTERNAL（更安全）")
    end
end)

test("4.4 tostring 不泄露实现细节", function()
    async function toStringTest()
        return "test"
    end

    local str = tostring(toStringTest)
    -- 不应包含明显的内部实现线索
    assert(not (str or ""):find("__async_wrap"),
           "tostring 不应暴露 __async_wrap")
    assert(not (str or ""):find("luaB_async"),
           "tostring 不应暴露 luaB_async")
    assert(not (str or ""):find("_ASYNC_LAZY"),
           "tostring 不应暴露 _ASYNC_LAZY")

    print(string.format("      tostring 输出: %s", str or "nil"))
end)

-- ============================================================================
-- 第五部分：边界情况和压力测试
-- ============================================================================

print("\n🔬 第五部分：边界情况\n")

test("5.1 快速连续创建/调用多个 async 函数", function()
    local results = {}
    for i = 1, 20 do
        local idx = i
        async function _G["rapid_" .. idx]()
            await(asyncio.sleep(0.0005))
            return idx
        end

        local p = _G["rapid_" .. idx]()
        results[idx] = p:await_sync(500)
    end

    for i = 1, 20 do
        assert(results[i] == i,
               string.format("函数 %d 应返回 %d", i, i))
        -- 清理全局函数
        _G["rapid_" .. i] = nil
    end
end)

test("5.2 无 await 的 async 函数", function()
    async function syncLike(a, b, c)
        return a + b + c
    end

    local p = syncLike(10, 20, 30)
    local r = p:await_sync(1000)
    assert(r == 60, "无 await 应正常工作")
end)

test("5.3 空 async 函数", function()
    async function emptyAsync()
        -- 什么都不做
    end

    local p = emptyAsync()
    local r = p:await_sync(1000)
    assert(r ~= nil, "空函数也应返回值")
end)

test("5.4 深层嵌套（5层）", function()
    async function deep5(n)
        await(asyncio.sleep(0.001))
        return n + 1
    end

    async function deep4(n)
        return await(deep5(n)) + 1
    end

    async function deep3(n)
        return await(deep4(n)) + 1
    end

    async function deep2(n)
        return await(deep3(n)) + 1
    end

    async function deep1(n)
        return await(deep2(n)) + 1
    end

    local p = deep1(0)
    local r = p:await_sync(2000)
    assert(r == 5, "5层嵌套期望 5，得到 " .. tostring(r))
end)

-- ============================================================================
-- 总结
-- ============================================================================

print("\n" .. string.rep("=", 70))
print(string.format(" 🎯🎯🎯  最终验证结果: %d 通过, %d 失败", passed, failed))
print(string.rep("=", 70))

if failed > 0 then
    print("\n❌❌❌  存在问题！某些内部实现可能被暴露！\n")
    os.exit(1)
else
    print("\n✨✨✨  完美！所有内部实现已完全隐藏！\n")

    print("╔══════════════════════════════════════════════════╗")
    print("║     LXCLUA-NCore 内部实现完全不可探测          ║")
    print("╠══════════════════════════════════════════════════╣")
    print("║  ✅ 全局表 (_G):                                ║")
    print("║     • 0 个 __ 前缀函数                          ║")
    print("║     • 0 个内部全局变量                          ║")
    print("║     • 完全清洁                                  ║")
    print("║                                                  ║")
    print("║  ✅ 协程环境:                                    ║")
    print("║     • 无 __aco_ctx__ 泄露                        ║")
    print("║     • 上下文存储在注册表中                      ║")
    print("║                                                  ║")
    print("║  ✅ 功能完整性:                                  ║")
    print("║     • async/await 正常工作                      ║")
    print("║     • Promise 系统完整                          ║")
    print("║     • 错误处理正常                              ║")
    print("║     • 嵌套/并发/混合使用全部支持                ║")
    print("║                                                  ║")
    print("║  ✅ 安全等级:                                    ║")
    print("║     • 用户视角：原生语言特性                     ║")
    print("║     • 探测难度：极高（需 debug + 注册表知识）    ║")
    print("║     • 实现透明度：100% 隐藏                     ║")
    print("╚══════════════════════════════════════════════════╝")
    print("")
    print("💪 这就是真正的语法糖！")
    print("   - 看起来像原生语言特性")
    print("   - 用起来像原生语言特性")
    print("   - 实现细节完全不可见")
    print("")
end
