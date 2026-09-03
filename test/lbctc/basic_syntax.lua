-- Lua 5.x 基础语法覆盖
print("=== basic syntax start ===")
-- 变量、算术、逻辑
local a, b = 1, 2
print(a + b, a * b, a - b, b / a)
print(a > b, a < b, a == b, a ~= b)
print(true and false, true or false, not false)
-- 字符串
local s = "hello" .. " world"
print(s, #s, string.upper(s))
-- 表
local t = {x = 10, y = 20, [3] = "three"}
t.z = 30
print(t.x, t[3], #t)
-- 分支
if a > b then print("a>b") elseif a < b then print("a<b") else print("eq") end
-- while / repeat
local i = 1
while i <= 3 do print("w", i); i = i + 1 end
i = 1
repeat print("r", i); i = i + 1 until i > 3
-- for 数值 / 泛型
for j = 1, 3 do print("fn", j) end
for k, v in pairs({a=1,b=2}) do print("fp", k, v) end
-- 函数、闭包
local function add(x, y) return x + y end
print(add(a, b))
local mk = function(n) return function(x) return x + n end end
print(mk(10)(5))
-- 多返回值、变长参数
local function m(a, ...) return a, ... end
local x, y, z = m(1, 2, 3)
print(x, y, z)
-- 元表
local t2 = setmetatable({}, {__add = function(a,b) return 42 end})
print(t2 + t2)
-- 错误处理
local ok, err = pcall(function() error("oops") end)
print(ok, err)
-- 协程
local co = coroutine.create(function(a, b)
  coroutine.yield(a + b)
  return "done"
end)
local st, r1 = coroutine.resume(co, 2, 3)
print(st, r1)
st, r1 = coroutine.resume(co)
print(st, r1)
print("=== basic syntax end ===")
