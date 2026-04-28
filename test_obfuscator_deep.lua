-- 深入测试混淆器 - 验证混淆后代码的正确性
local tcc = require "tcc"

print("=== 混淆器正确性测试 ===\n")

-- 测试1: 简单函数执行
print("--- 测试1: 简单函数 ---")
local code1 = [[
function add(a, b)
    return a + b
end
return add(3, 5)
]]

local c_code1, loadfn1 = tcc.compile(code1, {obfuscate = true}, "test_exec1")
if c_code1 and loadfn1 then
    local result = loadfn1()
    print("add(3, 5) = " .. tostring(result))
    if result == 8 then
        print("✓ 测试1通过")
    else
        print("✗ 测试1失败! 期望8, 得到" .. tostring(result))
    end
else
    print("✗ 测试1编译失败")
end

-- 测试2: 条件分支
print("\n--- 测试2: 条件分支 ---")
local code2 = [[
function max(a, b)
    if a > b then
        return a
    else
        return b
    end
end
return max(10, 20), max(20, 10)
]]

local c_code2, loadfn2 = tcc.compile(code2, {obfuscate = true}, "test_exec2")
if c_code2 and loadfn2 then
    local r1, r2 = loadfn2()
    print("max(10, 20) = " .. tostring(r1) .. ", max(20, 10) = " .. tostring(r2))
    if r1 == 20 and r2 == 20 then
        print("✓ 测试2通过")
    else
        print("✗ 测试2失败! 期望20,20, 得到" .. tostring(r1) .. "," .. tostring(r2))
    end
else
    print("✗ 测试2编译失败")
end

-- 测试3: 循环
print("\n--- 测试3: 循环 ---")
local code3 = [[
local sum = 0
for i = 1, 10 do
    sum = sum + i
end
return sum
]]

local c_code3, loadfn3 = tcc.compile(code3, {obfuscate = true}, "test_exec3")
if c_code3 and loadfn3 then
    local result = loadfn3()
    print("1+2+...+10 = " .. tostring(result))
    if result == 55 then
        print("✓ 测试3通过")
    else
        print("✗ 测试3失败! 期望55, 得到" .. tostring(result))
    end
else
    print("✗ 测试3编译失败")
end

-- 测试4: while循环
print("\n--- 测试4: while循环 ---")
local code4 = [[
local i = 1
local sum = 0
while i <= 5 do
    sum = sum + i * i
    i = i + 1
end
return sum
]]

local c_code4, loadfn4 = tcc.compile(code4, {obfuscate = true}, "test_exec4")
if c_code4 and loadfn4 then
    local result = loadfn4()
    print("1^2+2^2+...+5^2 = " .. tostring(result))
    if result == 55 then
        print("✓ 测试4通过")
    else
        print("✗ 测试4失败! 期望55, 得到" .. tostring(result))
    end
else
    print("✗ 测试4编译失败")
end

-- 测试5: repeat-until循环
print("\n--- 测试5: repeat-until循环 ---")
local code5 = [[
local x = 10
repeat
    x = x - 1
until x <= 5
return x
]]

local c_code5, loadfn5 = tcc.compile(code5, {obfuscate = true}, "test_exec5")
if c_code5 and loadfn5 then
    local result = loadfn5()
    print("repeat until 结果 = " .. tostring(result))
    if result == 5 then
        print("✓ 测试5通过")
    else
        print("✗ 测试5失败! 期望5, 得到" .. tostring(result))
    end
else
    print("✗ 测试5编译失败")
end

-- 测试6: 嵌套条件
print("\n--- 测试6: 嵌套条件 ---")
local code6 = [[
function classify(n)
    if n < 0 then
        return "negative"
    elseif n == 0 then
        return "zero"
    elseif n < 100 then
        return "small"
    else
        return "large"
    end
end
return classify(-5), classify(0), classify(50), classify(200)
]]

local c_code6, loadfn6 = tcc.compile(code6, {obfuscate = true}, "test_exec6")
if c_code6 and loadfn6 then
    local r1, r2, r3, r4 = loadfn6()
    print("classify结果: " .. tostring(r1) .. ", " .. tostring(r2) .. ", " .. tostring(r3) .. ", " .. tostring(r4))
    if r1 == "negative" and r2 == "zero" and r3 == "small" and r4 == "large" then
        print("✓ 测试6通过")
    else
        print("✗ 测试6失败!")
    end
else
    print("✗ 测试6编译失败")
end

-- 测试7: for循环与break
print("\n--- 测试7: for循环与break ---")
local code7 = [[
local found = nil
for i = 1, 20 do
    if i * i >= 50 then
        found = i
        break
    end
end
return found
]]

local c_code7, loadfn7 = tcc.compile(code7, {obfuscate = true}, "test_exec7")
if c_code7 and loadfn7 then
    local result = loadfn7()
    print("break测试结果 = " .. tostring(result))
    if result == 8 then
        print("✓ 测试7通过")
    else
        print("✗ 测试7失败! 期望8, 得到" .. tostring(result))
    end
else
    print("✗ 测试7编译失败")
end

-- 测试8: 逻辑运算符短路
print("\n--- 测试8: 逻辑运算符 ---")
local code8 = [[
local a = false or "default"
local b = true and "yes"
local c = false and "no" or "fallback"
return a, b, c
]]

local c_code8, loadfn8 = tcc.compile(code8, {obfuscate = true}, "test_exec8")
if c_code8 and loadfn8 then
    local r1, r2, r3 = loadfn8()
    print("逻辑运算结果: " .. tostring(r1) .. ", " .. tostring(r2) .. ", " .. tostring(r3))
    if r1 == "default" and r2 == "yes" and r3 == "fallback" then
        print("✓ 测试8通过")
    else
        print("✗ 测试8失败!")
    end
else
    print("✗ 测试8编译失败")
end

print("\n=== 所有测试完成 ===")
