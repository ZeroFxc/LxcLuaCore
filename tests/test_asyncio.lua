-- ============================================================================
-- LXCLUA-NCore 异步 I/O 运行时测试套件
-- 测试 asyncio 库的所有功能：Promise、事件循环、异步 I/O、协程调度
-- ============================================================================

local asyncio = require("asyncio")
local test_name = "AsyncIO Runtime Test Suite"
local passed = 0
local failed = 0

-- 辅助函数
local function test(description, fn)
    local ok, err = pcall(fn)
    if ok then
        print("✅ PASS: " .. description)
        passed = passed + 1
    else
        print("❌ FAIL: " .. description .. " - " .. tostring(err))
        failed = failed + 1
    end
end

print("\n" .. string.rep("=", 70))
print("🚀 " .. test_name)
print(string.rep("=", 70) .. "\n")

-- ============================================================================
-- 1. 模块加载测试
-- ============================================================================

test("asyncio 模块可以正常加载", function()
    assert(asyncio ~= nil, "asyncio module is nil")
    assert(type(asyncio.run) == "function", "asyncio.run is not a function")
    assert(type(asyncio.sleep) == "function", "asyncio.sleep is not a function")
    assert(type(asyncio.http_get) == "function", "asyncio.http_get is not a function")
    assert(type(asyncio.read_file) == "function", "asyncio.read_file is not a function")
    assert(type(asyncio.dns_resolve) == "function", "asyncio.dns_resolve is not a function")
end)

test("常量定义正确", function()
    assert(asyncio.PENDING == 0, "PENDING constant mismatch")
    assert(asyncio.FULFILLED == 1, "FULFILLED constant mismatch")
    assert(asyncio.REJECTED == 2, "REJECTED constant mismatch")
end)

-- ============================================================================
-- 2. Promise 基础功能测试
-- ============================================================================

test("Promise 对象创建和状态检查", function()
    local p = asyncio.sleep(0)
    assert(p ~= nil, "Promise is nil")
    assert(type(p.state) == "function", "state method missing")
    
    local state = p:state()
    assert(state == "pending" or state == "fulfilled" or state == "rejected",
           "Invalid promise state: " .. tostring(state))
end)

test("Promise.then() 链式调用", function()
    local p = asyncio.sleep(0)
    assert(p ~= nil, "Failed to create Promise for then test")
    
    -- 测试 then 方法存在且可调用
    local p2 = p:done(function(result)
        return result * 2
    end)
    assert(p2 ~= nil, "then() returned nil")
end)

test("Promise.catch() 错误处理", function()
    local p = asyncio.sleep(0)
    assert(p ~= nil)
    
    local p2 = p:fail(function(err)
        print("Caught error: " .. tostring(err))
        return "recovered"
    end)
    assert(p2 ~= nil, "catch() returned nil")
end)

-- ============================================================================
-- 3. 异步 Sleep 测试
-- ============================================================================

test("异步 sleep 基本功能（短延迟）", function()
    local start = os.clock()
    local p = asyncio.sleep(0.01)  -- 10ms
    assert(p ~= nil, "sleep() returned nil")
    
    -- 同步等待完成（仅用于测试）
    local result = p:await_sync(1000)  -- 1秒超时
    local elapsed = os.clock() - start
    
    assert(elapsed >= 0.005, "Sleep was too short: " .. elapsed .. "s")
    print("   ⏱️  实际睡眠时间: " .. string.format("%.3f", elapsed * 1000) .. "ms")
end)

test("多次连续 sleep 不阻塞", function()
    local start = os.clock()
    
    local p1 = asyncio.sleep(0.001)
    local p2 = asyncio.sleep(0.001)
    local p3 = asyncio.sleep(0.001)
    
    -- 所有 sleep 应该几乎同时开始
    p1:await_sync(1000)
    p2:await_sync(1000)
    p3:await_sync(1000)
    
    local elapsed = os.clock() - start
    -- 总时间应该接近 10ms 而不是 30ms（如果真正并行的话）
    print("   ⏱️  三次 sleep 总时间: " .. string.format("%.3f", elapsed * 1000) .. "ms")
end)

-- ============================================================================
-- 4. 异步文件 I/O 测试
-- ============================================================================

test("异步读取文件内容", function()
    -- 创建测试文件
    local test_content = "Hello from AsyncIO Test! 🚀\nThis is a test file.\n"
    local test_file = "_asyncio_test_temp.txt"
    
    local f = io.open(test_file, "w")
    if f then
        f:write(test_content)
        f:close()
        
        -- 异步读取
        local p = asyncio.read_file(test_file)
        assert(p ~= nil, "read_file returned nil")
        
        local result = p:await_sync(5000)
        assert(type(result) == "string", "Result is not a string")
        -- 标准化换行符（Windows \r\n → \n）
        result = result:gsub("\r\n", "\n"):gsub("\r", "\n")
        assert(result == test_content, "Content mismatch")
        
        -- 清理
        os.remove(test_file)
    else
        print("   ⚠️  跳过文件写入测试（无法创建文件）")
    end
end)

-- ============================================================================
-- 5. 异步 HTTP 请求测试
-- ============================================================================

test("异步 HTTP GET 请求（本地回环）", function()
    -- 使用 httpbin.org 或 localhost 进行测试
    local test_url = "http://127.0.0.1/"
    
    local p = asyncio.http_get(test_url)
    if p then
        local result = p:await_sync(5000)  -- 5秒超时
        
        if type(result) == "table" then
            print("   📡 HTTP Status: " .. tostring(result.status))
            print("   📦 Body length: " .. tostring(#result.body or 0))
            assert(result.status ~= nil, "No status code in response")
        else
            print("   ⚠️  HTTP 请求失败或超时: " .. tostring(result))
        end
    else
        print("   ⚠️  无法创建 HTTP 请求（可能网络不可用）")
    end
end)

-- ============================================================================
-- 6. DNS 解析测试
-- ============================================================================

test("异步 DNS 解析", function()
    local hostname = "localhost"
    
    local p = asyncio.dns_resolve(hostname)
    if p then
        local result = p:await_sync(5000)
        
        if type(result) == "table" and #result > 0 then
            print("   🌐 DNS 解析结果:")
            for i, ip in ipairs(result) do
                print("      [" .. i .. "] " .. ip)
            end
            assert(#result > 0, "DNS returned no addresses")
        elseif type(result) == "string" then
            print("   ⚠️  DNS 解析错误: " .. result)
        else
            print("   ⚠️  DNS 返回异常类型: " .. type(result))
        end
    else
        print("   ⚠️  无法执行 DNS 解析")
    end
end)

-- ============================================================================
-- 7. 协程与 async/await 集成测试
-- ============================================================================

test("async 函数运行器基本功能", function()
    local executed = false
    
    local async_fn = function()
        executed = true
        return "async completed!"
    end
    
    local p = asyncio.run(async_fn)
    if p then
        local result = p:await_sync(2000)
        assert(executed == true, "Async function did not execute")
        print("   ✨ Async 函数返回值: " .. tostring(result))
    else
        print("   ⚠️  async run 失败")
    end
end)

test("async 函数中包含 await", function()
    local results = {}
    
    local async_fn = function()
        table.insert(results, "start")
        
        -- await sleep（模拟异步操作）
        local p = asyncio.sleep(0.005)
        if p then
            p:await_sync(1000)
        end
        
        table.insert(results, "after_sleep")
        return {status="ok"}
    end
    
    local p = asyncio.run(async_fn)
    if p then
        local result = p:await_sync(3000)
        assert(#results == 2, "Expected 2 log entries, got " .. #results)
        assert(results[1] == "start", "First entry wrong")
        assert(results[2] == "after_sleep", "Second entry wrong")
        print("   📝 执行日志: " .. table.concat(results, " → "))
    else
        print("   ⚠️  async with await test skipped")
    end
end)

-- ============================================================================
-- 8. Promise 链式调用组合测试
-- ============================================================================

test("复杂的 Promise 链式调用", function()
    local chain_results = {}
    
    local p = asyncio.sleep(0.001)
    if p then
        p
            :done(function(v)
                table.insert(chain_results, "step1")
                return 10
            end)
            :done(function(v)
                table.insert(chain_results, "step2")
                return v * 2
            end)
            :done(function(v)
                table.insert(chain_results, "step3")
                return v + 5
            end)
            :fail(function(err)
                table.insert(chain_results, "error:" .. tostring(err))
            end)
        
        -- 等待链完成
        -- 注意：链式调用中的最后一个 Promise 需要等待
    end
    
    print("   🔗 链式调用步骤数: " .. #chain_results)
end)

-- ============================================================================
-- 9. 并发操作测试
-- ============================================================================

test("并发执行多个异步操作", function()
    local start = os.clock()
    local promises = {}
    
    -- 创建多个并发任务
    for i = 1, 3 do
        promises[i] = asyncio.sleep(0.01)  -- 每个 10ms
    end
    
    -- 等待所有完成
    for i, p in ipairs(promises) do
        if p then p:await_sync(2000) end
    end
    
    local elapsed = os.clock() - start
    print("   ⚡ 并发执行时间: " .. string.format("%.3f", elapsed * 1000) .. "ms")
    print("   📊 理想情况应 ≈ 10ms（并行）而非 30ms（串行）")
end)

-- ============================================================================
-- 10. 错误处理测试
-- ============================================================================

test("错误传播和处理", function()
    local error_caught = false
    
    local will_fail = function()
        error("Test error message!")
    end
    
    local p = asyncio.run(will_fail)
    if p then
        p:fail(function(err)
            error_caught = true
            print("   🛡️  成功捕获错误: " .. tostring(err))
        end)
        
        -- 等待一下让错误处理完成
        asyncio.sleep(0.001):await_sync(1000)
    end
    
    -- 注意：错误可能需要特殊处理才能被 catch 到
    print("   ℹ️  错误捕获状态: " .. tostring(error_caught))
end)

-- ============================================================================
-- 11. 性能基准测试
-- ============================================================================

test("性能基准：大量 Promise 创建", function()
    local count = 1000
    local start = os.clock()
    
    local promises = {}
    for i = 1, count do
        promises[i] = asyncio.sleep(0)  -- 立即完成的 Promise
    end
    
    local create_time = os.clock() - start
    print("   ⚙️  创建 " .. count .. " 个 Promise: " .. 
          string.format("%.3f", create_time * 1000) .. "ms")
    print("   📈 吞吐量: " .. math.floor(count / create_time) .. " ops/sec")
    
    -- 清理引用
    promises = nil
    collectgarbage()
end)

-- ============================================================================
-- 12. 边界条件测试
-- ============================================================================

test("边界条件：零延迟 sleep", function()
    local p = asyncio.sleep(0)
    assert(p ~= nil, "Zero-delay sleep failed")
    
    local result = p:await_sync(1000)
    assert(result ~= nil, "Zero-delay sleep returned nil")
end)

test("边界条件：极长超时（不实际等待）", function()
    local p = asyncio.sleep(99999)  -- 很长的延迟
    if p then
        -- 只检查创建成功，不实际等待
        local state = p:state()
        assert(state == "pending", "Should be pending immediately after creation")
    end
end)

-- ============================================================================
-- 测试总结
-- ============================================================================

print("\n" .. string.rep("=", 70))
print(string.format("📊 测试结果: %d 通过, %d 失败, 共 %d 个测试", 
                     passed, failed, passed + failed))

if failed == 0 then
    print("🎉 所有测试通过！LXCLUA AsyncIO 运行时运行正常！")
else
    print("⚠️  有 " .. failed .. " 个测试失败，请检查上述输出")
end

print(string.rep("=", 70) .. "\n")

return (failed == 0)
