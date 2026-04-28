-- 测试混淆器的脚本
local tcc = require "tcc"

-- 测试1: 简单函数
print("=== 测试1: 简单函数 ===")
local code1 = [[
function add(a, b)
    return a + b
end
local result = add(3, 5)
print("3 + 5 = " .. result)
]]

local ok, err = pcall(function()
    local c_code1 = tcc.compile(code1, {obfuscate = true}, "test_obf1")
    if c_code1 then
        print("混淆成功!")
        print("输出长度: " .. #c_code1)
    else
        print("混淆失败: " .. tostring(c_code1))
    end
end)
if not ok then print("错误: " .. tostring(err)) end

-- 测试2: 带条件分支的函数
print("\n=== 测试2: 条件分支 ===")
local code2 = [[
function max(a, b)
    if a > b then
        return a
    else
        return b
    end
end
local m = max(10, 20)
print("max(10, 20) = " .. m)
]]

ok, err = pcall(function()
    local c_code2 = tcc.compile(code2, {obfuscate = true}, "test_obf2")
    if c_code2 then
        print("混淆成功!")
    else
        print("混淆失败: " .. tostring(c_code2))
    end
end)
if not ok then print("错误: " .. tostring(err)) end

-- 测试3: 循环
print("\n=== 测试3: 循环 ===")
local code3 = [[
local sum = 0
for i = 1, 10 do
    sum = sum + i
end
print("1+2+...+10 = " .. sum)
]]

ok, err = pcall(function()
    local c_code3 = tcc.compile(code3, {obfuscate = true}, "test_obf3")
    if c_code3 then
        print("混淆成功!")
    else
        print("混淆失败: " .. tostring(c_code3))
    end
end)
if not ok then print("错误: " .. tostring(err)) end

-- 测试4: 复杂控制流
print("\n=== 测试4: 复杂控制流 ===")
local code4 = [[
function fibonacci(n)
    if n <= 1 then
        return n
    end
    local a, b = 0, 1
    for i = 2, n do
        a, b = b, a + b
    end
    return b
end
print("fibonacci(10) = " .. fibonacci(10))
]]

ok, err = pcall(function()
    local c_code4 = tcc.compile(code4, {obfuscate = true}, "test_obf4")
    if c_code4 then
        print("混淆成功!")
    else
        print("混淆失败: " .. tostring(c_code4))
    end
end)
if not ok then print("错误: " .. tostring(err)) end

print("\n=== 测试完成 ===")
