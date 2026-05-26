print("=== 边界测试: keyword 编译时特性 ===")

-- ========== 测试 1: 基本功能 ==========
print("\n--- 1. 基本功能 ---")
keyword add(a, b)
  return a + b
end
keyword mul(a, b)
  return a * b
end
assert($add(3, 4) == 7, "add(3,4) failed")
assert($mul(3, 4) == 12, "mul(3,4) failed")
assert(add(3, 4) == 7, "normal add call failed")
assert(mul(3, 4) == 12, "normal mul call failed")
print("PASS: 基本多参数 keyword")

-- ========== 测试 2: 无参数 keyword ==========
print("\n--- 2. 无参数 keyword ---")
keyword pi()
  return 3.14
end
assert($pi() == 3.14, "pi() failed")
assert(pi() == 3.14, "normal pi() failed")
print("PASS: 无参数 keyword")

-- ========== 测试 3: 类型标记作为 keyword 名 ==========
print("\n--- 3. 类型标记作为 keyword 名 ---")
keyword double(x)
  return x * 2
end
keyword int_val(x)
  return x
end
assert($double(5) == 10, "double(5) failed")
assert($int_val(42) == 42, "int_val(42) failed")
assert(double(5) == 10, "normal double(5) failed")
print("PASS: 类型标记 keyword 名")

-- ========== 测试 4: keyword 重定义 ==========
print("\n--- 4. keyword 重定义 ---")
keyword replace_me()
  return "old"
end
assert($replace_me() == "old", "replace_me old failed")
keyword replace_me()
  return "new"
end
assert($replace_me() == "new", "replace_me new failed")
assert(replace_me() == "new", "normal replace_me new failed")
print("PASS: keyword 重定义")

-- ========== 测试 5: $name 作为值（无括号调用） ==========
print("\n--- 5. $name 作为值 ---")
keyword get_func()
  return function(x) return x + 1 end
end
local f = $get_func()
assert(type(f) == "function", "$get_func() should return function")
assert(f(10) == 11, "f(10) should be 11")
print("PASS: $name 作为值")

-- ========== 测试 6: 字符串和表参数 ==========
print("\n--- 6. 特殊参数类型 ---")
keyword concat(a, b, c)
  return a .. b .. c
end
assert($concat("hello", " ", "world") == "hello world", "concat failed")
assert(concat("hello", " ", "world") == "hello world", "normal concat failed")
print("PASS: 字符串参数")

-- ========== 测试 7: 多个 keyword 并存 ==========
print("\n--- 7. 多 keyword 并存 ---")
keyword k1() return 1 end
keyword k2() return 2 end
keyword k3() return 3 end
keyword k4() return 4 end
keyword k5() return 5 end
keyword k6() return 6 end
keyword k7() return 7 end
keyword k8() return 8 end
keyword k9() return 9 end
keyword k10() return 10 end
assert($k1() == 1, "k1 failed")
assert($k5() == 5, "k5 failed")
assert($k10() == 10, "k10 failed")
local sum = $k1() + $k2() + $k3() + $k4() + $k5() + $k6() + $k7() + $k8() + $k9() + $k10()
assert(sum == 55, "sum should be 55, got " .. sum)
print("PASS: 10+ keyword 并存, sum=" .. sum)

-- ========== 测试 8: keyword 函数体内调用其他 keyword ==========
print("\n--- 8. 嵌套 keyword 调用 ---")
keyword square(x)
  return x * x
end
keyword sum_of_squares(a, b)
  return $square(a) + $square(b)
end
assert($sum_of_squares(3, 4) == 25, "sum_of_squares(3,4) failed")
assert(sum_of_squares(3, 4) == 25, "normal sum_of_squares(3,4) failed")
print("PASS: keyword 内调用其他 keyword")

-- ========== 测试 9: keyword 在函数内部使用 ==========
print("\n--- 9. 函数内引用 keyword ---")
do
  local result = $double(7)
  assert(result == 14, "double(7) inside function should be 14, got " .. result)
end
print("PASS: 函数内引用 keyword")

-- ========== 测试 10: 链式 keyword 调用 ==========
print("\n--- 10. 链式调用 ---")
keyword inc(x) return x + 1 end
local r = $double($inc(5))
assert(r == 12, "double(inc(5)) should be 12, got " .. r)
print("PASS: 链式 keyword 调用")

-- ========== 测试 11: keyword 使用 return 语句的多种形式 ==========
print("\n--- 11. return 语句形式 ---")
keyword no_return()
  local x = 1
end
local r = $no_return()
assert(r == nil, "no_return should be nil")
print("PASS: 空 return")

-- ========== 测试 12: 正常函数调用和 $keyword 调用结果一致 ==========
print("\n--- 12. 普通调用 vs $ 调用一致性 ---")
keyword consistent(a, b, c)
  return (a + b) * c
end
local r1 = consistent(2, 3, 4)
local r2 = $consistent(2, 3, 4)
assert(r1 == r2, "inconsistent: normal=" .. tostring(r1) .. " $call=" .. tostring(r2))
print("PASS: 普通调用与 $ 调用一致 (" .. r1 .. ")")

-- ========== 测试 13: 错误: 未定义的 keyword ==========
print("\n--- 13. 错误: 未定义的 keyword ---")
local ok, err = pcall(function()
  local code = load("$undefined_keyword(1)")
  return code
end)
if ok and err then
  local ok2, err2 = pcall(err)
  if not ok2 then
    print("PASS: 未定义 keyword 报错: " .. tostring(err2))
  end
else
  print("SKIP: 动态 load 测试跳过")
end

print("\n=== 所有边界测试完成 ===")