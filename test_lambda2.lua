-- ===== 常见错误用法 vs 正确用法 =====

-- Ôö 错误: => 后不能用表达式 (必须用 : 或 return)
-- local wrong = lambda x => x + 1

-- Üì 正确: => 后必须跟 return 或完整的语句
local correct1 = lambda x => return x + 1
local correct2 = lambda x => print("got", x)
print(correct1(100))
correct2(200)

-- Üì 正确: 用 : 支持表达式隐式return
local correct3 = lambda x: x * 3
print(correct3(10))

-- ===== 带默认参数的lambda =====
local greet = lambda name = "World" => return "Hello, " .. name
print(greet())          --> Hello, World
print(greet("Lua"))     --> Hello, Lua

-- ===== vararg lambda (语句体) =====
local sumall = lambda (...) =>
    local s = 0
    for _, v in ipairs({...}) do
        s = s + v
    end
    return s
print(sumall(1, 2, 3, 4, 5))

-- ===== lambda作为table字段 =====
local ops = {
    add = lambda (a, b): a + b,
    sub = lambda (a, b): a - b,
}
print(ops.add(10, 5))
print(ops.sub(10, 5))
