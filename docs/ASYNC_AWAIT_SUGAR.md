# LXCLUA-NCore Async/Await 语法糖完整文档

## 📚 概述

LXCLUA-NCore 提供了完整的 **async/await** 异步编程语法糖系统，包含两个层面：

1. **编译时脱糖（Compile-time Desugaring）**：通过 `llexer_compiler` 将现代语法转换为标准 Lua 代码
2. **运行时支持（Runtime Support）**：通过 `asyncio` 库提供 Promise、协程调度和异步 I/O

## 🎯 支持的语法形式

### 1. Async 函数声明

#### a) `local async function` (推荐)

```lua
-- 源代码（语法糖）
local async function fetchData(url)
    local resp = await(asyncio.http_get(url))
    return json.decode(resp.body)
end

-- 脱糖后（编译器生成）
local fetchData = asyncio.wrap(function(url)
    local resp = asyncio.wait(asyncio.http_get(url))
    return json.decode(resp.body)
end)
```

#### b) 全局 `async function`

```lua
-- 源代码
async function processData(data)
    await(asyncio.sleep(0.1))
    return data * 2
end

-- 脱糖后
processData = asyncio.wrap(function(data)
    asyncio.wait(asyncio.sleep(0.1))
    return data * 2
end)
```

#### c) Async 箭头函数

```lua
-- 源代码
local double = async (x) => x * 2

-- 脱糖后
local double = asyncio.wrap(function(x) return x * 2 end)
```

### 2. Await 表达式

```lua
-- 源代码
local result = await(asyncio.sleep(1))
local data = await(fetchUser(42))

-- 脱糖后
local result = asyncio.wait(asyncio.sleep(1))
local data = asyncio.wait(fetchUser(42))
```

### 3. 多参数支持

```lua
-- 源代码
local async function calculate(a, b, c)
    await(asyncio.sleep(0.01))
    return a + b + c
end

-- 脱糖后
local calculate = asyncio.wrap(function(a, b, c)
    asyncio.wait(asyncio.sleep(0.01))
    return a + b + c
end)
```

## 🔧 运行时 API 参考

### 核心函数

| 函数 | 说明 | 示例 |
|------|------|------|
| `asyncio.run(fn)` | 执行异步函数，返回 Promise | `asyncio.run(function() ... end)` |
| `asyncio.wrap(fn)` | 包装可复用异步函数 | `local fn = asyncio.wrap(function() ... end)` |
| `asyncio.wait(promise)` | 等待 Promise 完成（推荐） | `local r = wait(promise)` |
| `asyncio["await"](promise)` | 等待 Promise（保留字形式） | `local r = asyncio["await"](p)` |

### 别名（避免保留字冲突）

由于 Lua 5.4 和 LXCLUA-NCore 的保留字限制，提供了别名：

| 原始名称 | 别名 | 原因 |
|----------|------|------|
| `async` | `wrap` | Lua 5.4 保留字 |
| `await` | `wait` | Lua 5.4 保留字 |
| `defer` | `nexttick` | LXCLUA-NCore 保留字 |

**推荐用法**：

```lua
local asyncio = require("asyncio")
local wait = asyncio.wait      -- 定义一次，全局可用
local wrap = asyncio.wrap      -- 定义一次，全局可用
local nexttick = asyncio.nexttick
```

### Promise 方法

所有返回 Promise 的对象都支持以下方法：

#### 链式回调

```lua
promise:done(function(value)
    -- 成功时调用
    print("成功:", value)
end)

promise:fail(function(error)
    -- 失败时调用
    print("错误:", error)
end)

promise:finally(function()
    -- 无论成功或失败都调用
    print("完成")
end)
```

#### 同步等待（阻塞）

```lua
-- 等待 Promise 完成（会阻塞当前线程）
local result = promise:await_sync(timeout_ms)

-- timeout_ms: 可选，超时时间（毫秒），-1 为无限等待
-- 返回: Promise 的结果值
-- 如果超时: 返回 nil + 错误信息
```

#### 状态查询

```lua
print(promise.state)  --> "pending" / "fulfilled" / "rejected"
print(tostring(promise))  --> "Promise<Fulfilled> [tag]"
```

#### 取消操作

```lua
local success = promise:cancel()
-- 返回: true 如果取消成功，false 如果 Promise 已 settled
```

### 组合操作

#### `asyncio.all(...)` - 等待所有完成

```lua
local p = asyncio.all(p1, p2, p3)
local results = p:await_sync()

-- results 是数组: {result1, result2, result3}
-- 任一失败则立即 reject
```

#### `asyncio.race(...)` - 第一个完成的胜出

```lua
local p = asyncio.race(slowPromise, fastPromise)
local winner = p:await_sync()  --> 最快完成的结果
```

#### `asyncio.any(...)` - 任一成功即完成

```lua
local p = asyncio.any(maybeFail1, maybeFail2, successPromise)
local result = p:await_sync()  --> 第一个成功的结果
-- 全部失败才 reject
```

#### `asyncio.allSettled(...)` - 等待所有 settle

```lua
local p = asyncio.allSettled(p1, p2)
local results = p:await_sync()

-- results[i] = {
--   status = "fulfilled" | "rejected",
--   value = ...,  -- fulfilled 时
--   reason = ...  -- rejected 时
-- }
```

#### `asyncio.gather(...)` - all 的 Python 兼容别名

```lua
local results = asyncio.gather(p1, p2, p3):await_sync()
```

### 高级工具

#### `asyncio.map(fn, table[, concurrency])` - 并发映射

```lua
local urls = {"http://a.com", "http://b.com", "http://c.com"}

local results = asyncio.map(
    function(url)
        return http_get(url):await_sync()
    end,
    urls,
    3  -- 最大并发数（可选，0=无限制）
):await_sync()

-- results 是与 urls 等长的数组
```

#### `asyncio.eachSeries(table, fn)` - 串行执行

```lua
asyncio.eachSeries({"file1.txt", "file2.txt"}, function(file)
    local content = read_file(file):await_sync()
    process(content)
end):await_sync()
```

#### `asyncio.retry(fn, maxRetries[, delayMs])` - 自动重试

```lua
local result = asyncio.retry(function()
    return riskyOperation():await_sync()
end, 3, 1000):await_sync()
-- 最多重试 3 次，间隔 1 秒
```

#### `asyncio.withTimeout(promise, ms)` - 带超时的等待

```lua
local result = asyncio.withTimeout(slowOperation, 500):await_sync()
-- 500ms 后自动 reject
```

### 辅助工具

#### `asyncio.isPromise(value)` - 类型检查

```lua
if asyncio.isPromise(result) then
    local data = wait(result)
else
    processSync(result)
end
```

#### `asyncio.promisify(fn)` - 回转 Promise

将 Node.js 风格的回调函数转换为返回 Promise 的函数：

```lua
local fs_read = asyncio.promisify(function(path, callback)
    -- 异步读取文件
    readFile(path, function(err, data)
        if err then
            callback(err)  -- callback(error)
        else
            callback(nil, data)  -- callback(nil, result)
        end
    end)
end)

-- 使用时像普通 async 函数
local content = fs_read("test.txt"):await_sync()
```

#### `asyncio.resolve(value)` / `asyncio.reject(reason)` - 静态工厂

```lua
local p = asyncio.resolve(42)        --> 已完成的 Promise
local p = asyncio.reject("error")    --> 已拒绝的 Promise
```

### 定时器

#### `asyncio.setInterval(callback, seconds[, times])` - 周期执行

```lua
local id = setInterval(function()
    print("tick")
end, 1.0)  -- 每 1 秒执行一次

clearInterval(id)  -- 停止定时器
```

#### `asyncio.clearInterval(timerId)` - 取消定时器

```lua
clearInterval(timer_id)
```

## 💡 使用模式

### 模式 1: 基础异步函数

```lua
local wrap = require("asyncio").wrap
local wait = require("asyncio").wait

local fetchUser = wrap(function(userId)
    local resp = wait(http_get("/api/users/" .. userId))
    return parseJson(resp.body)
end)

-- 调用
local user = fetchUser(42):await_sync()
print(user.name)
```

### 模式 2: 并发执行

```lua
local processAll = wrap(function(urls)
    -- 并发发起所有请求
    local promises = {}
    for i, url in ipairs(urls) do
        promises[i] = http_get(url)
    end

    -- 等待所有完成
    local results = wait(asyncio.all(table.unpack(promises)))
    return results
end)

local data = processAll({"url1", "url2", "url3"}):await_sync()
```

### 模式 3: 错误处理

```lua
local safeFetch = wrap(function(url)
    local ok, result = pcall(function()
        local resp = wait(http_get(url))
        if resp.status ~= 200 then
            error("HTTP " .. resp.status)
        end
        return resp.body
    end)

    if not ok then
        return {error=true, message=result}
    end
    return {error=false, data=result}
end)

local result = safeFetch("http://example.com"):await_sync()
if result.error then
    print("错误:", result.message)
else
    print("数据:", result.data)
end
```

### 模式 4: 资源清理（模拟 try-finally）

```lua
local withResource = wrap(function(resourceId)
    local resource = acquire(resourceId)

    local ok, result = pcall(function()
        -- 使用资源
        return useResource(resource)
    end)

    -- 确保 cleanup 总是执行
    cleanup(resource)

    if not ok then
        return {error=result}
    end
    return result
end)
```

### 模式 5: 重试机制

```lua
local retry = wrap(function(operation, maxRetries)
    for attempt = 1, maxRetries do
        local ok, result = pcall(function()
            return operation():await_sync()
        end)

        if ok then
            return {success=true, data=result, attempts=attempt}
        end

        if attempt < maxRetries then
            wait(asyncio.sleep(0.1 * attempt))  -- 指数退避
        end
    end

    return {success=false, error="Max retries exceeded"}
end)

local result = retry(riskyOperation, 5):await_sync()
```

## ⚙️ 编译时脱糖规则

当使用 `llexer_compiler` 处理 `.lua` 文件时，以下转换会自动发生：

### 规则 1: 函数声明

```
local async function name(args)
    body
end
    ↓ 脱糖
local name = asyncio.wrap(function(args)
    body
end)
```

### 规则 2: 箭头函数

```
async (args) => expression
    ↓ 脱糖
asyncio.wrap(function(args) return expression end)
```

### 规则 3: Await 表达式

```
await(expression)
    ↓ 脱糖
asyncio.wait(expression)
```

## 🔍 调试技巧

### 1. 查看 Promise 状态

```lua
local p = someAsyncOperation()
print(p.state)         --> "pending" / "fulfilled" / "rejected"
print(tostring(p))     --> "Promise<Pending> [tag]"
```

### 2. 设置调试标签

```lua
promise_set_tag(p, "fetchUser")
-- 输出: "Promise<Fulfilled> [fetchUser]"
```

### 3. 使用 isPromise 检查类型

```lua
if asyncio.isPromise(value) then
    -- 处理 Promise
else
    -- 处理普通值
end
```

### 4. 链式调用调试

```lua
someAsyncOp()
    :done(function(v) print("成功:", v) end)
    :fail(function(e) print("失败:", e) end)
    :finally(function() print("结束") end)
```

## 📊 性能建议

1. **并发优于串行**: 使用 `all()` 或手动并发多个 Promise
2. **合理使用 defer**: 在长时间计算中使用 `nexttick()` 让出控制权
3. **设置超时**: 使用 `withTimeout()` 避免无限等待
4. **控制并发数**: `map()` 的第三个参数限制最大并发
5. **复用 async 函数**: 使用 `wrap()` 创建可复用的异步函数

## 🎓 与其他语言的对比

### JavaScript (ES2017+)

| JavaScript | LXCLUA-NCore |
|------------|--------------|
| `async function f(){}` | `async function f(){}` (编译时) |
| `const f = async () => {}` | `local f = async () => {}` (编译时) |
| `await promise` | `await(promise)` (编译时) |
| `Promise.all([...])` | `asyncio.all(...)` |
| `Promise.race([...])` | `asyncio.race(...)` |
| `new Promise((resolve,reject)=>{})` | `asyncio.run(function() ... end)` |

### Python (asyncio)

| Python | LXCLUA-NCore |
|--------|--------------|
| `async def f(): pass` | `async function f() end` |
| `await coroutine` | `await(coroutine)` |
| `asyncio.gather(*tasks)` | `asyncio.gather(...)` |
| `asyncio.wait(tasks)` | `asyncio.all(...)` |

## ❓ 常见问题

### Q: 为什么需要 `wait()` 而不是直接用 `await`?

**A**: 因为 `await` 是 Lua 5.4 的保留字。我们提供了：
- `asyncio.wait(promise)` - 推荐用法
- `asyncio["await"](promise)` - 字符串索引形式
- 编译时脱糖：源码写 `await(expr)` → 生成 `asyncio.wait(expr)`

### Q: 如何在非协程环境中获取 Promise 结果?

**A**: 使用 `:await_sync()` 方法（会阻塞当前线程）：
```lua
local result = asyncFunction():await_sync(5000)  -- 最多等 5 秒
```

### Q: Promise 会造成内存泄漏吗?

**A**: 不会。LXCLUA-NCore 的 Promise 实现使用引用计数（reference counting）和 Lua GC 双重保护。当 Promise 不再被引用时会自动释放。

### Q: 可以在 async 函数中嵌套调用其他 async 函数吗?

**A**: 可以！这正是 async/await 的核心优势：
```lua
local inner = wrap(function(n)
    wait(asyncio.sleep(0.1))
    return n + 1
end)

local outer = wrap(function()
    local a = wait(inner(10))
    local b = wait(inner(20))
    return a + b
end)
```

### Q: 如何取消正在进行的异步操作?

**A**: 使用 `:cancel()` 方法：
```lua
local p = longRunningOperation()
local success = p:cancel()  --> true 如果成功取消
```

注意：并非所有操作都能被取消，取决于底层实现。

---

**版本**: LXCLUA-NCore v1.0+
**最后更新**: 2026-04-06
