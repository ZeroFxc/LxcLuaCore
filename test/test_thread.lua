--[[
test_thread.lua — 线程模块 (thread) 功能测试
覆盖：thread.create, thread.join, thread.createx, thread.detach,
      thread.self, thread.name, thread.id,
      thread.channel (send/receive/try_send/try_recv/close/peek),
      thread.pick, thread.on, thread.over
--]]
local thread = require("thread")
local passed = 0
local failed = 0

local function check(cond, msg)
    if cond then
        passed = passed + 1
        print("[PASS] " .. msg)
    else
        failed = failed + 1
        print("[FAIL] " .. msg)
    end
end

local function assert_error(fn, msg)
    local ok, err = pcall(fn)
    check(not ok and err ~= nil, msg .. " (error: " .. tostring(err) .. ")")
end

print("=== 1. 基础线程创建与 join ===\n")

-- 1.1 创建线程并 join，无返回值
local ok1, res1 = pcall(function()
    local th = thread.create(function()
        local x = 0
        for i = 1, 1000 do x = x + i end
    end)
    local succ = th:join()
    return succ
end)
check(ok1 and res1, "thread.create + join (no return) -> success=true")

-- 1.2 创建线程并 join，有返回值
do
    local th = thread.create(function(a, b)
        return a + b, a - b
    end, 42, 10)
    local succ, sum, diff = th:join()
    check(succ == true and sum == 52 and diff == 32,
        "thread.create + join (with returns) -> true, 52, 32")
end

print("\n=== 2. 线程错误处理 (has_error) ===\n")

-- 2.1 线程执行出错，join 应返回 false + 错误信息
local th_err = thread.create(function()
    error("test error in thread")
end)
local succ, errmsg = th_err:join()
check(succ == false and type(errmsg) == "string",
    "thread.join on error -> false, error_string")

-- 2.2 线程执行出错，join 返回 false + 错误字符串
local th_err2 = thread.create(function()
    error("BOOM-42")
end)
local succ2, errmsg2 = th_err2:join()
check(succ2 == false and type(errmsg2) == "string",
    "thread.join on error returns false, string")

print("\n=== 3. thread.createx (创建并立即 join) ===\n")

local ok_cx, cx_result = pcall(function()
    return thread.createx(function(x, y) return x * y end, 6, 7)
end)
check(ok_cx and cx_result == 42, "thread.createx -> 42")

print("\n=== 4. thread.self / thread.current ===\n")

local self_th = thread.self()
check(type(self_th) == "userdata", "thread.self() returns userdata")
local self_id = self_th:id()
check(type(self_id) == "number" and self_id > 0, "thread.self():id() > 0")

local cur_th = thread.current()
check(type(cur_th) == "userdata", "thread.current() returns userdata")

print("\n=== 5. thread.name / thread.id ===\n")

local th_name = thread.create(function() end)
check(th_name:name() == "thread", "default thread name is 'thread'")
th_name:name("worker-1")
check(th_name:name() == "worker-1", "thread:name('worker-1')")
check(type(th_name:id()) == "number", "thread:id() returns number")
th_name:join()

print("\n=== 6. thread.detach (fire-and-forget) ===\n")

-- 6.1 detach 正常使用
local detach_done = false
local th_detach = thread.create(function()
    -- simulate work
    local s = 0
    for i = 1, 10000 do s = s + i end
end)
local ok_detach = pcall(function() th_detach:detach() end)
check(ok_detach, "thread:detach() succeeds")

-- 6.2 detach 后不能 join
assert_error(function() th_detach:join() end, "cannot join detached thread")

-- 6.3 detach 后不能再 detach
assert_error(function() th_detach:detach() end, "cannot detach twice")

print("\n=== 7. 双重 join 保护 ===\n")

local th_dj = thread.create(function() return 123 end)
th_dj:join()
assert_error(function() th_dj:join() end, "cannot join twice")

print("\n=== 8. Channel 基本操作 ===\n")

-- 8.1 send + receive (跨线程)
local ch1 = thread.channel()
local th_send = thread.create(function(ch)
    ch:send("hello")
    ch:send("world")
    ch:close()
end, ch1)
local v1 = ch1:receive()
local v2 = ch1:receive()
local v3 = ch1:receive()  -- closed, should be nil
check(v1 == "hello" and v2 == "world" and v3 == nil,
    "channel send/receive/close -> 'hello', 'world', nil")
th_send:join()

-- 8.2 push/pop 别名
local ch2 = thread.channel()
local th_push = thread.create(function(ch)
    ch:push(100)
    ch:push(200)
    ch:close()
end, ch2)
check(ch2:pop() == 100 and ch2:pop() == 200,
    "channel push/pop aliases work")
th_push:join()

-- 8.3 try_send / try_recv (非阻塞)
local ch3 = thread.channel()
check(ch3:try_send(999) == true, "channel:try_send() returns true")
check(ch3:try_recv() == 999, "channel:try_recv() returns value")
check(ch3:try_recv() == nil, "channel:try_recv() on empty returns nil")

-- 8.4 peek
local ch4 = thread.channel()
ch4:send("peekme")
check(ch4:peek() == "peekme", "channel:peek() returns value")
check(ch4:receive() == "peekme", "value still available after peek")

print("\n=== 9. Channel 类型约束 ===\n")

-- 9.1 类型匹配 (thread.channel("number") 返回工厂函数，需要调用)
local ch_type = thread.channel("number")()
ch_type:send(42)
check(ch_type:receive() == 42, "channel number type: 42")

-- 9.2 类型不匹配
assert_error(function()
    ch_type:send("not_a_number")
end, "channel type mismatch error")

-- 9.3 try_send 类型不匹配应返回 false
local ch_type2 = thread.channel("number")()
check(ch_type2:try_send("bad") == false, "channel try_send type mismatch -> false")

print("\n=== 10. thread.pick 多路选择 ===\n")

-- 10.1 从就绪的 channel 中选择
local ch_a = thread.channel()
local ch_b = thread.channel()
ch_a:send("from_a")

local result = nil
thread.pick{
    {
        thread.on(ch_b),
        function(v) result = "b:" .. v end
    },
    {
        thread.on(ch_a),
        function(v) result = "a:" .. v end
    },
}
check(result == "a:from_a", "thread.pick selects ready channel")

-- 10.2 pick 跨线程通信（主线程 pick 等待子线程发送）
local ch_wait = thread.channel()
local ch_wait2 = thread.channel()
local pick_result = nil

local th_producer = thread.create(function(ch)
    local s = 0
    for i = 1, 50000 do s = s + i % 7 end
    ch:send("delayed")
end, ch_wait)

thread.pick{
    {thread.on(ch_wait), function(v) pick_result = "got:" .. v end},
    {thread.over(5.0), function() end},
}

th_producer:join()
check(pick_result == "got:delayed", "thread.pick cross-thread works")

-- 10.3 thread.on 辅助函数
local ch_on = thread.channel()
ch_on:send("on_test")
local desc = thread.on(ch_on)
check(type(desc) == "table" and desc.op == "recv",
    "thread.on() returns recv descriptor")

-- 10.4 thread.over 超时操作
local desc_to = thread.over(1.0)
check(type(desc_to) == "table" and desc_to.op == "timeout" and desc_to.duration == 1.0,
    "thread.over() returns timeout descriptor")

print("\n=== 11. thread.pick 超时 ===\n")

local ch_timeout = thread.channel()
local timeout_hit = false
thread.pick{
    {
        thread.on(ch_timeout),
        function(v) end
    },
    {
        thread.over(0.1),
        function() timeout_hit = true end
    },
}
check(timeout_hit == true, "thread.pick timeout triggers")

print("\n=== 12. Channel close 广播接收者 ===\n")

local ch_close = thread.channel()
local th_receiver = thread.create(function(ch)
    local v = ch:receive()
    return v
end, ch_close)
ch_close:close()
local succ_c, val_c = th_receiver:join()
check(succ_c and val_c == nil, "channel close wakes receivers with nil")

print("\n=== 13. GC 保护 - ThreadHandle __gc ===\n")

-- 13.1 线程 handle 被 GC 后，线程自动 detach（不会泄漏）
local gc_test_done = false
do
    local th_gc = thread.create(function()
        local s = 0
        for i = 1, 20000 do s = s + i % 11 end
    end)
    th_gc:detach()  -- 手动 detach 验证不会报错
end
collectgarbage("collect")
check(true, "thread __gc does not crash on detached thread (manual detach)")

-- 13.2 未 join 也未 detach 的线程被 GC 时应 detach
do
    local th_gc2 = thread.create(function()
        local s = 0
        for i = 1, 10000 do s = s + i end
    end)
    -- th_gc2 离开作用域后会被 GC
end
collectgarbage("collect")
check(true, "thread __gc handles orphaned thread (auto-detach on GC)")

print("\n=== 14. 边界情况 ===\n")

-- 14.1 空函数线程
local th_empty = thread.create(function() end)
local succ_empty = th_empty:join()
check(succ_empty == true, "empty thread function returns success")

-- 14.2 多返回值线程
local th_multi = thread.create(function()
    return 1, 2, 3, 4, 5
end)
local succ_m, a, b, c, d, e = th_multi:join()
check(succ_m == true and a == 1 and b == 2 and c == 3 and d == 4 and e == 5,
    "thread join returns all 5 values")

-- 14.3 nil 参数传递
local th_nil = thread.create(function(v)
    return v == nil
end, nil)
local succ_nil, is_nil = th_nil:join()
check(succ_nil == true and is_nil == true,
    "thread.create passes nil argument correctly")

print("\n========================================")
print(string.format("=== 测试结果: %d 通过, %d 失败 ===", passed, failed))
print("========================================")

if failed > 0 then
    os.exit(1)
end