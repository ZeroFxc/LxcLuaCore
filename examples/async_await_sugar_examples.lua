-- ============================================================================
-- LXCLUA-NCore Async/Await 语法糖 - 实际使用示例
-- 展示如何使用编译时脱糖 + 运时 API 编写优雅的异步代码
-- ============================================================================

local asyncio = require("asyncio")

-- 推荐的简洁别名（避免保留字冲突）
local wait = asyncio.wait
local wrap = asyncio.wrap
local nexttick = asyncio.nexttick

print("=" .. string.rep("=", 70))
print("💡 Async/Await 语法糖实际使用示例")
print("=" .. string.rep("=", 70) .. "\n")

-- ============================================================================
-- 示例 1: HTTP 客户端（模拟）
-- ============================================================================

print("📌 示例 1: 异步 HTTP 客户端\n")

local function createHttpClient()
    local http = {}

    -- 使用 wrap 创建可复用的异步方法
    http.get = wrap(function(url)
        print("  [HTTP] GET " .. url)
        -- 模拟网络延迟
        wait(asyncio.sleep(0.1))

        -- 模拟返回响应
        return {
            status = 200,
            body = '{"message": "Hello from ' .. url .. '"}',
            headers = {"Content-Type": "application/json"}
        }
    end)

    http.post = wrap(function(url, data)
        print("  [HTTP] POST " .. url)
        wait(asyncio.sleep(0.15))

        return {
            status = 201,
            body = '{"created": true, "data": ' .. data .. '}'
        }
    end)

    return http
end

local http = createHttpClient()

-- 使用 async 函数
local fetchUser = wrap(function(userId)
    local resp = wait(http.get("/api/users/" .. tostring(userId)))
    if resp.status == 200 then
        return {id = userId, name = "User" .. tostring(userId)}
    else
        error("Failed to fetch user")
    end
end)

-- 调用并获取结果
print("获取用户信息...")
local user = fetchUser(42):await_sync(2000)
if type(user) == "table" then
    print(string.format("  用户: id=%d, name=%s", user.id, user.name))
end
print()

-- ============================================================================
-- 示例 2: 并发数据处理
-- ============================================================================

print("\n📌 示例 2: 并发数据处理\n")

local processAllUrls = wrap(function(urls)
    print("  开始并发处理 " .. #urls .. " 个 URL...")

    -- 并发发起所有请求
    local promises = {}
    for i, url in ipairs(urls) do
        promises[i] = http.get(url)
    end

    -- 等待所有请求完成
    local results = wait(asyncio.all(table.unpack(promises)))

    -- 处理结果
    local successes = 0
    for i, resp in ipairs(results) do
        if resp.status == 200 then
            successes = successes + 1
        end
    end

    return {total=#urls, success=successes}
end)

local urls = {
    "/api/data/1",
    "/api/data/2",
    "/api/data/3",
    "/api/data/4",
    "/api/data/5"
}

print("并发处理多个 URL...")
local stats = processAllUrls(urls):await_sync(3000)
if type(stats) == "table" then
    print(string.format("  结果: %d/%d 成功", stats.success, stats.total))
end
print()

-- ============================================================================
-- 示例 3: 带重试的异步操作
-- ============================================================================

print("\n📌 示例 3: 自动重试机制\n")

local unreliableOperation = wrap(function()
    math.randomseed(os.time())
    local shouldFail = math.random() > 0.7  -- 30% 成功率

    if shouldFail then
        error("Random failure!")
    end

    wait(asyncio.sleep(0.05))
    return {data="operation succeeded", timestamp=os.time()}
end)

-- 使用 retry 包装（手动实现重试逻辑）
local withRetry = wrap(function(fn, maxRetries)
    local lastError = nil
    for attempt = 1, maxRetries do
        local ok, result = pcall(function()
            return fn():await_sync(1000)
        end)

        if ok then
            return {success=true, data=result, attempts=attempt}
        else
            lastError = result
            print(string.format("    ⚠️  第 %d 次尝试失败: %s", attempt, tostring(result)))
            if attempt < maxRetries then
                wait(asyncio.sleep(0.1 * attempt))  -- 指数退避
            end
        end
    end

    return {success=false, error=lastError, attempts=maxRetries}
end)

print("执行带重试的操作...")
local result = withRetry(unreliableOperation, 5):await_sync(5000)
if type(result) == "table" then
    if result.success then
        print(string.format("  ✅ 成功! (第 %d 次尝试)", result.attempts))
    else
        print(string.format("  ❌ 失败: %s (共 %d 次)", tostring(result.error), result.attempts))
    end
end
print()

-- ============================================================================
-- 示例 4: 异步迭代器模式
-- ============================================================================

print("\n📌 示例 4: 异步数据流处理\n")

local asyncGenerator = function(items, processor)
    -- 返回一个异步函数，逐个处理项目
    return wrap(function()
        local results = {}
        for i, item in ipairs(items) do
            -- 处理每个项目（可能是异步操作）
            local processed = wait(processor(item))
            table.insert(results, {
                index=i,
                input=item,
                output=processed
            })

            -- 可选：让出控制权，避免阻塞事件循环
            wait(nexttick(wrap(function() end)))  -- 空的 nexttick
        end
        return results
    end)
end

-- 定义一个异步处理器
local asyncProcessor = wrap(function(item)
    wait(asyncio.sleep(0.02))  -- 模拟异步 I/O
    return item * item  -- 计算平方
end)

local items = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10}

print("异步处理数据流...")
local generator = asyncGenerator(items, asyncProcessor)
local processedResults = generator():await_sync(3000)

if type(processedResults) == "table" then
    print("  处理结果:")
    for _, r in ipairs(processedResults) do
        print(string.format("    [%d] %d → %d", r.index, r.input, r.output))
    end
end
print()

-- ============================================================================
-- 示例 5: Promise 链式调用
-- ============================================================================

print("\n📌 示例 5: Promise 链式调用\n")

local pipeline = asyncio.run(function()
    -- 步骤1: 获取数据
    local data = wait(http.get("/api/start"))

    -- 步骤2: 转换数据
    wait(asyncio.sleep(0.03))

    -- 步骤3: 提交结果
    local final = wait(http.post("/api/end", '{"processed":true}'))

    return {
        steps_completed = 3,
        final_status = final.status
    }
end)

-- 使用链式回调（替代 await_sync）
pipeline:done(function(result)
    print(string.format("  ✅ 管道完成: %d 步骤, 最终状态=%d",
           result.steps_completed, result.final_status))
end):fail(function(err)
    print("  ❌ 管道失败: " .. tostring(err))
end)

-- 等待链式调用完成
wait(asyncio.sleep(0.5))
print()

-- ============================================================================
-- 示例 6: 错误处理最佳实践
-- ============================================================================

print("\n📌 示例 6: 错误处理与资源清理\n")

local safeOperation = wrap(function(resourceId)
    local resource = nil
    local cleanup = function()
        if resource then
            print(string.format("    🧹 清理资源: %s", resourceId))
            resource = nil
        end
    end

    -- 使用 pcall + finally 模式
    local ok, result = pcall(function()
        -- 分配资源
        resource = {id=resourceId, acquired=os.time()}
        print(string.format("  📦 获取资源: %s", resourceId))

        -- 执行可能失败的操作
        wait(asyncio.sleep(0.05))

        if resourceId == "risky" then
            error("Simulated error for risky resource!")
        end

        return {status="ok", resource=resourceId}
    end)

    -- 确保 cleanup 总是执行（类似 try-finally）
    cleanup()

    if not ok then
        -- 返回错误信息而不是抛出异常
        return {status="error", message=tostring(result), resource=resourceId}
    end

    return result
end)

-- 测试正常情况
print("测试正常资源...")
local normalResult = safeOperation("normal"):await_sync(1000)
if type(normalResult) == "table" then
    print(string.format("  结果: status=%s, resource=%s",
           normalResult.status, normalResult.resource or "N/A"))
end

-- 测试错误情况
print("\n测试有风险的资源...")
local errorResult = safeOperation("risky"):await_sync(1000)
if type(errorResult) == "table" then
    print(string.format("  结果: status=%s, message=%s",
           errorResult.status, errorResult.message or "N/A"))
end
print()

-- ============================================================================
-- 示例 7: 使用 isPromise 和工具函数
-- ============================================================================

print("\n📌 示例 7: 工具函数使用\n")

local smartFetch = wrap(function(urlOrPromise)
    -- 支持传入 URL 字符串或已有的 Promise
    if asyncio.isPromise(urlOrPromise) then
        print("  检测到 Promise 参数，直接等待")
        return wait(urlOrPromise)
    else
        print(string.format("  检测到 URL 参数: %s，发起请求", urlOrPromise))
        return wait(http.get(urlOrPromise))
    end
end)

-- 两种用法都支持
print("方式1: 传入 URL")
local r1 = smartFetch("/api/test"):await_sync(1000)

print("\n方式2: 传入 Promise")
local existingPromise = http.get("/api/cached")
local r2 = smartFetch(existingPromise):await_sync(1000)

print("\n两种方式都成功！")
print()

-- ============================================================================
-- 总结
-- ============================================================================

print("=" .. string.rep("=", 70))
print("🎉 Async/Await 语法糖示例演示完成！\n")

print("📖 关键要点:")
print("  1. 使用 wrap() 创建可复用的异步函数")
print("  2. 使用 wait() 在协程中等待 Promise")
print("  3. 使用 all()/race() 组合多个异步操作")
print("  4. 使用 :done()/:fail() 进行链式回调")
print("  5. 使用 pcall 包装可能失败的异步操作")
print("  6. 使用 isPromise() 进行类型检查")
print("  7. 使用 nexttick() 让出控制权")
print("")
print("✨ 优势:")
print("  • 代码结构清晰，避免回调地狱")
print("  • 错误处理统一，使用 try-catch 或 pcall")
print("  • 支持并发执行，提升性能")
print("  • 与同步代码风格一致，学习成本低")
print("")
print("=" .. string.rep("=", 70) .. "\n")
