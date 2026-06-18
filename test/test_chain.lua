-- 链式比较测试
local function check(desc, actual, expected)
  if actual == expected then
    print("PASS: " .. desc)
  else
    print("FAIL: " .. desc .. " (got " .. tostring(actual) .. ", expected " .. tostring(expected) .. ")")
  end
end

-- 基本两段链式
check("1 < 2 < 3",           1 < 2 < 3,           true)
check("1 < 2 < 1",           1 < 2 < 1,           false)
check("1 > 3 < 5",           1 > 3 < 5,           false)
check("1 == 2 < 5",          1 == 2 < 5,          false)
check("1 ~= 1 < 5",          1 ~= 1 < 5,          false)
check("3 > 2 > 1",           3 > 2 > 1,           true)
check("3 > 2 > 4",           3 > 2 > 4,           false)

-- 三段链式
check("1 < 2 < 3 < 4",       1 < 2 < 3 < 4,       true)
check("1 < 2 < 3 < 0",       1 < 2 < 3 < 0,       false)
check("1 < 5 < 3 < 4",       1 < 5 < 3 < 4,       false)
check("5 > 3 > 2 > 1",       5 > 3 > 2 > 1,       true)
check("5 > 3 > 4 > 1",       5 > 3 > 4 > 1,       false)

-- 混合 <= 和 >=
check("1 <= 2 <= 3",         1 <= 2 <= 3,         true)
check("1 <= 1 <= 1",         1 <= 1 <= 1,         true)
check("1 <= 0 <= 3",         1 <= 0 <= 3,         false)
check("3 >= 2 >= 1",         3 >= 2 >= 1,         true)
check("3 >= 3 >= 3",         3 >= 3 >= 3,         true)

-- 混合不同比较符
check("1 < 3 > 2",           1 < 3 > 2,           true)
check("1 < 3 > 5",           1 < 3 > 5,           false)
check("5 > 3 < 4",           5 > 3 < 4,           true)

-- 变量参与
local a, b, c = 1, 5, 10
check("a < b < c",           a < b < c,           true)
check("a < b < 1",           a < b < 1,           false)
check("c > b > a",           c > b > a,           true)

-- 与普通表达式对比
check("(1 < 2) and (2 < 3)", (1 < 2) and (2 < 3), true)
check("(1 > 3) and (3 < 5)", (1 > 3) and (3 < 5), false)

print("\nDone!")