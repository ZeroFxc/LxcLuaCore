-- ============================================================================
-- LXCLUA-NCore AsyncIO 使用示例
-- 展示异步编程的实际应用场景
-- ============================================================================

local asyncio = require("asyncio")

print("=" .. string.rep("=", 70))
print("🚀 LXCLUA-NCore AsyncIO 实战示例")
print("=" .. string.rep("=", 70) .. "\n")

-- ============================================================================
-- 示例 1: 基础 async/await 模式
-- ============================================================================
print("📌 示例 1: 基础异步函数\n")

local function fetch_user_data(user_id)
    -- 模拟异步数据库查询
    local p = asyncio.sleep(0.1)
    if p then
        p:await_sync(2000)
    end
    
    return {
        id = user_id,
        name = "User_" .. user_id,
        email = "user" .. user_id .. "@example.com"
    }
end

local function example_basic_async()
    print("开始获取用户数据...")
    
    local p = asyncio.run(function()
        local start = os.clock()
        
        -- 并发获取多个用户
        local users = {}
        for i = 1, 3 do
            users[i] = fetch_user_data(i)
        end
        
        local elapsed = os.clock() - start
        print(string.format("✅ 获取 %d 个用户耗时: %.3fms", #users, elapsed * 1000))
        
        for _, user in ipairs(users) do
            print(string.format("   👤 ID=%d, Name=%s, Email=%s", 
                               user.id, user.name, user.email))
        end
        
        return {status="ok", count=#users}
    end)
    
    if p then
        local result = p:await_sync(5000)
        local ok, json = pcall(require, "json")
        if ok and json and json.encode then
            print("最终结果: " .. json.encode(result))
        else
            print("最终结果: " .. tostring(result))
        end
    end
end

example_basic_async()
print()

-- ============================================================================
-- 示例 2: 异步 HTTP 客户端
-- ============================================================================
print("\n📌 示例 2: 异步 HTTP 请求\n")

local function example_http_requests()
    local urls = {
        "http://127.0.0.1/",
        "http://localhost/"
    }
    
    print("并发发送 HTTP 请求...")
    local start = os.clock()
    
    local promises = {}
    for i, url in ipairs(urls) do
        promises[i] = asyncio.http_get(url)
    end
    
    local results = {}
    for i, p in ipairs(promises) do
        if p then
            local result = p:await_sync(3000)
            results[i] = result
            
            if type(result) == "table" then
                print(string.format("   [%d] Status: %d, Body: %d bytes", 
                                   i, result.status or 0, #(result.body or "")))
            else
                print(string.format("   [%d] Error: %s", i, tostring(result)))
            end
        end
    end
    
    local elapsed = os.clock() - start
    print(string.format("⏱️  总耗时: %.3fms (并行执行)", elapsed * 1000))
end

example_http_requests()
print()

-- ============================================================================
-- 示例 3: Promise 链式调用
-- ============================================================================
print("\n📌 示例 3: Promise 链式数据处理\n")

local function example_promise_chain()
    local data_pipeline = asyncio.sleep(0.001)
    
    if not data_pipeline then return end
    
    data_pipeline
        :done(function(v)
            print("步骤 1: 获取原始数据")
            return {value=10}
        end)
        :done(function(data)
            print("步骤 2: 数据转换 (×2)")
            data.value = data.value * 2
            return data
        end)
        :done(function(data)
            print("步骤 3: 数据验证")
            assert(data.value == 20, "Value mismatch!")
            data.validated = true
            return data
        end)
        :done(function(data)
            print("步骤 4: 最终结果")
            local ok2, json2 = pcall(require, "json")
            if ok2 and json2 and json2.encode then
                print("   📦 " .. json2.encode(data))
            else
                print("   📦 " .. tostring(data))
            end
            return data
        end)
        :fail(function(err)
            print("❌ 管道错误: " .. tostring(err))
        end)
    
    -- 给链一点时间完成（实际使用中不需要这样）
    asyncio.sleep(0.01):await_sync(100)
end

example_promise_chain()
print()

-- ============================================================================
-- 示例 4: 定时任务调度
-- ============================================================================
print("\n📌 示例 4: 定时任务与延迟执行\n")

local function example_scheduling()
    local counter = 0
    local max_count = 3
    
    print("启动定时器 (每100ms触发，共3次)...")
    local start = os.clock()
    
    for i = 1, max_count do
        local delay = i * 0.1  -- 100ms, 200ms, 300ms
        local p = asyncio.sleep(delay)
        
        if p then
            p:done(function()
                counter = counter + 1
                local now = os.clock() - start
                print(string.format("   ⏰ 定时器 #%d 触发 (%.0fms)", 
                                   counter, now * 1000))
            end)
        end
    end
    
    -- 等待所有定时器完成
    asyncio.sleep(0.35):await_sync(1000)
    
    assert(counter == max_count, string.format(
        "Expected %d triggers, got %d", max_count, counter))
    print("✅ 所有定时器正常触发")
end

example_scheduling()
print()

-- ============================================================================
-- 示例 5: 文件操作批处理
-- ============================================================================
print("\n📌 示例 5: 批量异步文件读取\n")

local function example_batch_file_io()
    -- 创建测试文件
    local test_files = {"test1.txt", "test2.txt", "test3.txt"}
    local contents = {
        "Content of file 1",
        "Content of file 2",
        "Content of file 3"
    }
    
    for i, filename in ipairs(test_files) do
        local f = io.open(filename, "w")
        if f then
            f:write(contents[i])
            f:close()
        end
    end
    
    print("批量读取多个文件...")
    local start = os.clock()
    
    local promises = {}
    for i, filename in ipairs(test_files) do
        promises[i] = asyncio.read_file(filename)
    end
    
    local read_results = {}
    for i, p in ipairs(promises) do
        if p then
            local content = p:await_sync(2000)
            read_results[i] = content
            local display_content = content
            if content and #content > 50 then
                display_content = content:sub(1, 50) .. "..."
            end
            print(string.format("   📄 %s: %s", test_files[i], 
                                display_content))
        end
    end
    
    local elapsed = os.clock() - start
    print(string.format("⏱️  批量读取耗时: %.3fms", elapsed * 1000))
    
    -- 验证内容
    for i, content in ipairs(read_results) do
        assert(content == contents[i], string.format(
            "File %d content mismatch", i))
    end
    print("✅ 所有文件内容正确")
    
    -- 清理
    for _, filename in ipairs(test_files) do
        os.remove(filename)
    end
end

example_batch_file_io()
print()

-- ============================================================================
-- 示例 6: DNS 解析缓存模拟
-- ============================================================================
print("\n📌 示例 6: DNS 解析\n")

local function example_dns_resolution()
    local hosts = {"localhost", "127.0.0.1", "google.com"}
    
    print("解析多个主机名...")
    local start = os.clock()
    
    local dns_cache = {}
    for _, host in ipairs(hosts) do
        local p = asyncio.dns_resolve(host)
        if p then
            local ips = p:await_sync(3000)
            dns_cache[host] = ips
            
            if type(ips) == "table" and #ips > 0 then
                print(string.format("   🌐 %-15s → %s", host, table.concat(ips, ", ")))
            else
                print(string.format("   ⚠️  %-15s → 解析失败", host))
            end
        end
    end
    
    local elapsed = os.clock() - start
    print(string.format("⏱️  DNS 解析总耗时: %.3fms", elapsed * 1000))
end

example_dns_resolution()
print()

-- ============================================================================
-- 示例 7: 错误处理与重试机制
-- ============================================================================
print("\n📌 示例 7: 错误处理与重试\n")

local function with_retry(fn, max_retries, delay)
    local last_error = nil
    
    for attempt = 1, (max_retries or 3) do
        local ok, result = pcall(fn)
        
        if ok then
            return true, result
        else
            last_error = result
            print(string.format("   🔁 第 %d/%d 次尝试失败: %s", 
                               attempt, max_retries or 3, tostring(result)))
            
            if attempt < (max_retries or 3) then
                local p = asyncio.sleep(delay or 0.1)
                if p then p:await_sync(1000) end
            end
        end
    end
    
    return false, last_error
end

local function example_retry_mechanism()
    local attempts = 0
    
    local flaky_operation = function()
        attempts = attempts + 1
        if attempts < 3 then
            error("Simulated failure (attempt " .. attempts .. ")")
        end
        return "success after " .. attempts .. " attempts!"
    end
    
    print("执行可能失败的操作（最多重试3次）...")
    local success, result = with_retry(flaky_operation, 3, 0.05)
    
    if success then
        print("✅ 操作成功: " .. tostring(result))
    else
        print("❌ 操作最终失败: " .. tostring(result))
    end
end

example_retry_mechanism()
print()

-- ============================================================================
-- 总结
-- ============================================================================

print("=" .. string.rep("=", 70))
print("🎉 所有示例执行完成！")
print()
print("📚 AsyncIO 核心功能总结:")
print("  • ✅ async/await 协程调度")
print("  • ✅ Promise 链式调用 (.then/.catch)")
print("  • ✅ 异步文件 I/O (read/write)")
print("  • ✅ 异步 HTTP 客户端 (GET/POST)")
print("  • ✅ 异步 DNS 解析")
print("  • ✅ 高精度定时器 (sleep/setInterval)")
print("  • ✅ 跨平台事件循环 (epoll/kqueue/IOCP/select)")
print("  • ✅ 线程池集成（自动并行化阻塞操作）")
print()
print("💡 下一步:")
print("  1. 在你的项目中使用: local asyncio = require('asyncio')")
print("  2. 将同步代码包装为 async 函数以获得非阻塞能力")
print("  3. 利用 Promise.all() 进行并发控制")
print("  4. 查看 tests/test_asyncio.lua 了解更多测试用例")
print("=" .. string.rep("=", 70) .. "\n")
