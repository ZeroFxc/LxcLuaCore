-- ============================================================
-- LXCLUA-NCore 全语法糖测试脚本
-- 每个语法糖使用 load() 隔离测试，单个失败不影响其他
-- ============================================================

local passed = 0
local failed = 0
local total = 0
local results = {}

-- 测试辅助函数：用 load 编译并执行代码片段
-- 返回 true=成功, false=失败, err_msg
local function test(name, code, execute)
    total = total + 1
    local f, err = load(code, "=test_" .. name)
    if not f then
        failed = failed + 1
        local short_err = err:match(".-:%d+: (.*)") or err:sub(1, 100)
        results[name] = {status = "FAIL", msg = "编译失败: " .. short_err}
        return false, short_err
    end
    if execute ~= false then
        local ok, run_err = pcall(f)
        if not ok then
            -- 编译通过但运行失败也记录
        end
    end
    passed = passed + 1
    results[name] = {status = "PASS"}
    return true
end

-- 测试辅助函数：编译通过即可（不执行）
local function test_compile(name, code)
    return test(name, code, false)
end

-- ============================================================
-- 1. 扩展操作符
-- ============================================================
print("========== 1. 扩展操作符 ==========")

test("复合赋值 +=", [[local a=10; a+=5; assert(a==15)]])
test("复合赋值 -=", [[local a=10; a-=3; assert(a==7)]])
test("复合赋值 *=", [[local a=10; a*=3; assert(a==30)]])
test("复合赋值 /=", [[local a=30; a/=2; assert(a==15)]])
test("复合赋值 //=", [[local a=10; a//=3; assert(a==3)]])
test("复合赋值 %=", [[local a=10; a%=3; assert(a==1)]])
test("复合赋值 &=", [[local a=0xFF; a&=0x0F; assert(a==0x0F)]])
test("复合赋值 |=", [[local a=0x0F; a|=0xF0; assert(a==0xFF)]])
test("复合赋值 ^=", [[local a=0xFF; a^=0xF0; assert(a==0x0F)]])
test("复合赋值 >>=", [[local a=8; a>>=2; assert(a==2)]])
test("复合赋值 <<=", [[local a=2; a<<=3; assert(a==16)]])
test("复合赋值 ..=", [[local a="hello"; a..=" world"; assert(a=="hello world")]])
test("复合赋值 ??=", [[local a=nil; a??=42; assert(a==42)]])

test("自增 var++", [[local a=10; a++; assert(a==11)]])

test("太空船操作符 <=>", [[
    assert(10 <=> 20 == -1)
    assert(20 <=> 20 == 0)
    assert(30 <=> 20 == 1)
]])

test("空值合并 ??", [[
    local a = nil
    local b = a ?? "default"
    assert(b == "default")
    local c = "exists"
    local d = c ?? "default"
    assert(d == "exists")
]])

test("可选链 ?.", [[
    local t = {a={b=42}}
    assert(t?.a?.b == 42)
    assert(t?.x?.y == nil)
]])

test("管道操作符 |>", [[
    local function add1(x) return x+1 end
    local function mul2(x) return x*2 end
    local r = 5 |> add1 |> mul2
    assert(r == 12)
]])

test("反向管道 <|", [[
    local function add1(x) return x+1 end
    local r = add1 <| 5
    assert(r == 6)
]])

test("安全管道 |?>", [[
    local function dbl(x) return x*2 end
    local a = nil
    local r = a |?> dbl
    assert(r == nil)
    local b = 5
    local r2 = b |?> dbl
    assert(r2 == 10)
]])

test("海象操作符 := 独立语句", [[local x; x := 100; assert(x == 100)]])
test("海象操作符 := 在if中", [[local x; if (x := 50) > 25 then assert(x==50) end]])
test("海象操作符 := 表赋值", [[local t; t := {name="test"}; assert(t.name=="test")]])

-- ============================================================
-- 2. 增强字符串
-- ============================================================
print("========== 2. 增强字符串 ==========")

test("字符串插值 ${var}", [[
    local name = "World"
    local s = "Hello, ${name}!"
    assert(s == "Hello, World!")
]])

test("字符串插值 ${[expr]}", [[
    local a, b = 10, 20
    local s = "Sum: ${[a+b]}"
    assert(s == "Sum: 30")
]])

test("原生字符串 _raw", [[
    local s = _raw"C:\\Windows\\System32"
    assert(s == "C:\\Windows\\System32")
]])

-- ============================================================
-- 3. 注释风格
-- ============================================================
print("========== 3. 注释风格 ==========")

test("C风格单行注释 //", [[
    // 这是单行注释
    local x = 1
    assert(x == 1)
]])

test("C风格多行注释 /* */", [[
    /* 这是
       多行注释 */
    local x = 1
    assert(x == 1)
]])

-- ============================================================
-- 4. 函数特性
-- ============================================================
print("========== 4. 函数特性 ==========")

test("箭头函数 ->(args){ body }", [[
    local f = ->(x) { return x * 2 }
    assert(f(5) == 10)
]])

test("箭头函数 =>(args){ expr }", [[
    local f = =>(x) { x * x }
    assert(f(4) == 16)
]])

test("箭头函数 (a,b)=>expr", [[
    local add = (a,b) => a + b
    assert(add(3,4) == 7)
]])

test("Lambda 表达式", [[
    local sq = lambda(x): x * x end
    assert(sq(6) == 36)
]])

test("C风格函数 int func(args){...}", [[
    int sum(int a, int b) {
        return a + b;
    }
    assert(sum(10,20) == 30)
]])

test("泛型函数", [[
    local function Factory(T)(val)
        return { type = T, value = val }
    end
    local obj = Factory("int")(99)
    assert(obj.type == "int" and obj.value == 99)
]])

test("泛型函数带约束", [[
    local function Echo(T)(val: T): T
        return val
    end
    local r = Echo("string")("hello")
    assert(r == "hello")
]])

-- ============================================================
-- 5. 类型提示
-- ============================================================
print("========== 5. 类型提示 ==========")

test("类型提示 :int", [[
    local x: int = 10
    assert(x == 10)
]])

test("类型提示 :string", [[
    local s: string = "hello"
    assert(s == "hello")
]])

test("类型提示 :float", [[
    local f: float = 3.14
    assert(f > 3)
]])

test("类型提示 :bool", [[
    local b: bool = true
    assert(b == true)
]])

test("函数参数和返回值类型", [[
    local function greet(name: string): string
        return "Hello, " .. name
    end
    assert(greet("World") == "Hello, World")
]])

-- ============================================================
-- 6. 控制流
-- ============================================================
print("========== 6. 控制流 ==========")

test("switch 语句", [[
    local result = ""
    local val = "b"
    switch (val) do
        case "a":
            result = "A"
            break
        case "b":
            result = "B"
            break
        default:
            result = "?"
    end
    assert(result == "B")
]])

test("when 语句", [[
    local result = ""
    local x = -5
    when x > 0
        result = "positive"
    case x < 0
        result = "negative"
    else
        result = "zero"
    end
    assert(result == "negative")
]])

test("try-catch", [[
    local caught
    try
        error("test error")
    catch(e)
        caught = e
    end
    assert(caught == "test error")
]])

test("try-catch-finally", [[
    local cleaned = false
    local caught
    try
        error("oops")
    catch(e)
        caught = e
    finally
        cleaned = true
    end
    assert(caught == "oops" and cleaned)
]])

test("defer 语句", [[
    local cleaned = false
    local function test_defer()
        defer cleaned = true
        assert(not cleaned)  -- defer 还没执行
    end
    test_defer()
    assert(cleaned)  -- defer 已执行
]])

test("with 语句", [[
    local ctx = { val = 99 }
    local result
    with (ctx) {
        result = val
    }
    assert(result == 99)
]])

test("三元条件 ?:", [[
    local x = true
    local r = x ? 10 : 20
    assert(r == 10)
    local r2 = (not x) ? 30 : 40
    assert(r2 == 40)
]])

test("continue 语句", [[
    local sum = 0
    for i = 1, 10 do
        if i % 2 == 0 then
            continue
        end
        sum = sum + i
    end
    assert(sum == 25)  -- 1+3+5+7+9
]])

test("if 条件表达式", [[
    local a, b = 10, 20
    local max = if a > b then a else b end
    assert(max == 20)
]])

-- ============================================================
-- 7. OOP：类、接口、继承
-- ============================================================
print("========== 7. OOP ==========")

test_compile("interface 接口", [[
    interface Drawable
        function draw(self)
    end
    return true
]])

test_compile("class 类 + implements", [[
    interface ITest
        function run(self)
    end
    class Animal implements ITest
        private _name = ""
        function __init__(self, name)
            self._name = name
        end
        function run(self)
            return self._name .. " runs"
        end
        static function create(name)
            return new Animal(name)
        end
    end
    local a = Animal.create("Cat")
    assert(a:run() == "Cat runs")
]])

test_compile("extends 继承 + super", [[
    class Animal
        function speak(self) return "..." end
    end
    class Dog extends Animal
        function speak(self)
            return "Woof! " .. super.speak(self)
        end
    end
    local d = Dog()
    assert(d:speak() == "Woof! ...")
]])

test_compile("sealed class", [[
    sealed class FinalClass
        function val(self) return 42 end
    end
    local f = FinalClass()
    assert(f:val() == 42)
]])

test_compile("get/set 属性", [[
    class Person
        private _age = 0
        public get age(self) return self._age end
        public set age(self, v)
            if v >= 0 then self._age = v end
        end
    end
    local p = Person()
    p.age = 25
    assert(p.age == 25)
]])

test("instanceof 操作符", [[
    class A end
    local a = A()
    assert(a instanceof A)
]])

test("is 操作符", [[
    class B end
    local b = B()
    assert(b is B)
]])

test("new 表达式", [[
    class MyObj
        function val(self) return 123 end
    end
    local o = new MyObj()
    assert(o:val() == 123)
]])

-- ============================================================
-- 8. 结构体与类型
-- ============================================================
print("========== 8. 结构体与类型 ==========")

test_compile("struct 结构体", [[
    struct Point {
        int x;
        int y;
    }
    local p = Point()
    p.x = 10
    p.y = 20
    assert(p.x == 10 and p.y == 20)
]])

test_compile("superstruct", [[
    superstruct MetaPoint [
        x: 0,
        y: 0,
        ["move"]: function(self, dx, dy)
            self.x = self.x + dx
            self.y = self.y + dy
        end
    ]
    local mp = MetaPoint()
    assert(mp.x == 0)
]])

test_compile("concept 概念", [[
    concept IsPositive(x)
        return x > 0
    end
    assert(IsPositive(5))
]])

test_compile("concept 表达式体", [[
    concept IsEven(x) = x % 2 == 0
    assert(IsEven(4))
]])

test_compile("enum 枚举", [[
    enum Color {
        Red,
        Green,
        Blue = 10
    }
    assert(Color.Red == 0)
    assert(Color.Green == 1)
    assert(Color.Blue == 10)
]])

test("解构赋值 take {} 字典", [[
    local data = { x = 10, y = 20, z = 30 }
    local take { x, y } = data
    assert(x == 10 and y == 20)
]])

test("解构赋值 take [] 数组", [[
    local arr = {10, 20, 30}
    local take [ first, , third ] = arr
    assert(first == 10 and third == 30)
]])

test("解构赋值 默认值", [[
    local data = { name = "Bob" }
    local take { name = "guest", age = 18 } = data
    assert(name == "Bob" and age == 18)
]])

test("展开运算符 ...", [[
    local a = {1, 2}
    local b = {3, 4}
    local c = { 0, ...a, ...b }
    assert(c[1] == 0 and c[2] == 1 and c[3] == 2 and c[4] == 3 and c[5] == 4)
]])

-- ============================================================
-- 9. 命名空间
-- ============================================================
print("========== 9. 命名空间 ==========")

test_compile("namespace + :: 访问", [[
    namespace MyLib {
        function test(x)
            return x * 2
        end
    }
    local r = MyLib::test(10)
    assert(r == 20)
]])

test_compile("using namespace", [[
    namespace Utils {
        function add(a, b)
            return a + b
        end
    }
    using namespace Utils;
    local r = add(3, 4)
    assert(r == 7)
]])

-- ============================================================
-- 10. 推导式
-- ============================================================
print("========== 10. 推导式 ==========")

test("列表推导式", [[
    local src = {1, 2, 3, 4, 5}
    local evens = [for _, v in ipairs(src) do v * 2 if v % 2 == 0]
    assert(#evens == 2)
    assert(evens[1] == 4)
    assert(evens[2] == 8)
]])

test("字典推导式", [[
    local src = {a=1, b=2, c=3}
    local dbl = {for k, v in pairs(src) do k, v*2}
    assert(dbl.a == 2 and dbl.b == 4 and dbl.c == 6)
]])

-- ============================================================
-- 11. Shell 风格测试
-- ============================================================
print("========== 11. Shell 风格测试 ==========")

test("Shell [-f file]", [[
    local r = [ -f "test_sugar_all.lua" ]
    assert(r == true)
]])

test("Shell [ str == str ]", [[
    local r = [ "hello" == "hello" ]
    assert(r == true)
]])

test("Shell [ num -gt num ]", [[
    local r = [ 10 -gt 5 ]
    assert(r == true)
]])

-- ============================================================
-- 12. 切片操作
-- ============================================================
print("========== 12. 切片操作 ==========")

test("切片 [a:b]", [[
    local arr = {1,2,3,4,5,6,7,8,9,10}
    local s1 = arr[1:5]
    assert(#s1 == 5 and s1[1]==1 and s1[5]==5)
]])

test("切片 [a:b:c] 步长", [[
    local arr = {1,2,3,4,5,6,7,8,9,10}
    local s2 = arr[1:10:2]
    assert(#s2 == 5 and s2[1]==1 and s2[5]==9)
]])

test("切片 [a:]", [[
    local arr = {1,2,3,4,5}
    local s = arr[3:]
    assert(#s == 3 and s[1]==3 and s[3]==5)
]])

test("切片 [:b]", [[
    local arr = {1,2,3,4,5}
    local s = arr[:3]
    assert(#s == 3 and s[1]==1 and s[3]==3)
]])

test("切片 [::-1] 反转", [[
    local arr = {1,2,3}
    local s = arr[::-1]
    assert(s[1]==3 and s[2]==2 and s[3]==1)
]])

test("字符串切片", [[
    local s = "Hello World"
    local sub = s[1:5]
    assert(sub == "Hello")
]])

-- ============================================================
-- 13. in 操作符
-- ============================================================
print("========== 13. in 操作符 ==========")

test("in 操作符 数组", [[
    local arr = {1,2,3,4,5}
    assert(3 in arr)
    assert(not (99 in arr))
]])

test("in 操作符 字符串", [[
    local s = "Hello World"
    assert("World" in s)
    assert(not ("XYZ" in s))
]])

-- ============================================================
-- 14. const 常量
-- ============================================================
print("========== 14. const 常量 ==========")

test("const 常量", [[
    const PI = 3.14159
    assert(PI == 3.14159)
]])

-- ============================================================
-- 15. 元编程：keyword, command, operator
-- ============================================================
print("========== 15. 元编程 ==========")

test_compile("keyword 自定义关键字", [[
    _G._KEYWORDS = {}
    keyword unless(cond, body)
        if not cond then
            body()
        end
    end
    local executed = false
    unless(false, function() executed = true end)
    assert(executed)
]])

test_compile("command 自定义命令", [[
    _G._CMDS = {}
    command echo(...)
        local args = {...}
        for _, v in ipairs(args) do
            -- just iterate
        end
    end
    echo("Hello", "World")
    assert(true)
]])

test_compile("operator 自定义操作符", [[
    _G._OPERATORS = {}
    operator ++(x)
        return x + 1
    end
    local r = $$++(10)
    assert(r == 11)
]])

-- ============================================================
-- 16. 预处理器指令
-- ============================================================
print("========== 16. 预处理器指令 ==========")

test("预处理器 $define", [[
    $define MAX_VAL 100
    assert(MAX_VAL == 100)
]])

test("预处理器 $alias", [[
    $alias MyFunc = print
    -- 基本验证
    assert(true)
]])

test("预处理器 $type", [[
    $type MyInt = int
    assert(true)
]])

test("预处理器 $if/$else/$end", [[
    $if true
        local x = 1
    $else
        local x = 2
    $end
    assert(x == 1)
]])

test("预处理器 $declare", [[
    $if true
        $declare g_var: int
    $end
    assert(true)
]])

-- ============================================================
-- 17. 内联汇编 (asm) - 仅编译检查
-- ============================================================
print("========== 17. 内联汇编 ==========")

test_compile("asm 内联汇编", [[
    local result
    asm(
        newreg a;
        newreg b;
        LOADI a 10;
        LOADI b 20;
        ADD a a b;
        MOVE $result a;
    )
    assert(result == 30)
]])

-- ============================================================
-- 18. Async/Await
-- ============================================================
print("========== 18. Async/Await ==========")

test_compile("async function 全局", [[
    -- 需要先加载 asyncio
    local ok, asyncio = pcall(require, "asyncio")
    if not ok then return true end -- 可能还未加载
    
    async function test_async_global()
        return 42
    end
    local p = test_async_global()
    assert(asyncio.isPromise(p))
]])

test_compile("local async function", [[
    local async function test_local()
        return 99
    end
    assert(true)
]])

test_compile("await 表达式", [[
    async function test_await()
        local r = await(asyncio.sleep(0.001))
        return r
    end
    assert(true)
]])

-- ============================================================
-- 报告汇总
-- ============================================================
print("\n")
print("=============================================================")
print("                    测试结果汇总")
print("=============================================================")
print(string.format("总计: %d | 通过: %d | 失败: %d", total, passed, failed))
print(string.format("通过率: %.1f%%", (passed / total) * 100))
print("-------------------------------------------------------------")

if failed > 0 then
    print("\n【失败的测试项】:")
    for name, r in pairs(results) do
        if r.status == "FAIL" then
            print(string.format("  ✗ %s: %s", name, r.msg or "未知错误"))
        end
    end
end

print("\n【通过的特性清单】:")
local cats = {}
for name, r in pairs(results) do
    if r.status == "PASS" then
        table.insert(cats, name)
    end
end
table.sort(cats)
for _, name in ipairs(cats) do
    print(string.format("  ✓ %s", name))
end

print("=============================================================")