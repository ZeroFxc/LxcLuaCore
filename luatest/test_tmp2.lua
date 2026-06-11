-- Lua 5.5 <close> 属性测试
-- 测试 <close> 变量的解析和基本运行时

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

print(string.rep("=", 60))
print("测试 <close> 属性")
print(string.rep("=", 60))

-- 测试1: <close> 编译通过
local f, err = load("local x <close> = 1")
check(f ~= nil, "<close> compiles: " .. (err or "OK"))

-- 测试2: <const> 编译通过 (回归测试)
local f2, err2 = load("local x <const> = 42")
check(f2 ~= nil, "<const> compiles: " .. (err2 or "OK"))

-- 测试3: <const> 不能赋值 (回归测试)
local f3, err3 = load("local x <const> = 42; x = 100")
check(f3 == nil and err3:find("const"), "<const> reassignment blocked")

-- 测试4: 未知属性报错
local f4, err4 = load("local x <unknown> = 1")
check(f4 == nil and err4:find("unknown"), "unknown attribute error: " .. tostring(err4))

-- 测试5: <close> 基本运行时 - __close 被调用
do
  local closed = false
  local mt = { __close = function() closed = true end }
  do
    local obj <close> = setmetatable({}, mt)
  end
  check(closed == true, "<close> __close called when scope ends")
end

-- 测试6: 多个 <close> 按逆序关闭
do
  local order = {}
  local function make_closer(name)
    return setmetatable({}, {
      __close = function() order[#order + 1] = name end
    })
  end
  do
    local a <close> = make_closer("a")
    local b <close> = make_closer("b")
    local c <close> = make_closer("c")
  end
  check(order[1] == "c" and order[2] == "b" and order[3] == "a",
    "reverse order: " .. table.concat(order, ","))
end

-- 测试7: 浮点数 tostring round-trip (Lua 5.5 改进)
local x = 1.1
check(tostring(x) == "1.1", "1.1 tostring = " .. tostring(x))
check(tonumber(tostring(x)) == x, "1.1 round-trip")

local y = 0.1 + 0.2
check(tonumber(tostring(y)) == y, "0.1+0.2 round-trip")

print("")
print(string.rep("=", 60))
print("总计: PASSED = " .. passed .. ", FAILED = " .. failed)
print(string.rep("=", 60))

if failed > 0 then
  os.exit(1)
end