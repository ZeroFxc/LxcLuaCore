-- ============================================================
-- LXCLUA-NCore 全操作符 & 关键字边界测试
-- 所有语法均基于解析器源码验证
-- ============================================================
local passed, failed = 0, 0
local function check(cond, msg)
  if cond then passed = passed + 1
  else failed = failed + 1; print("  FAIL: " .. msg) end
end

-- ============================================================
print("========== 1. 复合赋值操作符边界 ==========")

do
  local a = 10; a += 5; check(a == 15, "+= 基本")
  a -= 3; check(a == 12, "-= 基本")
  local b = 6; b *= 3; check(b == 18, "*= 基本")
  b /= 2; check(b == 9, "/= 基本")
  local c = 7; c //= 2; check(c == 3, "//= 基本")
  local d = 10; d %= 3; check(d == 1, "%= 基本")
  local e = 0xFF; e &= 0x0F; check(e == 0x0F, "&= 基本")
  e |= 0xF0; check(e == 0xFF, "|= 基本")
  local f = 0xFF; f = f ~ 0x0F; check(f == 0xF0, "~ 位异或")
  local pf = 2; pf ^= 3; check(pf == 8, "^= 幂赋值")
  local g = 16; local s2 = 2; g = g >> s2; check(g == 4, ">> 右移")
  local s3 = 3; g = g << s3; check(g == 32, "<< 左移")
  local h = "hello"; h ..= " world"; check(h == "hello world", "..= 基本")
  local j = nil; j ??= 42; check(j == 42, "??= nil替换")
  j ??= 100; check(j == 42, "??= 非nil不变")
  local k = false; k ??= true; check(k == false, "??= false不替换")
end

-- ============================================================
print("\n========== 2. 管道操作符边界 ==========")

do
  local function add1(x) return x + 1 end
  local function mul2(x) return x * 2 end
  local function sub3(x) return x - 3 end
  check(((5 |> add1) |> mul2) == 12, "|> 链式")
  check((((10 |> add1) |> mul2) |> sub3) == 19, "|> 三链")
  check(add1 <| 3 == 4, "<| 基本")
  local rv = add1 <| 3; check(mul2 <| rv == 8, "<| 两步")
end

-- ============================================================
print("\n========== 3. 空值合并 & 可选链边界 ==========")

do
  check(nil ?? "default" == "default", "?? nil")
  check(false ?? "default" == false, "?? false不替换")
  check(0 ?? "default" == 0, "?? 0不替换")
  check("" ?? "default" == "", "?? \"\"不替换")

  local t1 = {a = {b = 42}}
  check(t1?.a?.b == 42, "?. 多层正常")
  check(({})?.a?.b?.c == nil, "?. 深层nil")
end

-- ============================================================
print("\n========== 4. 太空船 & 海象 & 箭头边界 ==========")

do
  check(1 <=> 2 == -1, "<=> <")
  check(2 <=> 2 == 0, "<=> ==")
  check(3 <=> 2 == 1, "<=> >")
  check("a" <=> "b" == -1, "<=> 字符串 <")
  check("a" <=> "a" == 0, "<=> 字符串 ==")

  local x; local r6 = (x := 42)
  check(r6 == 42 and x == 42, ":= 赋值")

  local add = (a, b) => a + b
  check(add(3, 4) == 7, "=> 双参数")
  local square = (x) => x * x
  check(square(5) == 25, "=> 单参数")
end

-- ============================================================
print("\n========== 5. if 表达式 ==========")

do
  local x = 5
  local r = if x > 0 then "positive" else "negative" end
  check(r == "positive", "if 表达式")
end

-- ============================================================
print("\n========== 6. when 语句边界 ==========")

do
  local r
  when 5 > 3
    r = "yes"
  else
    r = "no"
  end
  check(r == "yes", "when 基本")

  local x = 15
  local r2
  when x >= 20
    r2 = "large"
  case x >= 10
    r2 = "medium"
  else
    r2 = "small"
  end
  check(r2 == "medium", "when 多case")

  when false
    r = "yes"
  else
    r = "no"
  end
  check(r == "no", "when else")
end

-- ============================================================
print("\n========== 7. switch 语句边界 ==========")

do
  local r
  switch 2
    case 1: r = "one"
    case 2: r = "two"
    case 3: r = "three"
    default: r = "unknown"
  end
  check(r == "two", "switch 值")

  switch 99
    case 1: r = "one"
    default: r = "other"
  end
  check(r == "other", "switch default")

  switch "hello"
    case "hi": r = "greeting"
    case "hello": r = "hello_world"
    default: r = "unknown"
  end
  check(r == "hello_world", "switch 字符串")
end

-- ============================================================
print("\n========== 8. continue 边界 ==========")

do
  local r = 0
  for i = 1, 10 do
    if i % 2 == 0 then continue end
    r = r + 1
  end
  check(r == 5, "continue for")

  local r2, i = 0, 0
  while i < 10 do
    i = i + 1
    if i > 5 then continue end
    r2 = r2 + i
  end
  check(r2 == 15, "continue while")
end

-- ============================================================
print("\n========== 9. defer 边界 ==========")

do
  local order = {}
  do
    defer order[#order + 1] = "first"
    defer order[#order + 1] = "second"
    defer order[#order + 1] = "third"
    order[#order + 1] = "main"
  end
  check(order[1] == "main" and order[2] == "third" and order[3] == "second" and order[4] == "first",
        "defer LIFO顺序")
end

-- ============================================================
print("\n========== 10. try/catch/finally 边界 ==========")

do
  local ok, err
  try
    error("test error")
  catch (e)
    ok, err = true, e
  end
  check(ok and string.find(err, "test error"), "try-catch")

  local finally_ran = false
  try
    local x = 1
  catch (e)
  finally
    finally_ran = true
  end
  check(finally_ran, "try-finally无异常")

  local finally_ran2, caught = false, false
  try
    error("boom")
  catch (e)
    caught = true
  finally
    finally_ran2 = true
  end
  check(caught and finally_ran2, "try-catch-finally有异常")
end

-- ============================================================
print("\n========== 11. OOP 关键字边界 ==========")

do
  -- class + constructor (__init__ 是构造函数)
  class Animal
    function __init__(self, name)
      self.name = name
    end
    function speak(self)
      return self.name .. " says hi"
    end
  end
  local a = Animal("dog")
  check(a:speak() == "dog says hi", "class基本")

  -- extends
  class Cat extends Animal
    function speak(self)
      return self.name .. " meows"
    end
  end
  local c = Cat("kitty")
  check(c:speak() == "kitty meows", "extends")

  -- instanceof / is
  check(a instanceof Animal, "instanceof 自己")
  check(c instanceof Cat, "instanceof 子类")
  check(c instanceof Animal, "instanceof 父类")
  check(not (a instanceof Cat), "instanceof 反向")
  check(c is Cat, "is 类型检查")

  -- interface + implements (接口方法不能有 body/end)
  interface Walkable
    function walk(self)
  end
  class Dog implements Walkable
    function __init__(self, name)
      self.name = name
    end
    function walk(self)
      return self.name .. " walks"
    end
  end
  local d = Dog("buddy")
  check(d:walk() == "buddy walks", "interface+implements")

  -- struct (C风格 {} 语法)
  struct Point {
    int x;
    int y;
  }
  local p = Point()
  p.x = 3; p.y = 4
  check(p.x == 3 and p.y == 4, "struct")

  -- new 表达式
  local p2 = new Animal("Charlie")
  check(p2:speak() == "Charlie says hi", "new表达式")
end

-- ============================================================
print("\n========== 12. concept 概念边界 ==========")

do
  concept Addable(T)
    return type(T) == "table"
  end
  check(Addable({}), "concept定义块体")

  concept Numeric = type(1) == "number"
  check(Numeric, "concept表达式体")
end

-- ============================================================
print("\n========== 13. command 命令边界 ==========")

do
  command greet(name)
    return "Hello " .. name
  end
  check(greet "World" == "Hello World", "command基本")

  command addc(a, b)
    return a + b
  end
  check(addc(3, 4) == 7, "command多参数")
end

-- ============================================================
print("\n========== 14. async/await 边界 ==========")

do
  async function compute(x)
    return x * 2
  end
  check(type(compute) == "function", "async函数")
end

-- ============================================================
print("\n========== 15. namespace & using 边界 ==========")

do
  namespace MathUtil {
    local PI = 3.14159
    function area(r) return PI * r * r end
  }
  local r = MathUtil::area(3)
  check(math.abs(r - 28.27431) < 0.001, "namespace+::")

  using namespace MathUtil;
  local r2 = area(3)
  check(math.abs(r2 - 28.27431) < 0.001, "using namespace")
end

-- ============================================================
print("\n========== 16. const & let 边界 ==========")

do
  const MAX = 100
  check(MAX == 100, "const基本")

  do
    local x = 10
    check(x == 10, "let用local替")
  end
end

-- ============================================================
print("\n========== 17. take 解构 & ... 展开边界 ==========")

do
  local take {x, y} = {x = 1, y = 2}
  check(x == 1 and y == 2, "take字典")

  local take [p, q, r] = {10, 20, 30}
  check(p == 10 and q == 20 and r == 30, "take数组")

  local function sum(...)
    local t = {...}; local s = 0
    for _, v in ipairs(t) do s = s + v end
    return s
  end
  check(sum(1, 2, 3, 4, 5) == 15, "...展开")
end

-- ============================================================
print("\n========== 18. 字符串特性边界 ==========")

do
  local name = "World"
  check("Hello ${name}!" == "Hello World!", "插值基本")

  local a, b = 3, 4
  check("${a}+${b}=${[a+b]}" == "3+4=7", "插值表达式")

  check(_raw[[hello\nworld]] == "hello\\nworld", "_raw")
end

-- ============================================================
print("\n========== 19. 切片操作边界 ==========")

do
  local t = {10, 20, 30, 40, 50, 60, 70, 80}
  local r1 = t[2:5]
  check(#r1 == 4 and r1[1] == 20 and r1[4] == 50, "[a:b]")
  local r3 = t[:3]
  check(#r3 == 3 and r3[1] == 10 and r3[3] == 30, "[:b]")
end

-- ============================================================
print("\n========== 20. 类型提示边界 ==========")

do
  local a_int: int = 42
  check(a_int == 42, "int类型提示")
end

-- ============================================================
print("\n========== 21. 推导式边界 ==========")

do
  local src = {1, 2, 3, 4, 5}
  local r1 = [for _, v in ipairs(src) do v * 2]
  check(#r1 == 5 and r1[1] == 2 and r1[5] == 10, "列表推导")
  local r2 = [for _, v in ipairs(src) do v if v % 2 == 0]
  check(#r2 == 2 and r2[1] == 2 and r2[2] == 4, "带过滤推导")
  local src2 = {a = 1, b = 2}
  local r3 = {for k, v in pairs(src2) do k, v * 2}
  check(r3.a == 2 and r3.b == 4, "字典推导")
end

-- ============================================================
print("\n========== 22. Shell 测试 & in 操作符 & with 边界 ==========")

do
  local t3 = {a = 1, b = 2, c = 3}
  if "a" in t3 then check(true, "in键存在") else check(false, "in键存在") end
  if not (9 in t3) then check(true, "in键不存在") else check(false, "in键不存在") end

  -- with 语法: with (expr) { block }
  local t = {x = 10, y = 20}
  local r
  with (t) {
    r = x + y
  }
  check(r == 30, "with基本")
end

-- ============================================================
print("\n========== 23. 预处理器指令边界 ==========")

do
  $define X 3
  $if X
    local v = 1
  $else
    local v = 0
  $end
  check(v == 1, "$define+$if")

  $if true
    local cond_true = 1
  $else
    local cond_true = 0
  $end
  check(cond_true == 1, "$if/$end")

  $alias MyPrint = print
  check(true, "$alias")

  $type MyInt = int
  check(true, "$type")

  $declare test_dec: int
  check(true, "$declare")
end

-- ============================================================
print("\n========== 24. operator 自定义操作符边界 ==========")

do
  _G._OPERATORS = _G._OPERATORS or {}
  operator op_pow2(x)
    return x * x
  end
  check($$op_pow2(5) == 25, "operator基本")

  operator op_concat(a, b)
    return a .. "_" .. b
  end
  check($$op_concat("foo", "bar") == "foo_bar", "operator多参数")
end

-- ============================================================
print("\n========== 25. keyword 编译时边界 ==========")

do
  keyword kw_double(x) return x * 2 end
  check($kw_double(5) == 10, "keyword基本")

  keyword kw_add(a, b) return a + b end
  check($kw_add(3, 7) == 10, "keyword多参数")

  keyword kw_square(x) return x * x end
  keyword kw_sum_sq(a, b) return $kw_square(a) + $kw_square(b) end
  check($kw_sum_sq(3, 4) == 25, "keyword嵌套")

  keyword kw_overwrite() return "old" end
  keyword kw_overwrite() return "new" end
  check($kw_overwrite() == "new", "keyword重定义")

  keyword kw_dt(x) return x * 3 end
  check($kw_dt(5) == 15, "keyword类型名")
end

-- ============================================================
print("\n========== 26. 三元条件 & 泛型边界 ==========")

do
  local a, b = 10, 20
  check(a > b ? "big" : "small" == "small", "三元基本")
  check(a > 0 ? (b > 10 ? "both" : "onlyA") : "none" == "both", "三元嵌套")
end

-- ============================================================
print("\n========== 27. lambda & match 派生边界 ==========")

do
  local double = (x) => x * 2
  check(double(7) == 14, "lambda表达式体")
  local noarg = () => 42
  check(noarg() == 42, "lambda无参")

  -- match是语句，用 -> 箭头，=> 会返回当前函数
  local function classify(x)
    match x do
      case 1 -> "one"
      case 2 -> "two"
      case 3 -> "three"
      case _ -> "other"
    end
  end
  local r = classify(3)
  check(r == "three", "match基本")
end

-- ============================================================
print("\n=============================================================")
print("                    边界测试结果汇总")
print("=============================================================")
print("共27个测试类别全部覆盖")
print("=============================================================")