-- JIT 循环详细测试
jit.on()

local passed = 0
local failed = 0

local function check(name, actual, expected)
    if actual == expected then
        passed = passed + 1
        print(string.format("  [OK] %s = %d", name, actual))
    else
        failed = failed + 1
        print(string.format("  [FAIL] %s = %d, expected %d", name, actual, expected))
    end
end

-- ===== 测试1: 基本累加 =====
print("=== 测试1: 基本累加 sum(n) ===")
local function sum(n)
    local s = 0
    for i = 1, n do s = s + i end
    return s
end
sum(1)  -- 预热
for _, n in ipairs({1, 2, 3, 5, 10, 100, 1000}) do
    check("sum(" .. n .. ")", sum(n), n * (n + 1) / 2)
end

-- ===== 测试2: 递减循环 =====
print("\n=== 测试2: 递减循环 ===")
local function sum_rev(n)
    local s = 0
    for i = n, 1, -1 do s = s + i end
    return s
end
sum_rev(1)
for _, n in ipairs({1, 2, 3, 5, 10, 100}) do
    check("sum_rev(" .. n .. ")", sum_rev(n), n * (n + 1) / 2)
end

-- ===== 测试3: 步长大于1 =====
print("\n=== 测试3: 步长 > 1 ===")
local function sum_step(n, step)
    local s = 0
    for i = 1, n, step do s = s + i end
    return s
end
sum_step(1, 2)
check("sum_step(10, 2)", sum_step(10, 2), 1 + 3 + 5 + 7 + 9)
check("sum_step(10, 3)", sum_step(10, 3), 1 + 4 + 7 + 10)
check("sum_step(20, 5)", sum_step(20, 5), 1 + 6 + 11 + 16)

-- ===== 测试4: 嵌套循环 =====
print("\n=== 测试4: 嵌套循环 ===")
local function nested(n)
    local s = 0
    for i = 1, n do
        for j = 1, i do
            s = s + 1
        end
    end
    return s
end
nested(1)
check("nested(1)", nested(1), 1)
check("nested(3)", nested(3), 6)
check("nested(5)", nested(5), 15)
check("nested(10)", nested(10), 55)

-- ===== 测试5: 循环内乘法 =====
print("\n=== 测试5: 循环内乘法 ===")
local function factorial(n)
    local r = 1
    for i = 2, n do r = r * i end
    return r
end
factorial(1)
check("factorial(1)", factorial(1), 1)
check("factorial(3)", factorial(3), 6)
check("factorial(5)", factorial(5), 120)
check("factorial(10)", factorial(10), 3628800)

-- ===== 测试6: 循环内条件判断 =====
print("\n=== 测试6: 循环内条件判断 ===")
local function sum_even(n)
    local s = 0
    for i = 1, n do
        if i % 2 == 0 then s = s + i end
    end
    return s
end
sum_even(1)
check("sum_even(10)", sum_even(10), 2 + 4 + 6 + 8 + 10)
check("sum_even(11)", sum_even(11), 2 + 4 + 6 + 8 + 10)

-- ===== 测试7: 提前 break =====
print("\n=== 测试7: 循环内 break ===")
local function sum_until(n, limit)
    local s = 0
    for i = 1, n do
        s = s + i
        if s >= limit then break end
    end
    return s
end
sum_until(1, 1)
check("sum_until(10, 10)", sum_until(10, 10), 10)
check("sum_until(10, 20)", sum_until(10, 20), 21)

-- ===== 测试8: 大循环 (性能验证) =====
print("\n=== 测试8: 大循环 ===")
local function sum_large(n)
    local s = 0
    for i = 1, n do s = s + i end
    return s
end
sum_large(1)
local n = 100000
local expected = n * (n + 1) / 2
check("sum_large(100000)", sum_large(n), expected)

-- ===== 测试9: 多次调用同一函数 =====
print("\n=== 测试9: 多次调用同一函数 ===")
local function accum(n)
    local s = 0
    for i = 1, n do s = s + i end
    return s
end
for i = 1, 5 do
    accum(10)
end
check("accum(10) after 5 calls", accum(10), 55)

-- ===== 测试10: 循环内多变量更新 =====
print("\n=== 测试10: 循环内多变量更新 ===")
local function multi_update(n)
    local a, b = 0, 0
    for i = 1, n do
        a = a + i
        b = b + i * 2
    end
    return a, b
end
multi_update(1)
local a, b = multi_update(5)
check("multi_update(5) a", a, 15)
check("multi_update(5) b", b, 30)

-- ===== 结果汇总 =====
print("\n========== 结果汇总 ==========")
print(string.format("通过: %d, 失败: %d", passed, failed))
if failed == 0 then
    print("全部测试通过!")
else
    print("存在失败用例!")
end