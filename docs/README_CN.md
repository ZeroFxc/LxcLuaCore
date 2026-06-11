# LXCLUA-NCore

[![License](https://img.shields.io/badge/License-MIT-blue.svg)](../LICENSE)
[![C Standard](https://img.shields.io/badge/C-C23-blue.svg)]()
[![Platform](https://img.shields.io/badge/Platform-Cross--Platform-green.svg)]()

基于 **Lua 5.5 (Custom)** 的高性能嵌入式脚本引擎，增强了安全性、扩展库和优化的字节码编译。

[English Documentation](README.md)

---

## 功能特性

### 核心增强

- **安全编译**: 动态操作码映射、时间戳加密、SHA-256 完整性校验。
- **自定义 VM**: 实现 XCLUA 指令集，采用 64 位指令格式，优化调度。
- **语法扩展**: 包含类、Switch、Try-Catch、箭头函数、管道操作符等现代语言特性。
- **Shell 风格条件测试**: 内置支持 Shell 风格的测试表达式（例如 `[ -f "file.txt" ]`）。
- **代码混淆**: 控制流扁平化、基本块洗牌、虚假块、VM 保护和字符串加密。
- **字节码转 C 代码生成 (tcc)**: 将 Lua 字节码转换为 C 源代码，便于嵌入 C 项目或外部编译优化。

### 扩展模块

| 模块 | require 名称 | 描述 |
|------|-------------|------|
| `bit` / `bit32` | `require("bit")` | 位运算库 |
| `bool` | `require("bool")` | 布尔增强 |
| `userdata` | `require("userdata")` | 二进制数据序列化 |
| `smgr` | `require("smgr")` | 内存管理工具 |
| `process` | `require("process")` | 进程管理 (Linux/Windows) |
| `http` | `require("http")` | HTTP 客户端/服务端和 Socket |
| `thread` | `require("thread")` | 多线程支持，包含互斥锁、条件变量和读写锁 |
| `fs` | `require("fs")` | 文件系统操作 |
| `struct` | `require("struct")` | C 风格结构体和数组 |
| `ptr` | `require("ptr")` | 指针操作库 |
| `vm` | `require("vm")` | VM 内省和字节码操作 |
| `jit` | `require("jit")` | 即时编译 (sljit-based JIT) |
| `tcc` | `require("tcc")` | 字节码转 C 代码生成 |
| `ByteCode` | `require("ByteCode")` | 字节码操作和分析 |
| `vmprotect` | `require("vmprotect")` | 基于 VM 的代码保护 |
| `translator` | `require("translator")` | 代码翻译工具 |
| `crypto` | `require("crypto")` | 密码算法库 (SHA-256, AES, HMAC, CRC32, CSPRNG) |
| `uuid` | `require("uuid")` | UUID 生成 |
| `rsa` | `require("rsa")` | RSA 非对称加密 |
| `ecc` | `require("ecc")` | ECC 椭圆曲线加密 |
| `lexer` | `require("lexer")` | 词法分析和 AST 操作 |
| `asyncio` | `require("asyncio")` | 异步 I/O 和 Promise |
| `wasm3` | `require("wasm3")` | WebAssembly 运行时 (wasm3) |
| `wasmtime` | `require("wasmtime")` | WebAssembly 运行时 (wasmtime) |
| `lua2wasm` | `require("lua2wasm")` | Lua 到 WASM 编译器 |
| `quickjs` | `require("quickjs")` | QuickJS JavaScript 引擎集成 |
| `vmcustom` | `require("vmcustom")` | 自定义操作码扩展系统 |
| `nativevm` | `require("nativevm")` | 原生 VM 接口 |
| `nativeparser` | `require("nativeparser")` | 原生解析器接口 |
| `logtable` | `require("logtable")` | 日志表支持 |
| `libc` | `require("libc")` | C 标准库调用接口 (MinGW) |

---

## 语法扩展

LXCLUA-NCore 引入了现代语言特性以扩展 Lua 5.5。

> 实现状态图例: [完全实现] [部分实现] [未实现]
>
> 以下状态基于对 `lparser.c`、`lcode.c`、`llex.c`、`lvm.c` 源码的实际验证。

### 1. 扩展操作符

支持复合赋值、自增、太空船操作符、空值合并、可选链、管道操作符和海象操作符。

```lua
-- 复合赋值 [完全实现] (支持13种: += -= *= /= //= %= &= |= ^= >>= <<= ..= ??=)
local a = 10
a += 5          -- a = 15

-- 自增 [部分实现: 仅后缀 var++，语句级]
a++             -- a = 16

-- 自减 (--) 未实现: 词法层无 TK_MINUSMINUS 定义
-- 前缀 ++var / --var 未实现
-- a--           -- 语法错误
-- ++a           -- 语法错误

-- 太空船操作符 [完全实现] (-1, 0, 1)
local cmp = 10 <=> 20  -- -1

-- 空值合并 [完全实现] (含 ??= 复合赋值)
local val = nil
local res = val ?? "default"  -- "default"

-- 可选链 [完全实现] (含 ?.() 可选调用)
local config = { server = { port = 8080 } }
local port = config?.server?.port  -- 8080
local timeout = config?.client?.timeout  -- nil

-- 管道操作符 [完全实现] (含反向管道 <|)
local function double(x) return x * 2 end
local result = 10 |> double  -- 20

-- 安全管道 [完全实现] (如果为 nil 则跳过)
local maybe_nil = nil
local _ = maybe_nil |?> print  -- (不执行)

-- 海象操作符 [完全实现]
local x
-- 独立语句
x := 100
-- 表字段赋值
t := {name = "test"}
t.name := "updated"
-- 数组索引赋值
arr := {1, 2, 3}
arr[1] := 100
-- 在表达式中使用
if (x := 100) > 50 then
    print(x) -- 100
end
-- 支持 while 循环条件
while (count := count + 1) <= 3 do
    print(count)
end
-- 支持 repeat-until
repeat
    i = i + 1
until i >= 3
-- 支持函数参数中
print((a := 10) + (b := 20))  -- 30
```

### 2. 增强字符串

- **插值** [完全实现]: 字符串内使用 `${var}` 或 `${[expr]}`。
- **原生字符串** [完全实现]: 前缀为 `_raw`，忽略转义序列。

```lua
local name = "World"
print("Hello, ${name}!")  -- Hello, World!

local calc = "1 + 1 = ${[1+1]}"  -- 1 + 1 = 2

local path = _raw"C:\Windows\System32"
```

### 3. 函数特性

支持箭头函数、Lambda、C 风格定义、泛型和 async/await。

```lua
-- 箭头函数 [完全实现] (多种语法: (a,b)=>expr, (a)=>expr, =>(a){expr}, ->{stat})
local add = (a, b) => a + b
local log = ->(msg) { print("[LOG]: " .. msg) }

-- Lambda 表达式 [完全实现]
local sq = lambda(x): x * x

-- C 风格函数 [完全实现]
int sum(int a, int b) {
    return a + b;
}

-- 泛型函数 [完全实现] (含约束、映射、泛型struct)
local function Factory(T)(val)
    return { type = T, value = val }
end
local obj = Factory("int")(99)

-- Async/Await [完全实现] (async 使用 OP_ASYNCWRAP; await 编译为 coroutine.yield)
async function fetchData(url)
    local data = await(http.get(url))
    return data
end
```

### 4. 面向对象编程 (OOP)

完整的类和接口系统，支持修饰符（`private`、`public`、`protected`、`static`、`final`、`abstract`、`sealed`）和属性（`get`/`set`）。

```lua
interface Drawable  -- [完全实现]
    function draw(self)
end

class Shape implements Drawable  -- [完全实现] extends/implements 均支持
    function draw(self)
        -- 类似抽象方法的行为
    end
end

-- 密封类 [完全实现] (sealed 修饰符)
sealed class Circle extends Shape
    private _radius = 0    -- [完全实现] private
    protected _id = 0      -- [完全实现] protected

    function __init__(self, r)
        self._radius = r
    end

    -- 带 Getter/Setter 的属性 [完全实现]
    get radius(self)
        return self._radius
    end

    set radius(self, v)
        if v >= 0 then self._radius = v end
    end

    function draw(self)
        super.draw(self)   -- [完全实现] super 表达式 (含 super() 构造函数调用)
        return "Drawing circle: " .. self._radius
    end

    static function create(r)  -- [完全实现] static
        return new Circle(r)   -- [完全实现] new 表达式
    end
end

local c = Circle.create(10)
c.radius = 20
print(c.radius)  -- 20

-- instanceof 检查 [完全实现] (与 is 操作符等价)
if c instanceof Circle then
    print("c is a Circle")
end
-- 也可以使用 is 操作符
if c is Circle then
    print("c is a Circle")
end
```

### 5. 结构体与类型

```lua
-- 结构体 [完全实现] (含泛型参数)
struct Point {
    int x;
    int y;
}
local p = Point()
p.x = 10

-- 概念 [完全实现] (含表达式体和块体)
concept IsPositive(x)
    return x > 0
end
-- 或单表达式形式
concept IsEven(x) = x % 2 == 0

-- SuperStruct [完全实现] (OP_NEWSUPER)
superstruct MetaPoint [
    x: 0,
    y: 0,
    ["move"]: function(self, dx, dy)
        self.x = self.x + dx
        self.y = self.y + dy
    end
]

-- 枚举 [完全实现] (含自动递增和显式赋值)
enum Color {
    Red,
    Green,
    Blue = 10
}

-- 解构赋值 [完全实现] (含嵌套解构、默认值、数组解构)
local data = { x = 1, y = 2 }
local take { x, y } = data

-- 数组解构 [完全实现]
local arr = {10, 20, 30}
local take [first, , third] = arr

-- 解构赋值默认值 [完全实现]
local data2 = { name = "default" }
local take { name = "guest", age = 18 } = data2

-- 展开运算符 [完全实现] (混用多个 spread 或在 spread 后使用多返回值函数时存在限制)
local arr1 = {1, 2}
local arr2 = {3, 4}
local combined = { 0, ...arr1, ...arr2 }  -- 单值 spread 可正常工作

local function sum(a, b, c) return a + b + c end
print(sum(1, ...arr2))  -- 函数参数中的 spread 可正常工作
```

### 6. 控制流

```lua
-- Switch 语句 [完全实现] (含模式匹配、表达式形式、fallthrough)
switch (val) do
    case 1:
        print("One")
        break
    default:
        print("Other")
end

-- When 语句 [完全实现] (if 的变体，用 case 替代 elseif)
do
    when x == 1
        print("x is 1")
    case x == 10
        print("x is 10")
    else
        print("other")
end

-- Try-Catch-Finally [完全实现] (转换为 pcall 调用)
try
    error("Error")
catch(e)
    print("Caught: " .. e)
finally
    print("Cleanup")
end

-- Defer [完全实现] (基于 to-be-closed 机制)
defer do print("Executes at scope exit") end

-- With 语句 [完全实现] (环境切换)
local ctx = { val = 10 }
with (ctx) {
    print(val) -- 10
}

-- 命名空间 [完全实现] 和 Using [完全实现]
namespace MyLib {
    function test() return "test" end
}
using namespace MyLib; -- 导入所有
-- using MyLib::test;  -- 导入特定成员

-- 三元条件表达式 [完全实现]
local is_debug = true
local level = is_debug ? 10 : 0

-- 列表推导式 [完全实现]
local src = {1, 2, 3, 4, 5}
local evens = [for _, v in ipairs(src) do v * 2 if v % 2 == 0]

-- 字典推导式 [完全实现]
local dict = {a = 1, b = 2}
local inverted = {for k, v in pairs(dict) do v, k}

-- Continue 语句 [完全实现] (实现为 goto continue)
for i = 1, 10 do
    if i % 2 == 0 then
        continue
    end
    print(i)
end
```

### 7. Shell 风格测试 [完全实现]

内置使用 `[ ... ]` 语法的条件测试，编译为 `__test__()` 调用。

```lua
if [ -f "config.lua" ] then
    print("Config file exists")
end

if [ "a" == "a" ] then
    print("Strings match")
end

if [ 10 -gt 5 ] then
    print("10 > 5")
end
```

### 8. 元编程和宏

```lua
-- 自定义命令 [完全实现]
command echo(msg)
    print(msg)
end
echo "Hello World"

-- 自定义操作符 [完全实现]
operator ++ (x)
    return x + 1
end
-- 使用 $$ 前缀调用
local res = $$++(10)

-- 预处理器指令 [完全实现] (大部分完整)
$define DEBUG 1          -- 编译时常量
$alias CONST_VAL = 100  -- 令牌序列别名
$type MyInt = int        -- 命名类型定义

$if DEBUG                -- 条件编译 (含 $elseif)
    print("Debug mode")
$else
    print("Release mode")
$end

$declare g_var: MyInt    -- 变量声明 (含类型提示和 nodiscard)

-- 对象宏 [部分实现] (仅在宏调用表达式中处理，非独立预处理器指令)
local x = 10
local obj = $object(x) -- {x=10}
```

### 9. 内联汇编 (Inline ASM) [完全实现]

直接编写 VM 指令。使用 `newreg` 安全分配寄存器。
支持伪指令如 `rep`, `_if`, `_print`。

```lua
asm(
    newreg r0
    LOADI r0 100

    -- 编译时循环
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

### 10. 切片操作 [完全实现]

Python 风格的切片语法，支持表和字符串。使用 `OP_SLICE` 虚拟机指令。

```lua
local arr = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10}

local slice1 = arr[1:5]      -- {1, 2, 3, 4, 5}
local slice2 = arr[1:10:2]   -- {1, 3, 5, 7, 9}
local slice3 = arr[5:]       -- {5, 6, 7, 8, 9, 10}
local slice4 = arr[:5]       -- {1, 2, 3, 4, 5}
local slice5 = arr[::-1]     -- {10, 9, 8, 7, 6, 5, 4, 3, 2, 1}
```

### 11. `in` 操作符 [完全实现]

检查值是否存在于容器中。作为二元运算符注册，优先级与比较运算符同级。

```lua
local arr = {1, 2, 3, 4, 5}
if 3 in arr then
    print("3 is in arr")
end

local str = "Hello World"
if "World" in str then
    print("Found 'World'")
end
```

### 12. 类型提示 [完全实现]

支持类型注解和类型检查，含类型兼容性验证。

```lua
local function greet(name: string): string
    return "Hello, " .. name
end

local x: int = 10
local y: float = 3.14
local flag: bool = true
```

---

## 实现状态总览

以下表格汇总了所有文档中声明的语法特性在解析器 (`lparser.c`)、代码生成 (`lcode.c`)、词法分析 (`llex.c`) 和虚拟机 (`lvm.c`) 中的实际支持情况。

### 完整实现的特性

| 特性 | 关键实现 | 备注 |
|------|----------|------|
| 复合赋值 (`+=` 等 13 种) | `compoundassign()` | 含 `??=` |
| 太空船操作符 (`<=>`) | `OPR_SPACESHIP` -> `OP_SPACESHIP` | 三路比较 |
| 空值合并 (`??`) | `OPR_NULLCOAL` -> `luaK_goifnil` | 含 `??=` |
| 可选链 (`?.`) | `TK_OPTCHAIN` -> `OP_TESTNIL` | 含 `?.()` 调用 |
| 管道操作符 (`|>`) | `luaK_pipe()` | 含反向管道 `<|` |
| 安全管道 (`|?>`) | `luaK_safepipe()` | nil 短路 |
| 三元条件 (`?:`) | `cond_expr()` | 短路求值 |
| 展开运算符 (`...`) | `TK_DOTS` 前瞻 | 单值 spread 正常; 混用多个 spread 或与多返回值混用时有限制 |
| `in` 操作符 | `OPR_IN` | 二元运算符 |
| `is` 操作符 | `OPR_IS` | 类型检查 |
| `instanceof` 操作符 | `TK_INSTANCEOF` -> `OPR_IS` | 与 `is` 等价 |
| 切片操作 (`[a:b:c]`) | `sliceexpr()` -> `OP_SLICE` | Python 风格 |
| switch 语句 | `switchstat()` | 含模式匹配、表达式形式 |
| when 语句 | `whenstat()` | `if` 变体 |
| try-catch-finally | `trystat()` | pcall 转换 |
| defer 语句 | `deferstat()` | to-be-closed 机制 |
| with 语句 | `withstat()` | 环境切换 |
| namespace | `namespacestat()` -> `OP_NEWNAMESPACE` | 含参数注入 |
| using | `usingstat()` -> `OP_LINKNAMESPACE` | 含 `using Name::Member` |
| continue 语句 | `goto continue` | 各类循环自动创建标签 |
| 列表推导式 | `[for...do...if]` | 闭包+表构造 |
| 字典推导式 | `{for...do...if}` | 闭包+表构造 |
| 箭头函数 (`=>`) | 多种语法分支 | 含泛型箭头 |
| lambda 表达式 | `lambda_body()` | 含 `=>` 语法糖 |
| C 风格函数定义 | `declaration_stat()` + `cpp_parlist()` | 类型前缀+花括号体 |
| 泛型函数 | `is_generic_factory` 分支 | 含约束、映射 |
| async/await | `OP_ASYNCWRAP` + `OPR_AWAIT` | await 编译为 `coroutine.yield` |
| class 类定义 | `classstat()` -> `OP_NEWCLASS` | 完整 OOP |
| interface 接口 | `interfacestat()` -> `OP_SETIFACEFLAG` | 方法签名注册 |
| sealed/final/abstract | `CLASS_FLAG_*` | 类级别修饰符 |
| extends/implements | `OP_INHERIT` / `OP_IMPLEMENT` | 多接口支持 |
| private/protected/public/static | 分表存储 | 互斥检查 |
| get/set 属性 | `class_getter()` / `class_setter()` | 含访问级别 |
| super 表达式 | `superexpr()` | 含 `super()` 构造函数 |
| new 表达式 | `SKW_NEW` 软关键字 | 类实例化 |
| struct 结构体 | `structstat()` | 含泛型参数 |
| superstruct | `superstructstat()` -> `OP_NEWSUPER` | 增强表定义 |
| concept 概念 | `conceptstat()` | 表达式体+块体 |
| enum 枚举 | `enumstat()` | 自动递增+显式赋值 |
| 解构赋值 (take) | `takestat_full()` | 含嵌套解构、默认值、数组解构、跳过元素 |
| 类型提示 | `gettypehint()` / `checktypehint()` | 含兼容性检查 |
| 字符串插值 | `TK_INTERPSTRING` | `${var}` + `${[expr]}` |
| 原生字符串 | `TK_RAWSTRING` | 不处理转义 |
| Shell 风格测试 | `[ test_expr ]` -> `__test__()` | 文件/数值/字符串/类型测试 |
| command 命令 | `commandstat()` -> `OP_GETCMDS` | 注册到 registry 的 `LXC_CMDS` 表 |
| operator 自定义操作符 | `operatorstat()` -> `OP_GETOPS` | 注册到 registry 的 `LXC_OPERATORS` 表 |
| 预处理器指令 | `constexprstat()` 系列 | `$define/$alias/$type/$if/$else/$end/$declare/$include/$haltcompiler` |
| 内联汇编 (asm) | `asmstat()` + 完整上下文系统 | 标签/常量/寄存器/对齐 |
| newreg | asm 块内伪指令 | 安全分配寄存器 |

### 部分实现的特性

| 特性 | 实现状态 | 缺失部分 |
|------|----------|----------|
| 自增/自减 (`++`/`--`) | 仅实现后缀 `var++`（语句级） | 自减 `--` 未实现（无 `TK_MINUSMINUS`）; 前缀 `++var`/`--var` 未实现; 表达式级返回值未实现 |
| 海象操作符 (`:=`) | 完整支持 | 支持 `while (x := expr)`、`repeat (x := expr)`、`if (x := expr)` 及表达式内使用 |
| `$object` 宏 | 仅在宏调用表达式中处理 | 非独立预处理器指令 |

### 未实现的特性

| 特性 | 说明 | 替代方案 |
|------|------|----------|
| 自减运算符 `--` | 词法层无 `TK_MINUSMINUS` 定义 | 使用 `a -= 1` 替代 `a--` |
| 前缀自增/自减 (`++var`/`--var`) | 未实现 | 使用 `a += 1` 等复合赋值替代 |

---

## 安全特性

### 代码混淆

LXCLUA-NCore 提供多种混淆技术：

| 标志 | 描述 |
|------|------|
| `OBFUSCATE_CFF` | 控制流扁平化 |
| `OBFUSCATE_BLOCK_SHUFFLE` | 随机化基本块顺序 |
| `OBFUSCATE_BOGUS_BLOCKS` | 插入虚假基本块 |
| `OBFUSCATE_STATE_ENCODE` | 混淆状态变量值 |
| `OBFUSCATE_NESTED_DISPATCHER` | 多层调度器 |
| `OBFUSCATE_OPAQUE_PREDICATES` | 不透明谓词 |
| `OBFUSCATE_FUNC_INTERLEAVE` | 函数交错 |
| `OBFUSCATE_VM_PROTECT` | VM 保护（自定义指令集） |
| `OBFUSCATE_BINARY_DISPATCHER` | 二分查找调度器 |
| `OBFUSCATE_RANDOM_NOP` | 插入随机 NOP 指令 |
| `OBFUSCATE_STR_ENCRYPT` | 字符串常量加密 |

```lua
-- 混淆字节码
local obfuscated = string.dump(func, false, OBFUSCATE_CFF | OBFUSCATE_STR_ENCRYPT)
```

### 密码算法库

内置统一密码算法库 `crypto`，采用子表结构组织各类算法。

```lua
local crypto = require("crypto")

-- SHA-256 哈希
local hash = crypto.sha256("Hello World")         -- 返回 hex 字符串
local raw = crypto.sha256.raw("Hello World")      -- 返回原始字节

-- HMAC-SHA256
local mac = crypto.hmac.sha256("key", "data")     -- 返回 hex 字符串
local raw_mac = crypto.hmac.sha256_raw("key", "data")  -- 返回原始字节

-- AES 加密 (ECB/CBC/CTR 模式)
local key = "16-byte-key-1234"
local iv = "initial-vector-16"                    -- CBC/CTR 模式需要
local encrypted = crypto.aes.encrypt(key, plaintext, "CBC", iv)
local decrypted = crypto.aes.decrypt(key, encrypted, "CBC", iv)

-- CRC32 校验
local checksum = crypto.crc32(data)

-- 安全随机数
local rand_hex = crypto.random.hex(16)            -- 16 字节随机 hex
local rand_bytes = crypto.random.bytes(32)        -- 32 字节随机原始数据
local rand_int = crypto.random.int(1, 100)        -- [1, 100] 随机整数
crypto.random.seed(12345)                         -- 手动设置种子
```

### UUID 生成

```lua
local uuid = require("uuid")
local id = uuid.v4()          -- 随机 UUID (版本 4)
local id2 = uuid.v7()         -- 时间有序 UUID (版本 7)
```

---

## 多线程支持

完整的多线程支持，包含同步原语。

```lua
local thread = require("thread")

-- 互斥锁
local m = thread.mutex()
m:lock()
-- 临界区
m:unlock()

-- 条件变量
local cond = thread.cond()
cond:wait(m)
cond:signal()
cond:broadcast()

-- 读写锁
local rwlock = thread.rwlock()
rwlock:rdlock()  -- 读锁
rwlock:wrlock()  -- 写锁
rwlock:unlock()

-- 线程创建
local t = thread.create(function()
    print("Running in thread")
end)
t:join()
```

---

## 字节码转 C 代码生成 (tcc)

`tcc` 模块将 Lua 源码编译为字节码，再将字节码转换为 C 源代码。生成的 C 代码可通过外部 C 编译器编译为原生可执行程序或动态库。此模块名称为历史遗留，与 Tiny C Compiler 无关。

```lua
local tcc = require("tcc")

-- 将 Lua 代码转换为 C 源代码
local c_code = tcc.compile([[
    local function add(a, b)
        return a + b
    end
    return add
]])

-- c_code 为 C 源代码字符串，可写入文件并使用 GCC 等编译
```

### 真正的 JIT 编译 (`jit`)

项目包含独立的 `jit` 模块，基于 sljit 实现真正的即时编译（JIT），在运行时将热点字节码直接编译为原生机器码执行：

```lua
local jit = require("jit")
jit.on()   -- 启用 JIT
jit.off()  -- 禁用 JIT
print(jit.status())  -- 查看 JIT 状态
```

---

## 扩展类型

LXCLUA-NCore 扩展了 Lua 的类型系统：

| 类型 | 描述 |
|------|------|
| `LUA_TSTRUCT` | C 风格结构体 |
| `LUA_TPOINTER` | 原始指针类型 |
| `LUA_TCONCEPT` | 类型谓词概念 |
| `LUA_TNAMESPACE` | 命名空间类型 |
| `LUA_TSUPERSTRUCT` | 增强表定义 |

---

## 大整数支持

任意精度整数运算。

```lua
local bigint = require("bigint")

local a = bigint.new("12345678901234567890")
local b = bigint.new("98765432109876543210")
local c = a + b
print(c:tostring())  -- 111111111011111111100
```

---

## HTTP 和网络

```lua
local http = require("http")

-- HTTP GET
local response = http.get("https://api.example.com/data")
print(response.body)

-- HTTP POST
local result = http.post("https://api.example.com/submit", {
    headers = { ["Content-Type"] = "application/json" },
    body = '{"key": "value"}'
})

-- Socket 操作
local sock = http.socket()
sock:connect("example.com", 80)
sock:send("GET / HTTP/1.0\r\n\r\n")
local data = sock:recv(1024)
sock:close()
```

---

## 文件系统操作

```lua
local fs = require("fs")

-- 文件操作
local content = fs.read("file.txt")
fs.write("output.txt", "Hello World")
fs.append("log.txt", "New entry\n")

-- 目录操作
local files = fs.listdir("/path/to/dir")
fs.mkdir("/path/to/new/dir")
fs.rmdir("/path/to/dir")

-- 路径工具
local exists = fs.exists("file.txt")
local is_dir = fs.isdir("path")
local is_file = fs.isfile("file.txt")
local size = fs.size("file.txt")
```

---

## 字节码操作

```lua
local bytecode = require("ByteCode")

-- 将函数转储为字节码
local bc = bytecode.dump(function() print("Hello") end)

-- 加载字节码
local func = bytecode.load(bc)

-- 分析字节码
local info = bytecode.analyze(func)
print("Instructions:", info.num_instructions)
print("Constants:", info.num_constants)
```

---

## 构建与测试

### 构建

```bash
# Linux
make linux

# Windows (MinGW)
make mingw

# Windows 静态链接 (MinGW)
make mingw-static

# Android (Termux)
make termux

# WebAssembly (主程序)
make wasm

# WebAssembly (LSP 服务)
make wasmlsp

# WebAssembly (最小化构建)
make wasm-minimal

# WebAssembly (C 库构建)
make wasm-c
```

### 构建产物

| 文件 | 描述 |
|------|------|
| `lxclua` / `lxclua.exe` | LXCLUA 解释器 |
| `luac` / `luac.exe` | Lua 字节码编译器 |
| `lbcdump` / `lbcdump.exe` | 字节码分析工具 |
| `lxclua-lsp.exe` | LXCLUA LSP 语言服务 |
| `liblua.a` / `lua55.dll` | Lua 静态库 / 动态库 |
| `lxclua.js` / `lxclua.wasm` | WebAssembly 构建产物 |
| `lxclua-lsp.js` / `lxclua-lsp.wasm` | LSP WebAssembly 构建产物 |

### 验证

运行测试套件以验证所有功能：

```bash
./lxclua tests/verify_docs_full.lua
./lxclua tests/test_parser_features.lua
./lxclua tests/test_advanced_parser.lua
```

### 清理

```bash
make clean
```

---

## API 参考

### 增强内存 API

```c
size_t lua_getmemoryusage(lua_State *L);
void   lua_gc_force(lua_State *L);
void   lua_table_iextend(lua_State *L, int idx, int n);
```

### 混淆 API

```c
int lua_dump_obfuscated(lua_State *L, lua_Writer writer, void *data,
                        int strip, int obfuscate_flags, unsigned int seed,
                        const char *log_path);
```

---

## 许可证

[MIT License](../LICENSE).
Lua original code Copyright (c) PUC-Rio.