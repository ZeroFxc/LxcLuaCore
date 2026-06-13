--[[
  测试 Lua 5.5 <const> 和 <toclose> 属性
--]]

local passed = 0
local failed = 0

local function check(cond, msg)
  if cond then
    passed = passed + 1
    print("  [PASS] " .. msg)
  else
    failed = failed + 1
    print("  [FAIL] " .. msg)
  end
end

-- 用 load() 来捕获编译期错误
local function check_err_load(code, expected_substr, msg)
  local f, err = load(code)
  if not f and err:find(expected_substr, 1, true) then
    passed = passed + 1
    print("  [PASS] " .. msg .. " (compile error: " .. tostring(err) .. ")")
  else
    failed = failed + 1
    print("  [FAIL] " .. msg .. " (expected compile error containing '" .. expected_substr .. "', got: " .. tostring(f ~= nil) .. " / " .. tostring(err) .. ")")
  end
end

-- 用 load() + pcall() 来捕获运行期错误
local function check_err_run(code, expected_substr, msg)
  local f, err = load(code)
  if not f then
    failed = failed + 1
    print("  [FAIL] " .. msg .. " (compile error: " .. tostring(err) .. ")")
    return
  end
  local ok, err = pcall(f)
  if not ok and err:find(expected_substr, 1, true) then
    passed = passed + 1
    print("  [PASS] " .. msg .. " (runtime error: " .. tostring(err) .. ")")
  else
    failed = failed + 1
    print("  [FAIL] " .. msg .. " (expected runtime error containing '" .. expected_substr .. "', got: " .. tostring(ok) .. " / " .. tostring(err) .. ")")
  end
end

print(string.rep("=", 60))
print("测试 <const> 属性")
print(string.rep("=", 60))

-- 测试1: <const> 局部变量不能修改（编译期错误）
check_err_load([[
  local x <const> = 42
  x = 100
]], "const", "<const> variable cannot be reassigned (compile-time)")

-- 测试2: <const> 变量可以正常读取
do
  local f, err = load([[
    local x <const> = 42
    return x
  ]])
  check(f ~= nil, "<const> compiles: " .. (err or "OK"))
  if f then
    local r = f()
    check(r == 42, "<const> variable can be read (got: " .. tostring(r) .. ")")
  end
end

-- 测试3: <const> 在多重赋值中
check_err_load([[
  local a, b <const>, c = 1, 2, 3
  b = 20
]], "const", "<const> in multi-assignment: cannot modify")

-- 测试4: 非 const 变量在多重赋值中可修改
do
  local f, err = load([[
    local a, b <const>, c = 1, 2, 3
    a = 10
    return a, b, c
  ]])
  check(f ~= nil, "non-const in multi-assignment compiles: " .. (err or "OK"))
  if f then
    local a, b, c = f()
    check(a == 10 and b == 2 and c == 3, "non-const variable in multi-assignment can be modified")
  end
end

-- 测试5: for 循环变量只读
check_err_load([[
  for i = 1, 5 do
    i = 0
  end
]], "const", "for-loop variable 'i' is read-only (compile-time)")

-- 测试6: for 循环正常执行
do
  local f, err = load([[
    local count = 0
    for i = 1, 5 do
      count = count + i
    end
    return count
  ]])
  check(f ~= nil, "for loop compiles: " .. (err or "OK"))
  if f then
    local r = f()
    check(r == 15, "for loop sum = 15 (got: " .. tostring(r) .. ")")
  end
end

-- 测试7: 泛型 for 第一个变量只读
check_err_load([[
  for k, v in pairs({a=1}) do
    k = "xxx"
  end
]], "const", "generic for first variable 'k' is read-only")

-- 测试8: 泛型 for 第二个变量可修改
do
  local f, err = load([[
    local t = {a = 1, b = 2, c = 3}
    local sum = 0
    for k, v in pairs(t) do
      v = v + 10
      sum = sum + v
    end
    return sum
  ]])
  check(f ~= nil, "generic for second variable modifiable compiles: " .. (err or "OK"))
  if f then
    local r = f()
    check(r == 36, "generic for: 1+10 + 2+10 + 3+10 = 36 (got: " .. tostring(r) .. ")")
  end
end

print("")
print(string.rep("=", 60))
print("测试 <toclose> 属性")
print(string.rep("=", 60))

-- 测试9: <toclose> 变量在作用域结束时自动关闭
do
  local f, err = load([[
    local closed = false
    local mt = { __close = function(self) closed = true end }
    do
      local obj <toclose> = setmetatable({}, mt)
      assert(not closed, "not closed yet")
    end
    return closed
  ]])
  check(f ~= nil, "<toclose> compiles: " .. (err or "OK"))
  if f then
    local r = f()
    check(r == true, "<toclose> __close called when scope ends")
  end
end

-- 测试10: <toclose> 在提前 return 时关闭
do
  local f, err = load([[
    local closed = false
    local mt = { __close = function(self) closed = true end }
    local function test()
      local obj <toclose> = setmetatable({}, mt)
      return "early"
    end
    local result = test()
    return result, closed
  ]])
  check(f ~= nil, "<toclose> early return compiles: " .. (err or "OK"))
  if f then
    local result, closed = f()
    check(result == "early" and closed == true,
      "<toclose> closed on early return (result=" .. tostring(result) .. ", closed=" .. tostring(closed) .. ")")
  end
end

-- 测试11: 多个 <toclose> 按逆序关闭
do
  local f, err = load([[
    local order = {}
    local function make_closer(name)
      return setmetatable({}, {
        __close = function() order[#order + 1] = name end
      })
    end
    do
      local a <toclose> = make_closer("a")
      local b <toclose> = make_closer("b")
      local c <toclose> = make_closer("c")
    end
    return order[1], order[2], order[3]
  ]])
  check(f ~= nil, "multiple <toclose> compiles: " .. (err or "OK"))
  if f then
    local a, b, c = f()
    check(a == "c" and b == "b" and c == "a",
      "multiple <toclose> closed in reverse order: " .. tostring(a) .. "," .. tostring(b) .. "," .. tostring(c))
  end
end

-- 测试12: <toclose> 在错误时关闭
do
  local f, err = load([[
    local closed = false
    local mt = { __close = function(self) closed = true end }
    local ok, msg = pcall(function()
      local obj <toclose> = setmetatable({}, mt)
      error("test error inside scope")
    end)
    return ok, closed
  ]])
  check(f ~= nil, "<toclose> error close compiles: " .. (err or "OK"))
  if f then
    local ok, closed = f()
    check(not ok and closed == true,
      "<toclose> closed on error (ok=" .. tostring(ok) .. ", closed=" .. tostring(closed) .. ")")
  end
end

-- 测试13: 没有 __close 元方法的 <toclose> 会报错
check_err_run([[
  local x <toclose> = {}
]], "close", "<toclose> without __close raises error")

print("")
print(string.rep("=", 60))
print("测试 global 声明 (Lua 5.5)")
print(string.rep("=", 60))

-- 测试14: global 基本声明
do
  local f, err = load([[
    global print, type, tostring, math
    local x = 42
    return type(x)
  ]])
  check(f ~= nil, "global declaration compiles: " .. (err or "OK"))
  if f then
    local r = f()
    check(r == "number", "global usage works (got: " .. tostring(r) .. ")")
  end
end

-- 测试15: global <const> * 使未声明的全局只读
do
  local f, err = load([[
    global <const> *
    global print
    return "ok"
  ]])
  check(f ~= nil, "global <const> * compiles: " .. (err or "OK"))
  if f then
    local r = f()
    check(r == "ok", "global <const> * runs (got: " .. tostring(r) .. ")")
  end
end

print("")
print(string.rep("=", 60))
print("总计: PASSED = " .. passed .. ", FAILED = " .. failed)
print(string.rep("=", 60))

if failed > 0 then
  os.exit(1)
end