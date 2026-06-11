-- 测试1: 原始报告的问题
local jsej = {}
local ll = 'key'
local jwap = {}
function jwap:aeks(a, b)
    print('aeks called:', a, b)
end
local sksj = 1
local ekek = 2
jsej[ll] = jwap aeks(sksj, ekek)
print('Test1 PASS')

-- 测试2: 对比
local result2 = jwap aeks(10, 20)
print('Test2 PASS')

-- 测试3: 无括号的单参数 infix
function jwap:test(x) return x * 2 end
local r3 = jwap test(5)
print('Test3 PASS:', r3)

-- 测试4: 带括号的空参数
function jwap:foo() return 'empty' end
local r4 = jwap foo()
print('Test4 PASS:', r4)

-- 测试5: 三参数 infix
function jwap:add3(a, b, c) return a + b + c end
local r5 = jwap add3(1, 2, 3)
print('Test5 PASS:', r5)

-- 测试6: 不带括号的 infix
function jwap:mul(x) return x * 3 end
local r6 = jwap mul(7)
print('Test6 PASS:', r6)

-- 测试7: 单参数带括号但无逗号
function jwap:neg(x) return -x end
local r7 = jwap neg((5))
print('Test7 PASS:', r7)

print('All tests passed!')