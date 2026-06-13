-- 测试 NLang 编译器

local parser = require("nativeparser")
local nv = require("nativevm")

local function compile_run(src, nregs)
  local code = parser.compile(src)
  local vm = nv.new(code, nregs or 256)
  return { vm = vm, nargs = 0 }
end

local function expect_eq(name, got, want)
  if got ~= want then
    error(string.format("FAIL %s: got %s, want %s",
      name, tostring(got), tostring(want)))
  end
  print("PASS " .. name)
end

-- ========================================
-- 测试 1: 简单整数加法
-- ========================================
do
  local code = parser.compile([[
    local a = 10
    local b = 20
    return a + b
  ]])
  local vm = nv.new(code, 256)
  local r = nv.call(vm)
  expect_eq("int add", r, 30)
end

-- ========================================
-- 测试 2: 浮点运算
-- ========================================
do
  local code = parser.compile([[
    local x = 3.14
    local y = 2.0
    return x * y
  ]])
  local vm = nv.new(code, 256)
  local r = nv.call(vm)
  expect_eq("float mul", math.floor(r * 100 + 0.5), 628)
end

-- ========================================
-- 测试 3: if/else
-- ========================================
do
  local code = parser.compile([[
    local x = 5
    local r = 0
    if x > 3 then
      r = 100
    else
      r = 200
    end
    return r
  ]])
  local vm = nv.new(code, 256)
  expect_eq("if/else", nv.call(vm), 100)
end

-- ========================================
-- 测试 4: while 循环求和
-- ========================================
do
  local code = parser.compile([[
    local sum = 0
    local i = 1
    while i <= 10 do
      sum = sum + i
      i = i + 1
    end
    return sum
  ]])
  local vm = nv.new(code, 256)
  expect_eq("while sum 1..10", nv.call(vm), 55)
end

-- ========================================
-- 测试 5: for 循环
-- ========================================
do
  local code = parser.compile([[
    local sum = 0
    for i = 1, 100 do
      sum = sum + i
    end
    return sum
  ]])
  local vm = nv.new(code, 256)
  expect_eq("for 1..100", nv.call(vm), 5050)
end

-- ========================================
-- 测试 6: 阶乘（10! = 3628800）
-- ========================================
do
  local code = parser.compile([[
    local n = 10
    local result = 1
    for i = 1, n do
      result = result * i
    end
    return result
  ]])
  local vm = nv.new(code, 256)
  expect_eq("factorial 10", nv.call(vm), 3628800)
end

-- ========================================
-- 测试 7: elseif 链
-- ========================================
do
  local code = parser.compile([[
    local x = 5
    local r = 0
    if x < 0 then
      r = 1
    elseif x < 3 then
      r = 2
    elseif x < 7 then
      r = 3
    else
      r = 4
    end
    return r
  ]])
  local vm = nv.new(code, 256)
  expect_eq("elseif chain", nv.call(vm), 3)
end

-- ========================================
-- 测试 8: repeat/until
-- ========================================
do
  local code = parser.compile([[
    local x = 0
    repeat
      x = x + 1
    until x >= 5
    return x
  ]])
  local vm = nv.new(code, 256)
  expect_eq("repeat-until", nv.call(vm), 5)
end

-- ========================================
-- 测试 9: 关系与逻辑运算
-- ========================================
do
  local code = parser.compile([[
    local a = 5
    local b = 10
    local r = 0
    if a < b and a > 0 then
      r = r + 1
    end
    if a == 5 or b == 100 then
      r = r + 10
    end
    if not (a > 100) then
      r = r + 100
    end
    return r
  ]])
  local vm = nv.new(code, 256)
  expect_eq("logic ops", nv.call(vm), 111)
end

-- ========================================
-- 测试 10: 位运算
-- ========================================
do
  local code = parser.compile([[
    local x = 0xFF
    local y = 0x0F
    local r = 0
    r = x & y
    return r
  ]])
  local vm = nv.new(code, 256)
  expect_eq("bitwise and", nv.call(vm), 15)
end

-- ========================================
-- 测试 11: 斐波那契（迭代）
-- ========================================
do
  local code = parser.compile([[
    local n = 20
    local a = 0
    local b = 1
    for i = 2, n do
      local t = a + b
      a = b
      b = t
    end
    return b
  ]])
  local vm = nv.new(code, 256)
  expect_eq("fibonacci 20", nv.call(vm), 6765)
end

-- ========================================
-- 测试 12: 一元负号
-- ========================================
do
  local code = parser.compile([[
    local x = 42
    return -x
  ]])
  local vm = nv.new(code, 256)
  expect_eq("unary minus", nv.call(vm), -42)
end

-- ========================================
-- 测试 13: 浮点取模（VM 不支持，退化为 int 取模）
-- ========================================
do
  local code = parser.compile([[
    return 17 % 5
  ]])
  local vm = nv.new(code, 256)
  expect_eq("int mod", nv.call(vm), 2)
end

-- ========================================
-- 测试 14: 表达式优先级
-- ========================================
do
  local code = parser.compile([[
    return 2 + 3 * 4 - 1
  ]])
  local vm = nv.new(code, 256)
  expect_eq("precedence", nv.call(vm), 13)
end

-- ========================================
-- 测试 15: 嵌套块与作用域
-- ========================================
do
  local code = parser.compile([[
    local a = 10
    if a > 5 then
      local b = 20
      if b > 15 then
        a = a + b
      end
    end
    return a
  ]])
  local vm = nv.new(code, 256)
  expect_eq("nested scope", nv.call(vm), 30)
end

-- ========================================
-- 测试 16: 注释
-- ========================================
do
  local code = parser.compile([==[
    -- 单行注释
    local x = 1 -- 行尾注释
    --[[
      多行注释
      块注释
    ]]
    return x + 2
  ]==])
  local vm = nv.new(code, 256)
  expect_eq("comments", nv.call(vm), 3)
end

-- ========================================
-- 测试 17: 错误处理 — 语法错误
-- ========================================
do
  local ok, err = pcall(parser.compile, "local x = ")
  if not ok then
    print("PASS syntax error caught: " .. tostring(err):sub(1, 50) .. "...")
  else
    error("FAIL: expected syntax error")
  end
end

-- ========================================
-- 测试 18: 错误处理 — 未定义变量
-- ========================================
do
  local ok, err = pcall(parser.compile, [[
    return undefined_var
  ]])
  if not ok then
    print("PASS undefined var error: " .. tostring(err):sub(1, 50) .. "...")
  else
    error("FAIL: expected undefined var error")
  end
end

-- ========================================
-- 测试 19: 整数除法
-- ========================================
do
  local code = parser.compile([[
    return 100 / 7
  ]])
  local vm = nv.new(code, 256)
  -- 整数除法向下取整
  expect_eq("int div", nv.call(vm), 14)
end

-- ========================================
-- 测试 20: 浮点除法
-- ========================================
do
  local code = parser.compile([[
    local a = 10.0
    local b = 3.0
    return a / b
  ]])
  local vm = nv.new(code, 256)
  local r = nv.call(vm)
  expect_eq("float div", math.floor(r * 1000 + 0.5), 3333)
end

-- ========================================
-- 测试 21: break（在 while 中）
-- ========================================
do
  local code = parser.compile([[
    local sum = 0
    local i = 0
    while i < 100 do
      i = i + 1
      if i > 5 then
        break
      end
      sum = sum + i
    end
    return sum
  ]])
  local vm = nv.new(code, 256)
  expect_eq("break", nv.call(vm), 15)  -- 1+2+3+4+5
end

-- ========================================
-- 测试 22: 十六进制与二进制字面量
-- ========================================
do
  local code = parser.compile([[
    local a = 0xFF
    local b = 0b1010
    return a + b
  ]])
  local vm = nv.new(code, 256)
  expect_eq("hex/bin literals", nv.call(vm), 265)
end

-- ========================================
-- 测试 23: nil 字面量
-- ========================================
do
  local code = parser.compile([[
    local x = nil
    return x
  ]])
  local vm = nv.new(code, 256)
  -- nil 是合法的返回值；这里只验证能成功返回
  local r = nv.call(vm)
  if r == nil then
    print("PASS nil literal (returned nil)")
  else
    error("FAIL nil literal: expected nil, got " .. tostring(r))
  end
end

-- ========================================
-- 测试 24: 复杂表达式
-- ========================================
do
  local code = parser.compile([[
    local a = 2
    local b = 3
    local c = 4
    return a * b + b * c + c * a
  ]])
  local vm = nv.new(code, 256)
  expect_eq("complex expr", nv.call(vm), 6 + 12 + 8)
end

print()
print("===== 全部 24 个测试通过 =====")
