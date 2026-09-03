-- match 语句测试用例
-- 测试 1: 通配符 _
print("=== Test 1: 通配符 _ ===")
local r1 = match 1 do
  case _ => "any"
end
print(r1)  -- 期望: any

-- 测试 2: 字面量匹配
print("=== Test 2: 字面量匹配 ===")
local r2 = match 2 do
  case 1 => "one"
  case 2 => "two"
  case _ => "other"
end
print(r2)  -- 期望: two

-- 测试 3: 字符串匹配
print("=== Test 3: 字符串匹配 ===")
local r3 = match "hello" do
  case "world" => "no"
  case "hello" => "yes"
  case _ => "default"
end
print(r3)  -- 期望: yes

-- 测试 4: 守卫条件 if
print("=== Test 4: 守卫条件 ===")
local r4 = match 5 do
  case n if n > 3 => "big"
  case n if n > 0 => "small"
  case _ => "zero"
end
print(r4)  -- 期望: big

-- 测试 5: 变量绑定
print("=== Test 5: 变量绑定 ===")
local r5 = match 10 do
  case v => v * 2
end
print(r5)  -- 期望: 20

-- 测试 6: 多 case 臂
print("=== Test 6: 多 case 臂 ===")
local r6 = match 3 do
  case 1 => "one"
  case 2 => "two"
  case 3 => "three"
  case 4 => "four"
  case _ => "default"
end
print(r6)  -- 期望: three

-- 测试 7: then 分隔符
print("=== Test 7: then 分隔符 ===")
local r7 = match 42 then
  case 42 => "answer"
  case _ => "unknown"
end
print(r7)  -- 期望: answer

-- 测试 8: 花括号语法
print("=== Test 8: 花括号语法 ===")
local r8 = match 1 {
  case 1 => "one"
  case _ => "other"
}
print(r8)  -- 期望: one

-- 测试 9: 多个守卫条件
print("=== Test 9: 多个守卫条件 ===")
local r9 = match 8 do
  case n if n < 5 => "low"
  case n if n < 10 => "mid"
  case n if n < 20 => "high"
  case _ => "very high"
end
print(r9)  -- 期望: mid

-- 测试 10: nil 匹配
print("=== Test 10: nil 匹配 ===")
local r10 = match nil do
  case nil => "nil"
  case _ => "not nil"
end
print(r10)  -- 期望: nil

-- 测试 11: bool 匹配
print("=== Test 11: bool 匹配 ===")
local r11 = match true do
  case true => "yes"
  case false => "no"
end
print(r11)  -- 期望: yes

-- 测试 12: 浮点数匹配
print("=== Test 12: 浮点数匹配 ===")
local r12 = match 3.14 do
  case 3.14 => "pi"
  case _ => "unknown"
end
print(r12)  -- 期望: pi

-- 测试 13: 守卫条件 and/or/not
print("=== Test 13: 守卫条件 and/or/not ===")
local r13 = match 5 do
  case n if n > 3 and n < 10 => "range"
  case _ => "other"
end
print(r13)  -- 期望: range

local r13b = match 5 do
  case n if n < 3 or n > 10 => "extreme"
  case n if n == 5 => "five"
  case _ => "other"
end
print(r13b)  -- 期望: five

local r13c = match 5 do
  case n if not (n < 0) => "nonneg"
  case _ => "neg"
end
print(r13c)  -- 期望: nonneg

-- 测试 14: 守卫条件字符串长度
print("=== Test 14: 守卫条件字符串长度 ===")
local r14 = match "hello" do
  case s if #s > 3 => "long"
  case _ => "short"
end
print(r14)  -- 期望: long

-- 测试 15: 守卫条件算术运算
print("=== Test 15: 守卫条件算术运算 ===")
local r15 = match 6 do
  case n if n * 2 > 10 => "big"
  case _ => "small"
end
print(r15)  -- 期望: big

-- 测试 16: 块体形式
print("=== Test 16: 块体形式 ===")
local r16 = nil
match 5 do
  case n:
    r16 = n * 2
end
print(r16)  -- 期望: 10

-- 测试 17: 守卫条件子表达式
print("=== Test 17: 守卫条件子表达式 ===")
local r17 = match 5 do
  case n if (n + 1) * 2 == 12 => "twelve"
  case _ => "other"
end
print(r17)  -- 期望: twelve

print("=== All match tests completed ===")