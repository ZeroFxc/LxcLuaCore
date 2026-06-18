-- 测试范围操作符 1..10
local function check(name, a, b)
  if type(a) == "table" and type(b) == "table" then
    local ok = #a == #b
    if ok then
      for i = 1, #a do
        if a[i] ~= b[i] then ok = false; break end
      end
    end
    local status = ok and "PASS" or "FAIL"
    print(string.format("[%s] %s", status, name))
    if not ok then
      print("  expected: " .. table.concat(b, ", "))
      print("  got:      " .. table.concat(a, ", "))
    end
  else
    local ok = a == b
    local status = ok and "PASS" or "FAIL"
    print(string.format("[%s] %s", status, name))
    if not ok then
      print("  expected: " .. tostring(b))
      print("  got:      " .. tostring(a))
    end
  end
end

-- 测试1: 基本范围 1..5
local r1 = 1..5
local expected1 = {1, 2, 3, 4, 5}
check("1..5", r1, expected1)

-- 测试2: 范围 0..3
local r2 = 0..3
local expected2 = {0, 1, 2, 3}
check("0..3", r2, expected2)

-- 测试3: 范围 -3..3
local r3 = -3..3
local expected3 = {-3, -2, -1, 0, 1, 2, 3}
check("-3..3", r3, expected3)

-- 测试4: 单元素范围 5..5
local r4 = 5..5
local expected4 = {5}
check("5..5", r4, expected4)

-- 测试5: 范围 10..20
local r5 = 10..20
local expected5 = {}
for i = 10, 20 do expected5[#expected5 + 1] = i end
check("10..20", r5, expected5)

-- 测试6: 在for循环中使用
local sum = 0
for _, v in ipairs(1..10) do
  sum = sum + v
end
check("for loop sum 1..10", sum, 55)

-- 测试7: 反向范围 5..1 (空范围，回退到字符串拼接)
local r7 = 5..1
check("5..1 (string concat)", r7, "51")

-- 测试8: 字符串拼接仍然正常
local r8 = "hello" .. " world"
check("string concat", r8, "hello world")

-- 测试9: 变量拼接仍正常
local a, b = 1, 5
local r9 = a .. b
check("variable concat", r9, "15")

print("\n范围操作符测试完成!")