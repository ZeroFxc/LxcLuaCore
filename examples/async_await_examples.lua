-- ============================================================================
-- LXCLUA-NCore async/await 语法糖完整示例
-- 展示如何使用 asyncio.run() / asyncio.async() / asyncio.wait()
-- ============================================================================

local asyncio = require("asyncio")

print("=" .. string.rep("=", 70))
print("🚀 LXCLUA-NCore async/await 语法糖演示")
print("=" .. string.rep("=", 70) .. "\n")

-- ============================================================================
-- 辅助函数：定义 wait、wrap、nexttick 局部变量（推荐用法，避免保留字冲突）
-- ============================================================================
local wait = asyncio.wait
local wrap = asyncio.wrap
local nexttick = asyncio.nexttick

-- ============================================================================
-- 示例 1: 基础 async/await 用法
-- ============================================================================
print("📌 示例 1: 基础 async/await\n")

local function example1_basic()
    print("开始执行异步函数...")
    
    local p = asyncio.run(function()
        print("  [async] 步骤1: 开始")
        
        -- 使用局部变量 wait（最简洁的写法）
        local result = wait(asyncio.sleep(0.5))
        print("  [async] 步骤2: sleep 完成, result=" .. tostring(result))
        
        return "Hello from async!"
    end)
    
    if p then
        local final_result = p:await_sync(2000)
        print("  [main] 最终结果: " .. tostring(final_result))
    end
end

example1_basic()
print()

-- ============================================================================
-- 示例 2: 使用 async() 包装器创建可复用异步函数
-- ============================================================================
print("\n📌 示例 2: async() 包装器\n")

local fetchUser = wrap(function(userId)
    print("  [fetchUser] 获取用户 " .. userId)
    
    wait(asyncio.sleep(0.3))
    
    return {
        id = userId,
        name = "User_" .. userId,
        email = "user" .. userId .. "@example.com"
    }
end)

print("调用 fetchUser(42)...")
local userPromise = fetchUser(42)

if userPromise then
    local user = userPromise:await_sync(2000)
    if type(user) == "table" then
        print("  用户信息:")
        print("    ID: " .. tostring(user.id))
        print("    Name: " .. user.name)
        print("    Email: " .. user.email)
    end
end
print()

-- ============================================================================
-- 示例 3: 组合多个 await 操作
-- ============================================================================
print("\n📌 示例 3: 组合多个 await\n")

local function example3_composition()
    local p = asyncio.run(function()
        print("  [async] 开始任务...")
        
        wait(asyncio.sleep(0.2))
        print("  [async] ✅ 第一步完成 (0.2s)")
        
        local httpResult = wait(asyncio.http_get("http://127.0.0.1"))
        print("  [async] ✅ 第二步完成 (HTTP)")
        
        local ips = wait(asyncio.dns_resolve("localhost"))
        if type(ips) == "table" and #ips > 0 then
            print("  [async] ✅ 第三步完成 (DNS: " .. table.concat(ips, ", ") .. ")")
        end
        
        wait(asyncio.sleep(0.1))
        print("  [async] ✅ 全部完成!")
        
        return {status="success", steps=4}
    end)
    
    if p then
        local result = p:await_sync(5000)
        if type(result) == "table" then
            print("  结果: status=" .. result.status .. ", steps=" .. result.steps)
        end
    end
end

example3_composition()
print()

-- ============================================================================
-- 示例 4: defer() 让出控制权到下一个事件循环 tick
-- ============================================================================
print("\n📌 示例 4: defer()\n")

local function example4_defer()
    local p = asyncio.run(function()
        print("  [async] 任务 A 开始")
        
        wait(nexttick(function()
            print("  [defer] 延迟执行的回调")
        end))
        
        print("  [async] 任务 A 继续")
        
        return "completed"
    end)
    
    if p then
        local result = p:await_sync(2000)
        print("  结果: " .. tostring(result))
    end
end

example4_defer()
print()

-- ============================================================================
-- 示例 5: 并发执行多个异步任务
-- ============================================================================
print("\n📌 示例 5: 并发执行\n")

local function example5_concurrent()
    local start = os.clock()
    
    local task1 = asyncio.run(function()
        wait(asyncio.sleep(0.5))
        return "Task1"
    end)
    
    local task2 = asyncio.run(function()
        wait(asyncio.sleep(0.5))
        return "Task2"
    end)
    
    local task3 = asyncio.run(function()
        wait(asyncio.sleep(0.5))
        return "Task3"
    end)
    
    local results = {}
    if task1 then results[1] = task1:await_sync(2000) end
    if task2 then results[2] = task2:await_sync(2000) end
    if task3 then results[3] = task3:await_sync(2000) end
    
    for i, r in ipairs(results) do
        print("  " .. tostring(r) .. " 完成")
    end
    
    local elapsed = os.clock() - start
    print(string.format("  ⏱️  总耗时: %.0fms (理想 ≈ 500ms)", elapsed * 1000))
end

example5_concurrent()
print()

-- ============================================================================
-- 示例 6: 错误处理
-- ============================================================================
print("\n📌 示例 6: 错误处理\n")

local function example6_error_handling()
    local p = asyncio.run(function()
        print("  [async] 尝试执行可能失败的操作...")
        
        -- 模拟一个会失败的 Promise
        local fail_promise = asyncio.run(function()
            error("模拟错误!")
        end)
        
        local ok, err = pcall(function()
            wait(fail_promise)
        end)
        
        if not ok then
            print("  [async] ✅ 错误被捕获: " .. tostring(err))
            return {caught=true, error=err}
        else
            return {caught=false}
        end
    end)
    
    if p then
        local result = p:await_sync(2000)
        if type(result) == "table" and result.caught then
            print("  ✅ 错误处理成功!")
        end
    end
end

example6_error_handling()
print()

-- ============================================================================
-- 总结
-- ============================================================================
print("=" .. string.rep("=", 70))
print("🎉 async/await 语法糖演示完成！")
print()
print("📚 核心API:")
print("  • asyncio.run(fn)          - 运行异步函数，返回 Promise")
print("  • asyncio.wrap(fn)         - 包装可复用异步函数（推荐，避免保留字）")
print("  • asyncio.wait(promise)    - 等待 Promise 完成（推荐）")
print("  • asyncio.nexttick(fn)     - 延迟执行到下个 tick（推荐，避免保留字）")
print()
print("✨ 推荐用法 (最简洁优雅):")
print([[
  local wait = asyncio.wait      -- 定义一次，全局可用
  local wrap = asyncio.wrap      -- 定义一次，全局可用
  local nexttick = asyncio.nexttick
  
  local fetchData = wrap(function(url)
      local resp = wait(asyncio.http_get(url))
      return resp.body
  end)
  
  local data = fetchData("http://api.example.com"):await_sync()
]])
print()
print("💡 提示:")
print("  • asyncio.wait 是 asyncio['await'] 的别名，避免 Lua 5.4 保留字冲突")
print("  • asyncio.wrap 是 asyncio['async'] 的别名，避免 Lua 5.4 保留字冲突")
print("  • asyncio.nexttick 是 asyncio['defer'] 的别名，避免 LXCLUA-NCore 保留字冲突")
print("  • 所有异步操作必须在 asyncio.run() 创建的协程中使用 wait()")
print("  • 使用 :done() 和 :fail() 链式调用替代 then/catch（保留字问题）")
print("=" .. string.rep("=", 70) .. "\n")
