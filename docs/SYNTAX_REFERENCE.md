# LXCLUA-NCore 语法参考手册

本文档基于 `lparser.c`、`llex.c`、`llex.h` 和 `lcode.c` 源码实际分析，详细列出所有 LXCLUA-NCore 扩展语法的定义、用法和编译方式。

---

## 1. 词法扩展 (Token 系统)

### 1.1 新增关键字

| Token | 关键字 | 用途 |
|-------|--------|------|
| `TK_ASM` | `asm` | 内联汇编块 |
| `TK_ASYNC` | `async` | 异步函数声明 |
| `TK_AWAIT` | `await` | 异步等待表达式 |
| `TK_COMMAND` | `command` | 自定义命令定义 |
| `TK_CONCEPT` | `concept` | 类型谓词概念定义 |
| `TK_CONST` | `const` | 常量声明 |
| `TK_CONTINUE` | `continue` | 循环继续语句 |
| `TK_DEFER` | `defer` | 延迟执行语句 |
| `TK_ENUM` | `enum` | 枚举定义 |
| `TK_INSTANCEOF` | `instanceof` | 类型检查二元操作符 |
| `TK_IS` | `is` | 类型检查二元操作符 |
| `TK_LAMBDA` | `lambda` | Lambda 表达式 |
| `TK_NAMESPACE` | `namespace` | 命名空间定义 |
| `TK_OPERATOR` | `operator` | 自定义操作符定义 |
| `TK_STRUCT` | `struct` | 结构体定义 |
| `TK_SUPERSTRUCT` | `superstruct` | 增强表(SETFL)定义 |
| `TK_SWITCH` | `switch` | 分支语句 |
| `TK_TAKE` | `take` | 解构赋值前缀 |
| `TK_TRY` | `try` | 异常处理块 |
| `TK_CATCH` | `catch` | 异常捕获块 |
| `TK_FINALLY` | `finally` | 异常清理块 |
| `TK_USING` | `using` | 命名空间导入 |
| `TK_WHEN` | `when` | 条件分支变体语句 |
| `TK_WITH` | `with` | 环境切换块 |
| `TK_LET` | `let` | 块级变量声明 |

### 1.2 普通关键字 (Reserved Words)

以下为在 `enum RESERVED` 中定义的关键字：

- `TK_BOOL`, `TK_CHAR`, `TK_DOUBLE`, `TK_FALSE`, `TK_TRUE`, `TK_NIL`
- `TK_TYPE_FLOAT`, `TK_TYPE_INT`, `TK_LONG`, `TK_VOID`
- `TK_CLASS`, `TK_INTERFACE`, `TK_EXPORT`, `TK_GLOBAL`
- `TK_PRIVATE`, `TK_PUBLIC`, `TK_PROTECTED`, `TK_STATIC`, `TK_FINAL`, `TK_ABSTRACT`, `TK_SEALED`
- `TK_GET`, `TK_SET`, `TK_VAR`
- `TK_EXTENDS`, `TK_IMPLEMENTS`, `TK_SUPER`, `TK_NEW`, `TK_REQUIRES`
- `TK_KEYWORD`, `TK_CASE`, `TK_DEFAULT`, `TK_INFIX`
- `TK_WHERE`, `TK_DBCOLON`

### 1.3 新增运算符 Token

| Token | 字符 | 含义 |
|-------|------|------|
| `TK_SPACESHIP` | `<=>` | 太空船(三路比较)操作符 |
| `TK_NULLCOAL` | `??` | 空值合并操作符 |
| `TK_NULLCOALEQ` | `??=` | 空值合并复合赋值 |
| `TK_OPTCHAIN` | `?.` | 可选链操作符 |
| `TK_PIPE` | `\|>` | 正向管道操作符 |
| `TK_REVPIPE` | `<\|` | 反向管道操作符 |
| `TK_SAFEPIPE` | `\|?>` | 安全管道操作符(nil短路) |
| `TK_WALRUS` | `:=` | 海象操作符(赋值表达式) |
| `TK_ARROW` / `TK_MEAN` | `=>` | 箭头函数操作符 |
| `TK_DOLLAR` | `$` | 预处理器前缀 |
| `TK_DOLLDOLL` | `$$` | 自定义操作符调用前缀 |
| `TK_PLUSPLUS` | `++` | 后缀自增操作符 |

### 1.4 新增字符串类型

| Token | 前缀 | 用途 |
|-------|------|------|
| `TK_INTERPSTRING` | 字符串中含 `${}` | 插值字符串 |
| `TK_RAWSTRING` | `_raw` 前缀 | 原生字符串 (不处理转义) |

### 1.5 复合赋值运算符

`TK_ADDEQ(+=)` `TK_SUBEQ(-=)` `TK_MULEQ(*=)` `TK_DIVEQ(/=)` `TK_IDIVEQ(//=)` `TK_MODEQ(%=)` `TK_BANDEQ(&=)` `TK_BOREQ(\|=)` `TK_BXOREQ(~=)` `TK_SHREQ(>>=)` `TK_SHLEQ(<<=)` `TK_CONCATEQ(..=)` `TK_POWEQ(^=)` `TK_NULLCOALEQ(??=)`

共 14 种复合赋值运算符(含 ??=)。

---

## 2. 表达式扩展

### 2.1 复合赋值运算符 (Compound Assignment)

解析函数: `compoundassign()` 在 `lparser.c` 中

```lua
local a = 10
a += 5      -- a = a + 5
a -= 3      -- a = a - 3
a *= 2      -- a = a * 2
a /= 4      -- a = a / 4
a //= 3     -- a = a // 3
a %= 2      -- a = a % 2
a &= 0xFF   -- a = a & 0xFF
a |= 0x100  -- a = a | 0x100
a ^= 0x0F   -- 按位异或赋值
a >>= 2     -- a = a >> 2
a <<= 1     -- a = a << 1
a ..= "suffix"  -- a = a .. "suffix"
a ^= 2      -- a = a ^ 2
local b
b ??= 100   -- b = b ?? 100 (空值合并赋值)
```

13 种复合赋值(不含 ??= 为 12 种标准复合赋值，加 ??= 共 13 种)已完整实现于 `compoundassign()` 函数。

### 2.2 自增/自减 (Increment/Decrement)

Token: `TK_PLUSPLUS`

解析位置: `simpleexp()` 函数中

```lua
local a = 10
a++         -- 后缀自增 (语句级)
```

**限制**:
- 仅支持后缀 `var++` (语句级)
- 自减 `--` 未实现 (词法层无 `TK_MINUSMINUS` 定义)
- 前缀自增 `++var` 未实现
- 表达式内自增返回值未实现

### 2.3 太空船操作符 (Spaceship Operator)

Token: `TK_SPACESHIP` `<=>`

编译为: `OP_SPACESHIP` 虚拟机指令

```lua
local cmp = 10 <=> 20   -- 返回 -1 (小于)
local cmp = 20 <=> 20   -- 返回 0  (等于)
local cmp = 30 <=> 20   -- 返回 1  (大于)
```

### 2.4 空值合并 (Null Coalescing)

Token: `TK_NULLCOAL` `??`

编译方式: `luaK_goifnil()` 生成 nil 检查跳转逻辑

```lua
local val = nil
local res = val ?? "default"  -- "default"
local res = 0 ?? "default"    -- 0 (注意: 0 不是 nil)
```

空值合并复合赋值 `TK_NULLCOALEQ` `??=`:

```lua
local val = nil
val ??= 100    -- val = 100
local val = 0
val ??= 100    -- val 仍为 0
```

### 2.5 可选链 (Optional Chaining)

Token: `TK_OPTCHAIN` `?.`

编译方式: 生成 `OP_TESTNIL` 指令检查 nil

```lua
local config = { server = { port = 8080 } }
local port = config?.server?.port        -- 8080
local timeout = config?.client?.timeout  -- nil (无异常)

-- 可选调用
local obj = nil
obj?.method()  -- 安全跳过
```

### 2.6 管道操作符 (Pipe Operators)

Token: `TK_PIPE` `|>` 和 `TK_REVPIPE` `<|`

解析函数: `pipeexp()` 和 `revpipe()` 在 `lparser.c` 中

```lua
local function double(x) return x * 2 end
local result = 10 |> double     -- double(10) = 20

-- 链式管道
local result = 10 |> double |> double  -- double(double(10)) = 40

-- 反向管道
local result = double <| 10      -- double(10) = 20
```

### 2.7 安全管道 (Safe Pipe)

Token: `TK_SAFEPIPE` `|?>`

解析函数: `safepipe()` 在 `lparser.c` 中

```lua
local maybe_nil = nil
local result = maybe_nil |?> print  -- 不执行 (nil短路)

local value = "hello"
value |?> print  -- 执行 print("hello")
```

### 2.8 海象操作符 (Walrus Operator)

Token: `TK_WALRUS` `:=`

解析函数: `walrusstat()` / `walrusexpr()` 在 `lparser.c` 中

```lua
-- 独立语句
x := 100

-- 表字段赋值
t := {name = "test"}
t.name := "updated"

-- 数组索引赋值
arr := {1, 2, 3}
arr[1] := 100

-- 条件中使用
if (x := 100) > 50 then
    print(x)  -- 100
end

-- while 循环条件
while (count := count + 1) <= 3 do
    print(count)
end

-- 表达式内使用
print((a := 10) + (b := 20))  -- 30
```

### 2.9 三元条件表达式 (Ternary)

解析函数: `cond_expr()` 在 `lparser.c` 中

```lua
local level = is_debug ? 10 : 0
local msg = (score > 60) ? "pass" : "fail"

-- 可嵌套
local grade = (score > 90) ? "A" : (score > 80) ? "B" : "C"
```

### 2.10 展开运算符 (Spread)

```lua
local arr1 = {1, 2}
local arr2 = {3, 4}
local combined = { 0, ...arr1, ...arr2 }  -- {0, 1, 2, 3, 4}

local function sum(a, b, c)
    return a + b + c
end
print(sum(1, ...arr2))  -- sum(1, 3, 4) = 8
```

### 2.11 `in` 操作符

Token: `TK_IN`，编译为: `OPR_IN` 二元运算

```lua
local arr = {1, 2, 3}
if 3 in arr then print("found") end

local str = "Hello"
if "He" in str then print("found") end
```

### 2.12 `is` 和 `instanceof` 操作符

Token: `TK_IS` 和 `TK_INSTANCEOF`，均编译为: `OPR_IS`

```lua
local obj = SomeClass()
if obj is SomeClass then print("yes") end
if obj instanceof SomeClass then print("yes") end
```

### 2.13 切片操作 (Slice)

解析函数: `sliceexpr()`，编译为: `OP_SLICE`

```lua
local arr = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10}
local s1 = arr[1:5]      -- {1, 2, 3, 4, 5}
local s2 = arr[1:10:2]   -- {1, 3, 5, 7, 9}
local s3 = arr[5:]       -- {5, 6, 7, 8, 9, 10}
local s4 = arr[:5]       -- {1, 2, 3, 4, 5}
local s5 = arr[::-1]     -- {10, 9, 8, 7, 6, 5, 4, 3, 2, 1}
```

---

## 3. 函数扩展

### 3.1 箭头函数 (Arrow Function)

Token: `TK_ARROW` / `TK_MEAN` `=>`

```lua
-- 单表达式箭头
local add = (a, b) => a + b

-- 带花括号体
local greet = (name) => { return "Hello, " .. name }

-- 无参箭头
local now = () => os.time()

-- 单参(可省略括号)
local double = x => x * 2

-- 箭头风格的函数表达式 ->(参数){语句}
local log = ->(msg) { print("[LOG]: " .. msg) }
```

### 3.2 Lambda 表达式

关键字: `lambda`

```lua
local sq = lambda(x): x * x
local add = lambda(a, b): a + b

-- 多语句体
local process = lambda(x):
    local y = x * 2
    return y + 1
end
```

### 3.3 C 风格函数定义

解析函数: `declaration_stat()` + `cpp_parlist()`

```lua
int sum(int a, int b) {
    return a + b;
}

void greet(string name) {
    print("Hello, " .. name);
}

float divide(double a, double b) {
    if b == 0 then return 0 end
    return a / b;
}
```

支持类型前缀: `int`, `void`, `float`, `double`, `char`, `long`, `bool`, `string`

### 3.4 泛型函数 (Generic Function)

```lua
local function Factory(T)(val)
    return { type = T, value = val }
end
local obj = Factory("int")(99)

-- 带约束的泛型
local function Container(T) where T ~= nil (val: T): T
    return val
end
```

### 3.5 Async/Await

关键字: `TK_ASYNC` / `TK_AWAIT`

`async` 编译为: `OP_ASYNCWRAP`
`await` 编译为: `OPR_AWAIT` → `coroutine.yield` 调用

```lua
async function fetchData(url)
    local data = await(http.get(url))
    return data
end

-- 嵌套异步调用
async function processAll()
    local a = await(fetchData("url1"))
    local b = await(fetchData("url2"))
    return a .. b
end

-- Promise 风格 (来自 asyncio 库)
local async = require("asyncio")
local p = async.new(function(resolve, reject)
    -- 异步操作
    resolve("ok")
end)
```

### 3.6 Infix 函数 (中缀函数调用)

支持 Kotlin 风格的 infix 语法：`receiver functionName argument`

```lua
local result = 10 infix_add 20   -- 等价于 infix_add(10, 20)
```

解析位置: `exprstat()` 中的 infix 链循环。

---

## 4. 面向对象 (OOP)

### 4.1 类定义 (Class)

解析函数: `classstat()`，生成 `OP_NEWCLASS` 指令

```lua
-- 基础类
class Person
    function __init__(self, name)
        self.name = name
    end

    function greet(self)
        print("Hello, I'm " .. self.name)
    end
end

local p = Person("Alice")
p:greet()
```

### 4.2 继承 (Inheritance)

```lua
class Animal
    function speak(self) return "..." end
end

class Dog extends Animal
    function speak(self)
        return "Woof!"
    end
end
```

编译为: `OP_INHERIT` 指令

### 4.3 接口 (Interface)

解析函数: `interfacestat()`，生成 `OP_SETIFACEFLAG`

```lua
interface Drawable
    function draw(self)
end

class Circle implements Drawable
    function draw(self)
        return "Drawing circle"
    end
end
```

支持多接口: `class X implements A, B`

### 4.4 访问修饰符

`private`, `public`, `protected`, `static` 支持:

```lua
class Example
    private _secret = 0
    public name = ""
    protected _id = 0

    static function create()
        return new Example()
    end

    -- private 方法
    private function helper(self)
        return self._secret * 2
    end
end
```

### 4.5 类修饰符

`sealed`, `final`, `abstract` 支持:

```lua
sealed class NoExtend  -- 不可被继承
    function test(self) return "sealed" end
end

abstract class Base
    function must_impl(self)
        -- 子类必须实现
    end
end
```

### 4.6 Getter/Setter 属性

```lua
class Circle
    private _radius = 0

    get radius(self)
        return self._radius
    end

    set radius(self, v)
        if v >= 0 then self._radius = v end
    end
end
```

### 4.7 `super` 和 `new` 表达式

```lua
class Parent
    function value(self) return 100 end
end

class Child extends Parent
    function value(self)
        return super.value(self) + 50  -- super: 调用父类方法
    end
end

local obj = new Child()  -- new: 创建实例
```

### 4.8 类型推断 - `auto` 和 `var`

```lua
auto x = 100       -- 推导为 int
auto y = "hello"   -- 推导为 string
var z = {1, 2}     -- 推导为 table
```

---

## 5. 结构体与类型

### 5.1 struct (结构体)

解析函数: `structstat()`

```lua
struct Point {
    int x;
    int y;
}
local p = Point()
p.x = 10

-- 泛型结构体
struct Pair(T) {
    T first;
    T second;
}
local ip = Pair("int")({first = 1, second = 2})
```

### 5.2 superstruct (增强表)

解析函数: `superstructstat()`，生成 `OP_NEWSUPER` 指令

```lua
superstruct MetaPoint [
    x: 0,
    y: 0,
    ["move"]: function(self, dx, dy)
        self.x = self.x + dx
        self.y = self.y + dy
    end
]
```

### 5.3 concept (类型谓词)

```lua
concept IsPositive(x)
    return x > 0
end

-- 单表达式形式
concept IsEven(x) = x % 2 == 0
```

### 5.4 enum (枚举)

```lua
enum Color {
    Red,        -- 0
    Green,      -- 1
    Blue = 10   -- 10
}
```

### 5.5 namespace (命名空间)

解析函数: `namespacestat()`，生成 `OP_NEWNAMESPACE`

```lua
namespace MyLib {
    function test() return "test" end
    local secret = 42

    namespace Inner {
        function helper() return 99 end
    }
}
```

### 5.6 using (命名空间导入)

```lua
using namespace MyLib       -- 导入所有成员
using MyLib::test           -- 导入特定成员
using namespace Outer::Inner -- 嵌套导入
```

### 5.7 解构赋值 (take)

解析函数: `takestat_full()`

```lua
-- 表解构
local data = { x = 1, y = 2 }
local take { x, y } = data

-- 数组解构
local arr = {10, 20, 30}
local take [first, , third] = arr   -- first=10, third=30

-- 嵌套解构
local nested = { pos = { x = 1, y = 2 } }
local take { pos = { x, y } } = nested

-- 默认值
local data2 = { name = "default" }
local take { name = "guest", age = 18 } = data2
```

---

## 6. 控制流扩展

### 6.1 switch 语句

```lua
switch (val) do
    case 1:
        print("One")
        break
    case 2, 3, 4:
        print("Two to Four")
        break
    default:
        print("Other")
end

-- 表达式形式
local result = switch (val) do
    case 1: "one"
    case 2: "two"
    default: "unknown"
end
```

### 6.2 when 语句

```lua
do
    when x == 1
        print("x is 1")
    case x == 10
        print("x is 10")
    else
        print("other")
end
```

### 6.3 try-catch-finally

编译为: `pcall` 调用封装

```lua
try
    error("something wrong")
catch(e)
    print("Caught: " .. e)
finally
    print("Cleanup")
end

-- 多个 catch 块
try
    risky_operation()
catch TypeError(e)
    print("Type error: " .. e)
catch(e)
    print("Other error: " .. e)
end
```

### 6.4 defer 语句

基于 to-be-closed 机制实现，在作用域退出时执行:

```lua
defer do print("Exiting scope") end

function test()
    defer do print("Function cleanup") end
    -- 函数操作...
    return "result"
    -- 返回前自动执行 defer 块
end
```

### 6.5 with 语句

环境切换块:

```lua
local ctx = { val = 10 }
with (ctx) {
    print(val)  -- 10 (从 ctx 查找变量)
}
```

### 6.6 continue 语句

编译为 `goto continue`，各类循环自动创建 `continue` 标签:

```lua
for i = 1, 10 do
    if i % 2 == 0 then
        continue
    end
    print(i)  -- 只打印奇数
end

while condition do
    if skip_this then
        continue
    end
    -- ...
end
```

### 6.7 列表推导式 (List Comprehension)

```lua
local src = {1, 2, 3, 4, 5}
local evens = [for _, v in ipairs(src) do v * 2 if v % 2 == 0]
-- {4, 8}
```

### 6.8 字典推导式 (Dict Comprehension)

```lua
local dict = {a = 1, b = 2}
local inverted = {for k, v in pairs(dict) do v, k}
-- {[1] = "a", [2] = "b"}
```

---

## 7. 字符串扩展

### 7.1 插值字符串 (Interpolation)

Token: `TK_INTERPSTRING`

```lua
local name = "World"
print("Hello, ${name}!")           -- Hello, World!

local calc = "1 + 1 = ${[1+1]}"   -- 1 + 1 = 2

local t = {x = 10, y = 20}
print("x=${t.x}, y=${t.y}")       -- x=10, y=20
```

### 7.2 原生字符串 (Raw String)

Token: `TK_RAWSTRING`，前缀: `_raw`

```lua
local path = _raw"C:\Windows\System32"
-- 等价于常规字符串 "C:\\Windows\\System32"
```

---

## 8. Shell 风格条件测试

```lua
-- 文件测试
if [ -f "config.lua" ] then print("file exists") end
if [ -d "/path/to/dir" ] then print("directory exists") end

-- 字符串测试
if [ "a" == "a" ] then print("strings match") end

-- 数值比较
if [ 10 -gt 5 ] then print("10 > 5") end
if [ 5 -lt 10 ] then print("5 < 10") end
if [ 5 -eq 5 ] then print("5 == 5") end
```

---

## 9. 元编程

### 9.1 command (自定义命令)

```lua
command echo(msg)
    print("[ECHO]: " .. msg)
end
echo "Hello World"  -- [ECHO]: Hello World
```

### 9.2 operator (自定义操作符)

```lua
operator ++ (x)
    return x + 1
end
local result = $$++(10)  -- 11

operator cube (x)
    return x * x * x
end
print($$cube(3))  -- 27
```

### 9.3 预处理器指令

所有预处理器以 `$` 开头:

```lua
-- 编译时常量
$define DEBUG 1
$define VERSION "1.0"

-- 令牌别名
$alias CONST_VAL = 100
$alias TABLE_REF = tbl.field

-- 类型别名
$type MyInt = int
$type Callback = function(int, string): bool

-- 条件编译
$if DEBUG
    print("Debug mode")
$elseif TEST
    print("Test mode")
$else
    print("Release mode")
$end

-- 变量声明
$declare g_var: MyInt
$declare g_func: Callback

-- 文件包含
$include "common.lxclua"

-- 停止编译
$haltcompiler "Custom error message"
```

---

## 10. 内联汇编 (Inline ASM)

关键字: `asm`，解析函数: `asmstat()`

```lua
asm(
    -- 安全分配寄存器
    newreg r0
    newreg r1

    LOADI r0 100
    ADDI r1 r0 50

    -- 编译时循环展开
    rep 5 {
        ADDI r0 r0 1
    }

    -- 条件汇编
    _if 1
       _print "Compiling this block"
    _endif

    _if 0
    _else
       _print "Compile this instead"
    _endif

    RETURN1 r0
)
```

---

## 11. 类型提示 (Type Hints)

```lua
-- 函数参数和返回值类型
local function greet(name: string): string
    return "Hello, " .. name
end

-- 局部变量类型
local x: int = 10
local y: float = 3.14
local flag: bool = true
local arr: [int] = {1, 2, 3}           -- 数组类型
local fn: function(int, int): int = add -- 函数类型
```

支持类型: `int`, `float`, `double`, `char`, `long`, `bool`, `string`, `void`, `function(...)` , `[T]` (数组)

---

## 12. 代码混淆选项

```lua
-- 通过 string.dump 第三个参数传递混淆标志
local obfuscated = string.dump(func, false,
    OBFUSCATE_CFF              -- 控制流扁平化
    | OBFUSCATE_BLOCK_SHUFFLE  -- 基本块随机排列
    | OBFUSCATE_BOGUS_BLOCKS   -- 插入虚假块
    | OBFUSCATE_STATE_ENCODE   -- 状态变量编码
    | OBFUSCATE_NESTED_DISPATCHER  -- 多层调度器
    | OBFUSCATE_OPAQUE_PREDICATES -- 不透明谓词
    | OBFUSCATE_FUNC_INTERLEAVE   -- 函数交错
    | OBFUSCATE_VM_PROTECT        -- VM 指令集保护
    | OBFUSCATE_BINARY_DISPATCHER -- 二分调度器
    | OBFUSCATE_RANDOM_NOP        -- 随机NOP插入
    | OBFUSCATE_STR_ENCRYPT       -- 字符串加密
)
```

---