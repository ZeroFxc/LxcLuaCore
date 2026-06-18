-- 测试解构赋值（含默认值）

print("=== 测试1: 解构赋值（无默认值，原有功能） ===")
do
  local t = {a = 1, b = 2}
  local {a, b} = t
  print(a, b)
  assert(a == 1 and b == 2, "test1 failed")
  print("PASS")
end

print("=== 测试2: 解构赋值（字符串默认值） ===")
do
  local t = {a = 1, b = 2}
  local {a, b, name = "default"} = t
  print(a, b, name)
  assert(a == 1 and b == 2 and name == "default", "test2 failed")
  print("PASS")
end

print("=== 测试3: 解构赋值（数值默认值） ===")
do
  local t = {x = 10}
  local {x, y = 99} = t
  print(x, y)
  assert(x == 10 and y == 99, "test3 failed")
  print("PASS")
end

print("=== 测试4: 解构赋值（nil覆盖默认值） ===")
do
  local t = {a = 1, name = nil}
  local {a, name = "default"} = t
  print(a, name)
  -- name 在 t 中显式存在但值为 nil，应该使用默认值
  assert(a == 1 and name == "default", "test4 failed")
  print("PASS")
end

print("=== 测试5: 解构赋值（只含默认值字段） ===")
do
  local t = {}
  local {name = "hello"} = t
  print(name)
  assert(name == "hello", "test5 failed")
  print("PASS")
end

print("=== 测试6: 解构赋值（混合：有默认值和无默认值） ===")
do
  local t = {a = 1, c = 3}
  local {a, b = 20, c, d = 40} = t
  print(a, b, c, d)
  assert(a == 1, "a failed")
  assert(b == 20, "b failed")
  assert(c == 3, "c failed")
  assert(d == 40, "d failed")
  print("PASS")
end

print("=== 测试7: 解构赋值（变量作为默认值） ===")
do
  local dval = "default"
  local t = {x = 1}
  local {x, y = dval} = t
  print(x, y)
  assert(x == 1 and y == "default", "test7 failed")
  print("PASS")
end

print("\n全部测试通过!")