# LXCLUA-NCore 教程

## 1. 简介
LXCLUA-NCore 是基于 Lua 5.5 的深度定制版本，提供了大量现代化编程特性，包括面向对象编程、强类型支持、异步/await、模式匹配、宏系统以及内联汇编等。本文档将全面介绍这些扩展关键字及其使用方法。

## 2. 基础语法扩展

### 注释
支持 C 风格的多行注释和单行注释：
```lua
// 这是单行注释
/* 这是
   多行注释 */
```

### 复合赋值运算符
支持类似于 C/C++ 的复合赋值操作：
```lua
local x = 10
x += 5      -- x = x + 5
x -= 2      -- x = x - 2
x *= 3      -- x = x * 3
x /= 2      -- x = x / 2
x //= 2     -- x = x // 2 (整除)
x %= 3      -- x = x % 3
x++         -- 自增 x = x + 1
```

### 字符串插值
使用 `${...}` 在字符串中嵌入变量或表达式：
```lua
local name = "World"
local age = 25
print("Hello, ${name}!")           -- Hello, World!
print("Next year: ${[age + 1]}")   -- Next year: 26
print("Price: $$100")              -- Price: $100 (转义)
```
支持 `_raw` 前缀创建原始字符串（不转义）：
```lua
local path = _raw"C:\Windows\System32"
```

### 条件表达式
```lua
local a = 10
local b = 20
local max = if a > b then a else b end
```

### Switch 语句
`switch` 语句需要带有 `do ... end`：
```lua
local val = "s"
local type_name
switch (val) do
    case 1:
        type_name = "Integer"
    case "s":
        type_name = "String"
    default:
        type_name = "Unknown"
end
```

### When 语句
类似于 `if-elseif-else` 的语法糖，使用 `when ... case ... else` 结构（注意没有 `then`）：
```lua
local x = -5
when x > 0
    print("Positive")
case x < 0
    print("Negative")
else
    print("Zero")
end
```

## 3. 变量与类型系统

### 强类型声明
支持 C 风格的变量声明：
```lua
int i = 10
string s = "text"
```

### 解构赋值 (take)
使用 `take` 关键字解构表或数组：
```lua
-- 字典解构
local data = { x = 10, y = 20, z = 30 }
local take { x, y } = data
print(x, y) -- 10, 20

-- 数组解构 (跳过第二个元素)
local list = { 1, 2, 3 }
local take [ a, , c ] = list
print(a, c) -- 1, 3
```

### Defer 语句
`defer` 用于在当前作用域结束时执行代码（类似 Go）：
```lua
local function test_defer()
    defer print("File closed or cleanup done")
    print("Doing work...")
end
test_defer()
```

### Const 常量
声明不可修改的变量：
```lua
const PI = 3.14159
```

## 4. 数据结构扩展

### Struct 结构体
定义具有特定字段的结构体：
```lua
struct Point {
    x = 0,
    y = 0
}

local p = Point{ x = 10, y = 20 }
print(p.x)
```

## 5. 函数扩展

### 箭头函数
简洁的函数定义语法：
```lua
-- 语句块形式
local log = ->(msg) { print(msg) }

-- 表达式形式（自动返回）
local f = =>(x) { x * x }
```

### 管道操作符
- `|>` (正向管道): 将左侧结果作为右侧函数的第一个参数

```lua
local function add1(x) return x + 1 end
local function mul2(x) return x * 2 end

-- 注意管道调用不要带括号
local res = 5 |> add1 |> mul2

local s2 = "hello"
local res3 = s2 |> string.upper |> string.reverse
```

## 6. 面向对象编程 (OOP)

### 类定义 (class)
支持单继承、接口实现、静态成员、访问控制。
注意类的构造函数名必须是 `__init__`，并使用类名直接调用来进行实例化。

```lua
interface Drawable
    function draw()
end

class Shape implements Drawable
    function draw()
        print("Drawing shape")
    end
end

class Circle extends Shape
    private radius = 0

    function __init__(r)
        self.radius = r
    end

    function draw()
        print("Drawing circle: " .. self.radius)
    end

    static function create(r)
        return Circle(r)
    end
end

-- 实例化
local c = Circle.create(10)
c:draw()

local c2 = Circle(20)
c2:draw()
```

### 属性 (Getter/Setter)
```lua
class Person
    private _age = 0

    public get age()
        return self._age
    end

    public set age(v)
        if v >= 0 then self._age = v end
    end
end

local p = Person()
p.age = 25
```

## 7. 错误处理

### Try-Catch-Finally
```lua
try
    error("Something wrong")
catch(e)
    print("Caught error: " .. e)
finally
    print("Cleanup")
end
```

## 8. 模块与命名空间

### Namespace
组织代码的命名空间：
```lua
namespace MyLib {
    function test(x)
        return x * 2
    end
}

local res1 = MyLib::test(10)
print(res1)

using namespace MyLib;
local res2 = test(20)
print(res2)
```

## 9. 元编程与宏

### 自定义关键字 (keyword)
定义新的关键字语法。必须先初始化 `_G._KEYWORDS = {}` 才能使用。
```lua
_G._KEYWORDS = {}

keyword unless(cond, body)
    if not cond then
        body()
    end
end

unless(5 > 10, function()
    print("5 is not greater than 10")
end)
```

### 自定义命令 (command)
定义 Shell 风格的命令。必须先初始化 `_G._CMDS = {}` 才能使用。
```lua
_G._CMDS = {}

command echo(...)
    local args = {...}
    local str = ""
    for i, v in ipairs(args) do
        str = str .. tostring(v) .. (i < #args and " " or "")
    end
    print(str)
end

echo("Hello", "World")
```

### 自定义运算符 (operator)
重载或定义新运算符。必须先初始化 `_G._OPERATORS = {}` 才能使用，调用时需要使用 `$$`。
```lua
_G._OPERATORS = {}

operator ++(x)
    return x + 1
end

local val = 10
local val2 = $$++(val)
```

## 10. 内联汇编 (ASM)
使用 `asm` 块直接编写虚拟机指令：
```lua
local result
asm(
    newreg a;
    newreg b;
    LOADI a 10;
    LOADI b 20;
    ADD a a b;
    MOVE $result a;
)
print("Result from asm: " .. result) -- 30
```
更多详情请参考 `ASM_TUTORIAL.md`。
