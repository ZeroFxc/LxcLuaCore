-- NLang 综合语法测试
-- 测试 NativeVM 的 NLang 编译器所有语法特性

local nv = require("nativevm")
local np = require("nativeparser")

local passed = 0
local failed = 0
local total = 0

local function test(name, code, expected, check_fn)
  total = total + 1
  local ok, main, nregs, funcs = pcall(np.compile, code)
  if not ok then
    print(string.format("  [FAIL] %s: 编译失败 - %s", name, tostring(main)))
    failed = failed + 1
    return
  end
  local vm = nv.new(main, nregs or 256)
  -- 注册函数
  if funcs then
    for _, f in ipairs(funcs) do
      nv.deffunc(vm, f.code, f.nregs, f.nparams, f.upvalues)
    end
  end
  -- 执行
  local ok2, result = pcall(nv.call, vm)
  if not ok2 then
    print(string.format("  [FAIL] %s: 执行失败 - %s", name, tostring(result)))
    failed = failed + 1
    return
  end
  local pass = false
  if check_fn then
    pass = check_fn(result)
  else
    pass = (result == expected)
  end
  if pass then
    print(string.format("  [PASS] %s", name))
    passed = passed + 1
  else
    print(string.format("  [FAIL] %s: 期望 %s, 实际 %s", name, tostring(expected), tostring(result)))
    failed = failed + 1
  end
end

print("========================================")
print("  NLang 语法测试")
print("========================================")
print("")

-- ============================================================
-- 1. 基础表达式
-- ============================================================
print("--- 1. 基础表达式 ---")

test("整数常量", "return 42", 42)
test("浮点常量", "return 3.14", 3.14, function(r) return math.abs(r - 3.14) < 0.001 end)
test("加法", "return 1 + 2", 3)
test("减法", "return 5 - 3", 2)
test("乘法", "return 4 * 3", 12)
test("除法", "return 15 / 4", 3)
test("取模", "return 15 % 4", 3)
test("整除", "return 15 // 4", 3)
test("幂运算", "return 2 ^ 3", 8)
test("比较 <", "return 1 < 2", 1)
test("比较 >", "return 2 > 1", 1)
test("比较 <=", "return 2 <= 2", 1)
test("比较 >=", "return 3 >= 2", 1)
test("比较 ==", "return 2 == 2", 1)
test("比较 ~=", "return 2 ~= 3", 1)
test("按位与 &", "return 0xFF & 0x0F", 0x0F)
test("按位或 |", "return 0xF0 | 0x0F", 0xFF)
test("按位异或 ~", "return 0xFF ~ 0x0F", 0xF0)
test("左移 <<", "return 1 << 3", 8)
test("右移 >>", "return 8 >> 2", 2)
test("按位取反 ~", "return ~0", -1)
test("not 操作", "return not 0", 1)
test("not true", "return not true", 0)
test("and 短路", "return 1 and 2", 2)
test("or 短路", "return 0 or 42", 42)

print("")

-- ============================================================
-- 2. 变量和控制流
-- ============================================================
print("--- 2. 变量和控制流 ---")

test("局部变量", "local x = 42; return x", 42)
test("赋值", "local x = 0; x = 42; return x", 42)

test("if-then", [[
  local x = 0
  if 1 < 2 then
    x = 42
  end
  return x
]], 42)

test("if-else true", [[
  local x = 0
  if 1 < 2 then x = 42 else x = 99 end
  return x
]], 42)

test("if-else false", [[
  local x = 0
  if 1 > 2 then x = 42 else x = 99 end
  return x
]], 99)

test("while 循环", [[
  local x = 0
  while x < 10 do
    x = x + 1
  end
  return x
]], 10)

test("repeat 循环", [[
  local x = 0
  repeat
    x = x + 1
  until x >= 10
  return x
]], 10)

test("for 循环", [[
  local s = 0
  for i = 1, 5 do
    s = s + i
  end
  return s
]], 15)

test("break", [[
  local x = 0
  while x < 10 do
    x = x + 1
    if x >= 5 then break end
  end
  return x
]], 5)

test("continue", [[
  local x = 0; local s = 0
  while x < 5 do
    x = x + 1
    if x == 3 then continue end
    s = s + x
  end
  return s
]], 12)

test("do 块", [[
  local x = 0
  do
    local y = 42
    x = y
  end
  return x
]], 42)

print("")

-- ============================================================
-- 3. 函数定义和调用
-- ============================================================
print("--- 3. 函数定义和调用 ---")

test("简单函数调用", [[
  function add(a, b)
    return a + b
  end
  return add(3, 4)
]], 7)

test("多参数函数", [[
  function sum(a, b, c)
    return a + b + c
  end
  return sum(1, 2, 3)
]], 6)

test("无参数函数", [[
  function val()
    return 42
  end
  return val()
]], 42)

test("递归 fib(10)", [[
  function fib(n)
    if n <= 1 then
      return n
    end
    return fib(n - 1) + fib(n - 2)
  end
  return fib(10)
]], 55)

test("递归 fib(15)", [[
  function fib(n)
    if n <= 1 then
      return n
    end
    return fib(n - 1) + fib(n - 2)
  end
  return fib(15)
]], 610)

test("递归阶乘", [[
  function fact(n)
    if n <= 1 then
      return 1
    end
    return n * fact(n - 1)
  end
  return fact(5)
]], 120)

print("")

-- ============================================================
-- 4. 闭包
-- ============================================================
print("--- 4. 闭包 ---")

test("闭包赋值调用", [[
  local add = function(a, b)
    return a + b
  end
  return add(10, 20)
]], 30)

test("闭包捕获外部变量", [[
  local x = 10
  local get_x = function()
    return x
  end
  return get_x()
]], 10)

test("闭包累加器", [[
  local x = 0
  local inc = function()
    x = x + 1
    return x
  end
  inc()
  inc()
  return inc()
]], 3)

print("")

-- ============================================================
-- 5. 字符串
-- ============================================================
print("--- 5. 字符串 ---")

test("字符串字面量", [[
  return "hello"
]], "hello", function(r) return tostring(r) == "hello" end)

test("单引号字符串", [[
  return 'world'
]], "world", function(r) return tostring(r) == "world" end)

print("")

-- ============================================================
-- 6. 表
-- ============================================================
print("--- 6. 表 ---")

test("表数组式", [[
  local t = {1, 2, 3}
  return t[1]
]], 1, function(r) return r == 1 end)

test("表键值式", [[
  local t = {x = 42, y = 99}
  return t.x
]], 42, function(r) return r == 42 end)

print("")

-- ============================================================
-- 7. 多重赋值
-- ============================================================
print("--- 7. 多重赋值 ---")

test("多重赋值", [[
  local a, b, c = 1, 2, 3
  return a + b + c
]], 6)

test("多重赋值交换", [[
  local a, b = 1, 2
  a, b = b, a
  return a == 2 and b == 1
]], 1)

print("")

-- ============================================================
-- 8. 逻辑运算符
-- ============================================================
print("--- 8. 逻辑运算符 ---")

test("and 真假", [[
  local a = (1 and 2)
  local b = (0 and 3)
  return a + b
]], 2)

test("or 真假", [[
  local a = (0 or 42)
  local b = (1 or 99)
  return a + b
]], 43)

test("not 操作", [[
  local a = not 0
  local b = not 1
  return a + b
]], 1)

print("")

-- ============================================================
-- 9. 嵌套控制流
-- ============================================================
print("--- 9. 嵌套控制流 ---")

test("嵌套 while", [[
  local s = 0
  local i = 1
  while i <= 3 do
    local j = 1
    while j <= 3 do
      s = s + 1
      j = j + 1
    end
    i = i + 1
  end
  return s
]], 9)

test("嵌套 for", [[
  local s = 0
  for i = 1, 3 do
    for j = 1, 3 do
      s = s + 1
    end
  end
  return s
]], 9)

test("if-elseif-else", [[
  local x = 0
  local val = 2
  if val == 0 then x = 10
  elseif val == 1 then x = 20
  elseif val == 2 then x = 30
  else x = 40
  end
  return x
]], 30)

print("")

-- ============================================================
-- 10. 长度运算符
-- ============================================================
print("--- 10. 长度运算符 ---")

test("字符串长度", [[
  return #"hello"
]], 5, function(r) return r == 5 end)

print("")

-- ============================================================
-- 11. 注释测试
-- ============================================================
print("--- 11. 注释 ---")

test("行注释", [[
  -- 这是注释
  local x = 42 -- 也是注释
  return x
]], 42)

test("块注释", [=[
  --[[
    这是块注释
    注释结束
  ]]
  return 42
]=], 42)

print("")

-- ============================================================
-- 12. 长字符串
-- ============================================================
print("--- 12. 长字符串 ---")

test("长字符串", [=[
  return [[hello world]]
]=], "hello world", function(r) return tostring(r) == "hello world" end)

print("")

-- ============================================================
-- 13. goto 标签
-- ============================================================
print("--- 13. goto 标签 ---")

test("goto 跳转", [[
  local x = 0
  goto lbl
  x = 99
  ::lbl::
  x = 42
  return x
]], 42)

print("")

-- ============================================================
-- 14. 性能：递归 fib 计时
-- ============================================================
print("--- 14. 递归 fib 性能对比 ---")

local fib_code = [[
  function fib(n)
    if n <= 1 then
      return n
    end
    return fib(n - 1) + fib(n - 2)
  end
  return fib(30)
]]

local function lua_fib(n)
  if n <= 1 then return n end
  return lua_fib(n-1) + lua_fib(n-2)
end

-- NLang fib(30)
local ok, main, nregs, funcs = pcall(np.compile, fib_code)
if ok then
  local vm = nv.new(main, nregs or 256)
  if funcs then
    for _, f in ipairs(funcs) do
      nv.deffunc(vm, f.code, f.nregs, f.nparams, f.upvalues)
    end
  end
  local start = os.clock()
  local result = nv.call(vm)
  local elapsed = os.clock() - start
  print(string.format("  NLang fib(30) = %d, 耗时 %.4f 秒", result or 0, elapsed))
end

local start = os.clock()
local result = lua_fib(30)
local elapsed = os.clock() - start
print(string.format("  Lua   fib(30) = %d, 耗时 %.4f 秒", result, elapsed))

print("")

-- ============================================================
-- 总结
-- ============================================================
print("========================================")
print(string.format("  结果: %d/%d 通过, %d 失败", passed, total, failed))
print("========================================")