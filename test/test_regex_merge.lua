-- 表合并：不重叠数组
local a = {1, 2, 3}
local b = {x=10, y=20}
local c = a <> b
print("数组部分: " .. table.concat(c, ","))
print("x: " .. c.x .. " y: " .. c.y)

-- 表合并：嵌套
local d = {a=1, b={inner=2}}
local e = {b={outer=3}, c=4}
local f = d <> e
print("f.a: " .. f.a .. " f.c: " .. f.c)

-- 正则：作为参数
local function describe(r)
    print("匹配模式: " .. r.pattern .. " flag: " .. r.flags)
end
describe(/hello/i)

-- 正则：表达式上下文
local x = /[0-9]+/
local y = x
print("y.pattern: " .. y.pattern)

-- 除法 vs 正则区分
local a2 = 10
local b2 = 2
local div = a2 / b2
print("除法: " .. div)

-- 正则：复杂转义
local r3 = /\w+\s+\d+/
print("r3.pattern: " .. r3.pattern)

print("\n所有测试通过!")