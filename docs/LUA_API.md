# LXCLUA-NCore Lua API 完整参考

> 基于 Lua 5.5 Custom 的高性能嵌入式脚本引擎，本文档涵盖所有 `require()` 可加载模块的完整 API 说明。

---

## 目录

- [1. 标准库模块（Lua 兼容）](#1-标准库模块lua-兼容)
- [2. LXCLUA 扩展模块](#2-lxclua-扩展模块)
- [3. 系统集成模块](#3-系统集成模块)
- [4. VM 和字节码模块](#4-vm-和字节码模块)
- [5. 编译和转换模块](#5-编译和转换模块)
- [6. 密码学模块](#6-密码学模块)
- [7. WASM 相关模块](#7-wasm-相关模块)
- [8. 内部与平台相关模块](#8-内部与平台相关模块)
- [附录：模块注册总表](#附录模块注册总表)

---

## 1. 标准库模块（Lua 兼容）

与标准 Lua 5.4+ 行为基本一致的库，兼容 Lua 生态。

### 1.1 base — 基础函数库

| 属性 | 值 |
|------|-----|
| **require 名称** | 自动加载到 `_G`，无需 `require()` |
| **源文件** | `src/stdlib/lbaselib.c` |
| **加载条件** | 始终 |

**主要函数：**

| 函数 | 说明 |
|------|------|
| `assert(v [, message])` | 断言，`v` 为 false/nil 时抛出错误 |
| `error(message [, level])` | 抛出错误，`level` 指定调用栈层级 |
| `pcall(f [, ...])` | 保护模式调用，返回 `true, result` 或 `false, error` |
| `xpcall(f, msgh [, ...])` | 带错误处理函数的保护模式调用 |
| `print(...)` | 打印到标准输出 |
| `type(v)` | 返回值的类型名称字符串 |
| `tostring(v)` | 转换为字符串 |
| `tonumber(v [, base])` | 转换为数字，支持 2-36 进制 |
| `select(index, ...)` | 选择变长参数，`index` 为 `"#"` 时返回参数数量 |
| `ipairs(t)` | 数组迭代器，按数字索引遍历 |
| `pairs(t)` | 通用迭代器，遍历所有键值对 |
| `next(t [, index])` | 返回下一个键值对 |
| `rawget(t, k)` | 绕过元方法获取表字段 |
| `rawset(t, k, v)` | 绕过元方法设置表字段 |
| `rawequal(a, b)` | 绕过元方法比较相等性 |
| `rawlen(v)` | 绕过元方法获取长度 |
| `setmetatable(t, mt)` | 设置元表 |
| `getmetatable(t)` | 获取元表 |
| `collectgarbage([opt [, arg]])` | 垃圾回收控制 |
| `dofile([filename])` | 执行 Lua 文件 |
| `load(chunk [, chunkname [, mode [, env]]])` | 加载代码块 |
| `loadfile([filename [, mode [, env]]])` | 加载文件 |

### 1.2 package — 模块加载

| 属性 | 值 |
|------|-----|
| **require 名称** | `require("package")` |
| **源文件** | `src/stdlib/loadlib.c` |
| **加载条件** | 始终 |

**主要函数/字段：**

| 函数/字段 | 说明 |
|-----------|------|
| `package.path` | Lua 模块搜索路径 |
| `package.cpath` | C 模块搜索路径 |
| `package.loaded` | 已加载模块表 |
| `package.preload` | 预加载函数表 |
| `package.searchers` | 搜索器函数列表 |
| `package.searchpath(name, path)` | 在指定路径中搜索模块文件 |
| `require(modname)` | 加载模块（全局可用） |
| `module(modname)` | 模块定义辅助函数 |

### 1.3 coroutine — 协程

| 属性 | 值 |
|------|-----|
| **require 名称** | `require("coroutine")` |
| **源文件** | `src/stdlib/lcorolib.c` |
| **加载条件** | 始终 |

**主要函数：**

| 函数 | 说明 |
|------|------|
| `coroutine.create(f)` | 创建协程，返回 `thread` 类型 |
| `coroutine.resume(co [, ...])` | 启动/恢复协程执行 |
| `coroutine.yield(...)` | 挂起协程，返回参数给调用者 |
| `coroutine.status(co)` | 返回协程状态：`"running"`/`"suspended"`/`"normal"`/`"dead"` |
| `coroutine.wrap(f)` | 创建协程并返回函数包装器 |
| `coroutine.running()` | 返回当前运行的协程 |
| `coroutine.isyieldable()` | 当前上下文是否可 yield |
| `coroutine.close(co)` | 关闭协程，触发 `__close` 元方法 |

### 1.4 debug — 调试

| 属性 | 值 |
|------|-----|
| **require 名称** | `require("debug")` |
| **源文件** | `src/stdlib/ldblib.c` |
| **加载条件** | 始终 |

**主要函数：**

| 函数 | 说明 |
|------|------|
| `debug.traceback([thread,] [message [, level]])` | 获取调用栈回溯字符串 |
| `debug.getinfo([thread,] f [, what])` | 获取函数/调用栈层级信息 |
| `debug.getlocal([thread,] level, index)` | 获取局部变量名和值 |
| `debug.setlocal([thread,] level, index, value)` | 设置局部变量值 |
| `debug.getupvalue(f, index)` | 获取上值名称和值 |
| `debug.setupvalue(f, index, value)` | 设置上值 |
| `debug.sethook([thread,] hook, mask [, count])` | 设置调试钩子 |
| `debug.gethook([thread])` | 获取当前钩子函数 |
| `debug.getregistry()` | 返回注册表 |
| `debug.getmetatable(value)` | 获取元表 |
| `debug.setmetatable(value, table)` | 设置元表 |
| `debug.upvalueid(f, n)` | 获取上值 ID |
| `debug.upvaluejoin(f1, n1, f2, n2)` | 连接两个上值 |
| `debug.getuservalue(u)` | 获取 userdata 关联值 |
| `debug.setuservalue(u, value)` | 设置 userdata 关联值 |

### 1.5 io — 文件输入输出

| 属性 | 值 |
|------|-----|
| **require 名称** | `require("io")` |
| **源文件** | `src/stdlib/liolib.c` |
| **加载条件** | 始终 |

**主要函数：**

| 函数 | 说明 |
|------|------|
| `io.open(filename [, mode])` | 打开文件，返回文件句柄 |
| `io.close([file])` | 关闭文件 |
| `io.read(...)` | 从标准输入/文件读取 |
| `io.write(...)` | 写入标准输出/文件 |
| `io.lines([filename, ...])` | 返回文件行迭代器 |
| `io.flush()` | 刷新输出缓冲区 |
| `io.seek([whence [, offset]])` | 移动文件指针 |
| `io.tmpfile()` | 创建临时文件 |
| `io.type(obj)` | 检查是否为文件句柄 |
| `io.input([file])` | 设置/获取默认输入文件 |
| `io.output([file])` | 设置/获取默认输出文件 |
| `io.popen(prog [, mode])` | 创建管道 |
| `io.stdin` / `io.stdout` / `io.stderr` | 标准输入/输出/错误文件句柄 |

### 1.6 math — 数学函数

| 属性 | 值 |
|------|-----|
| **require 名称** | `require("math")` |
| **源文件** | `src/stdlib/lmathlib.c` |
| **加载条件** | 始终 |

**主要函数/常量：**

| 函数/常量 | 说明 |
|-----------|------|
| `math.abs(x)` | 绝对值 |
| `math.sin(x)` / `math.cos(x)` / `math.tan(x)` | 三角函数 |
| `math.asin(x)` / `math.acos(x)` / `math.atan(x)` | 反三角函数 |
| `math.atan2(y, x)` | 两参数反正切 |
| `math.sinh(x)` / `math.cosh(x)` / `math.tanh(x)` | 双曲函数 |
| `math.exp(x)` | 指数函数 e^x |
| `math.log(x [, base])` | 对数函数 |
| `math.log10(x)` | 以 10 为底的对数 |
| `math.sqrt(x)` | 平方根 |
| `math.floor(x)` / `math.ceil(x)` | 向下/向上取整 |
| `math.modf(x)` | 返回整数部分和小数部分 |
| `math.fmod(x, y)` | 浮点取模 |
| `math.rad(x)` / `math.deg(x)` | 弧度/角度转换 |
| `math.max(...)` / `math.min(...)` | 最大值/最小值 |
| `math.random([m [, n]])` | 随机数 |
| `math.randomseed(x)` | 设置随机种子 |
| `math.randomseed()` | 自动设置随机种子 |
| `math.type(x)` | 返回数值类型：`"integer"`/`"float"`/`nil` |
| `math.tointeger(x)` | 转换为整数 |
| `math.ult(x, y)` | 无符号比较 |
| `math.pi` | 圆周率 π |
| `math.huge` | 正无穷大 |
| `math.maxinteger` / `math.mininteger` | 最大/最小整数值 |

### 1.7 os — 操作系统

| 属性 | 值 |
|------|-----|
| **require 名称** | `require("os")` |
| **源文件** | `src/stdlib/loslib.c` |
| **加载条件** | 始终 |

**主要函数：**

| 函数 | 说明 |
|------|------|
| `os.clock()` | 返回 CPU 时间（秒） |
| `os.time([table])` | 返回当前时间戳或从表构建时间戳 |
| `os.date([format [, time]])` | 格式化日期时间 |
| `os.difftime(t1, t2)` | 计算时间差 |
| `os.execute([command])` | 执行系统命令 |
| `os.exit([code [, close]])` | 退出程序 |
| `os.getenv(varname)` | 获取环境变量 |
| `os.setenv(varname, value)` | 设置环境变量 |
| `os.remove(filename)` | 删除文件 |
| `os.rename(oldname, newname)` | 重命名文件 |
| `os.tmpname()` | 生成临时文件名 |

### 1.8 string — 字符串

| 属性 | 值 |
|------|-----|
| **require 名称** | `require("string")` |
| **源文件** | `src/stdlib/lstrlib.c` |
| **加载条件** | 始终 |

**主要函数：**

| 函数 | 说明 |
|------|------|
| `string.byte(s [, i [, j]])` | 返回字符的字节码 |
| `string.char(...)` | 从字节码构建字符串 |
| `string.find(s, pattern [, init [, plain]])` | 查找模式匹配位置 |
| `string.format(formatstr, ...)` | 格式化字符串 |
| `string.gmatch(s, pattern)` | 返回模式匹配迭代器 |
| `string.gsub(s, pattern, repl [, n])` | 全局替换 |
| `string.len(s)` | 返回字符串长度 |
| `string.lower(s)` / `string.upper(s)` | 大小写转换 |
| `string.match(s, pattern [, init])` | 返回第一个匹配 |
| `string.rep(s, n [, sep])` | 重复字符串 n 次 |
| `string.reverse(s)` | 反转字符串 |
| `string.sub(s, i [, j])` | 提取子字符串 |
| `string.pack(fmt, ...)` | 打包二进制数据 |
| `string.packsize(fmt)` | 返回打包格式大小 |
| `string.unpack(fmt, s [, pos])` | 解包二进制数据 |
| `string.split(s [, sep])` | 按分隔符拆分字符串 |
| `string.dump(fn [, strip])` | 将函数转储为字节码字符串 |

### 1.9 table — 表操作

| 属性 | 值 |
|------|-----|
| **require 名称** | `require("table")` |
| **源文件** | `src/stdlib/ltablib.c` |
| **加载条件** | 始终 |

**主要函数：**

| 函数 | 说明 |
|------|------|
| `table.insert(t, [pos,] value)` | 插入元素 |
| `table.remove(t [, pos])` | 移除元素 |
| `table.sort(t [, comp])` | 排序表 |
| `table.concat(t [, sep [, i [, j]]])` | 连接表元素为字符串 |
| `table.move(a1, f, e, t [, a2])` | 移动表元素 |
| `table.pack(...)` | 打包变长参数为表 |
| `table.unpack(t [, i [, j]])` | 解包表为多个值 |
| `table.freeze(t)` | 冻结表（只读） |
| `table.clone(t)` | 浅拷贝表 |
| `table.size(t)` | 返回表元素数量 |

### 1.10 utf8 — UTF-8 编码支持

| 属性 | 值 |
|------|-----|
| **require 名称** | `require("utf8")` |
| **源文件** | `src/stdlib/lutf8lib.c` |
| **加载条件** | 始终 |

**主要函数：**

| 函数 | 说明 |
|------|------|
| `utf8.char(...)` | 从 Unicode 码点构建 UTF-8 字符串 |
| `utf8.codes(s)` | 返回码点迭代器 |
| `utf8.codepoint(s [, i [, j]])` | 返回指定位置的 Unicode 码点 |
| `utf8.len(s [, i [, j]])` | 返回 UTF-8 字符数 |
| `utf8.offset(s, n [, i])` | 返回第 n 个字符的字节偏移 |
| `utf8.charpattern` | UTF-8 字符匹配模式（`[\\0-\\x7F\\xC2-\\xF4][\\x80-\\xBF]*`） |

### 1.11 patch — Lua 补丁库

| 属性 | 值 |
|------|-----|
| **require 名称** | `require("patch")` |
| **源文件** | `src/utils/lpatchlib.c` |
| **加载条件** | 始终 |

热修复补丁库，支持运行时替换函数实现，无需重启即可更新代码逻辑。

---

## 2. LXCLUA 扩展模块

LXCLUA-NCore 特有的扩展类型和增强功能模块。

### 2.1 bit / bit32 — 位运算

| 属性 | 值 |
|------|-----|
| **require 名称** | `require("bit")` 或 `require("bit32")` |
| **源文件** | `src/stdlib/lbitlib.c` |
| **加载条件** | 始终 |

提供标准 Lua 5.2 bit32 库的全部函数：

| 函数 | 说明 |
|------|------|
| `bit.band(...)` | 按位与（AND） |
| `bit.bor(...)` | 按位或（OR） |
| `bit.bxor(...)` | 按位异或（XOR） |
| `bit.bnot(x)` | 按位取反（NOT） |
| `bit.lshift(x, n)` | 左移（算术左移） |
| `bit.rshift(x, n)` | 逻辑右移 |
| `bit.arshift(x, n)` | 算术右移（保留符号位） |
| `bit.btest(...)` | 测试位，等价于 `band(...) ~= 0` |
| `bit.extract(x, field [, width])` | 提取位字段 |
| `bit.replace(x, v, field [, width])` | 替换位字段 |
| `bit.lrotate(x, n)` | 左循环移位 |
| `bit.rrotate(x, n)` | 右循环移位 |

### 2.2 bool — 布尔增强

| 属性 | 值 |
|------|-----|
| **require 名称** | `require("bool")` |
| **源文件** | `src/stdlib/lboolib.c` |
| **加载条件** | 始终 |

提供布尔类型相关的增强操作和工具函数，扩展标准 Lua 布尔值的行为。

### 2.3 class — 面向对象类系统

| 属性 | 值 |
|------|-----|
| **类型** | 语法级内置特性，运行时支持由 `src/stdlib/lclass.c` 提供 |
| **加载条件** | 始终 |

`class` 关键字为 LXCLUA 语法扩展，提供完整的 OOP 支持。运行时通过 `lclass.c` 提供类定义、继承链、接口实现、属性 getter/setter、方法调用、super 访问等功能。

**语法特性：**

| 特性 | 说明 |
|------|------|
| `class` | 类定义，支持 `extends`/`implements` |
| `interface` | 接口定义 |
| `private` / `public` / `protected` | 成员访问修饰符 |
| `static` | 静态成员 |
| `sealed` / `final` / `abstract` | 类级别修饰符 |
| `get` / `set` | 属性访问器 |
| `super` | 父类访问 |
| `new` | 类实例化 |
| `instanceof` / `is` | 类型检查 |

### 2.4 struct — C 风格结构体

| 属性 | 值 |
|------|-----|
| **require 名称** | `require("struct")` |
| **源文件** | `src/stdlib/lstruct.c` |
| **加载条件** | 始终 |

定义和操作 C 风格结构体，支持字段类型定义、内存布局、对齐等。

| 函数 | 说明 |
|------|------|
| `struct.define(fields)` | 定义结构体类型，`fields` 为包含 `{name, type}` 的数组 |
| `struct.sizeof(type)` | 获取结构体大小 |
| `struct.new(type, ...)` | 创建结构体实例 |

**语法级特性：** 支持 `struct Name { ... }` 语法糖直接定义结构体，包括泛型参数。

### 2.5 ptr — 指针操作

| 属性 | 值 |
|------|-----|
| **require 名称** | `require("ptr")` |
| **源文件** | `src/stdlib/lptrlib.c` |
| **加载条件** | 始终 |

提供原始指针操作，用于底层内存管理。

| 函数 | 说明 |
|------|------|
| `ptr.alloc(size)` | 分配指定大小的内存 |
| `ptr.free(p)` | 释放内存 |
| `ptr.store(p, offset, value)` | 在指定偏移写入值 |
| `ptr.load(p, offset)` | 从指定偏移读取值 |
| `ptr.add(p, offset)` | 指针偏移运算 |
| `ptr.deref(p)` | 解引用指针 |

### 2.6 userdata — 二进制数据序列化

| 属性 | 值 |
|------|-----|
| **require 名称** | `require("userdata")` |
| **源文件** | `src/stdlib/ludatalib.c` |
| **加载条件** | 始终 |

提供 userdata 类型的元方法和工具函数，支持 userdata 的二进制序列化/反序列化、内存布局分析等。

### 2.7 map — Map 数据结构

| 属性 | 值 |
|------|-----|
| **require 名称** | `require("map")` |
| **源文件** | `src/stdlib/lmaplib.c` + `src/core/lmap.c` |
| **加载条件** | 始终 |

基于哈希表的高效键值对容器，对应 `LUA_TMAP` 类型。提供键值对增删改查、遍历、大小查询等操作。

| 主要操作 | 说明 |
|----------|------|
| 创建/初始化 | 创建 Map 实例 |
| 插入/更新 | 设置键值对 |
| 查找 | 按键获取值 |
| 删除 | 移除键值对 |
| 遍历 | 迭代所有键值对 |
| 大小查询 | 获取元素数量 |

### 2.8 superstruct — 增强表

| 属性 | 值 |
|------|-----|
| **类型** | 语法级内置特性，运行时支持由 `src/stdlib/lsuper.c` 提供 |
| **加载条件** | 始终 |

`superstruct` 关键字定义增强表，对应 `LUA_TSUPERSTRUCT` 类型。增强表具有强类型约束、字段验证、默认值等特性，通过 `OP_NEWSUPER` 虚拟机指令创建。

**语法格式：**
```lua
superstruct Name [
    field1: default_value,
    field2: default_value,
    ["method"]: function(self, ...) ... end
]
```

---

## 3. 系统集成模块

与操作系统和环境交互的模块。

### 3.1 process — 进程管理

| 属性 | 值 |
|------|-----|
| **require 名称** | `require("process")` |
| **源文件** | `src/stdlib/lproclib.c` |
| **加载条件** | Linux 平台 |

进程创建、等待、终止和进程间通信。

| 函数 | 说明 |
|------|------|
| `process.execute(cmd, ...)` | 执行外部命令，返回进程对象 |
| `process.wait(proc)` | 等待进程结束 |
| `process.kill(proc [, signal])` | 终止进程 |

**进程对象方法：**

| 方法 | 说明 |
|------|------|
| `proc:read([mode])` | 读取进程输出 |
| `proc:write(data)` | 写入进程输入 |
| `proc:close()` | 关闭进程管道 |
| `proc:wait()` | 等待进程结束 |

### 3.2 http — HTTP 客户端/服务端和 Socket

| 属性 | 值 |
|------|-----|
| **require 名称** | `require("http")` |
| **源文件** | `src/utils/libhttp.c` |
| **加载条件** | 始终 |

#### 模块级函数

| 函数 | 说明 |
|------|------|
| `http.request(method, url [, options])` | 发起 HTTP 请求 |
| `http.get(url [, options])` | HTTP GET 请求快捷方式 |
| `http.post(url [, options])` | HTTP POST 请求快捷方式 |
| `http.server()` | 创建 HTTP 服务器 |
| `http.websocket(url)` | 创建 WebSocket 连接 |
| `http.socket()` | 创建 TCP Socket |

#### http.url 子表

| 函数 | 说明 |
|------|------|
| `http.url.encode(str)` | URL 编码 |
| `http.url.decode(str)` | URL 解码 |
| `http.url.parse(url)` | 解析 URL 为 table |

#### http.base64 子表

| 函数 | 说明 |
|------|------|
| `http.base64.encode(data)` | Base64 编码 |
| `http.base64.decode(data)` | Base64 解码 |

#### HTTP 服务器对象方法

| 方法 | 说明 |
|------|------|
| `server:route(method, path, handler)` | 注册路由处理 |
| `server:start(port [, host])` | 启动服务器 |
| `server:stop()` | 停止服务器 |

#### HTTP 响应对象方法

| 方法 | 说明 |
|------|------|
| `res:write_head(status [, headers])` | 写入状态码和响应头 |
| `res:write(data)` | 写入响应体 |
| `res:finish()` | 结束响应 |
| `res:set_header(name, value)` | 设置响应头 |
| `res:json(data)` | 发送 JSON 响应 |
| `res:error(status, msg)` | 发送错误响应 |

#### WebSocket 对象方法

| 方法 | 说明 |
|------|------|
| `ws:send(data)` | 发送消息 |
| `ws:close()` | 关闭连接 |

#### Socket 对象方法

| 方法 | 说明 |
|------|------|
| `sock:connect(host, port)` | 连接服务器 |
| `sock:send(data)` | 发送数据 |
| `sock:recv(size)` | 接收数据 |
| `sock:close()` | 关闭连接 |

### 3.3 thread — 多线程

| 属性 | 值 |
|------|-----|
| **require 名称** | `require("thread")` |
| **源文件** | `src/stdlib/lthreadlib.c` |
| **加载条件** | 始终 |

#### 模块级函数

| 函数 | 说明 |
|------|------|
| `thread.create(fn)` | 创建线程，执行 `fn()` |
| `thread.createx(data, fn)` | 创建线程，传递 `data` 给 `fn` |
| `thread.channel([size])` | 创建通道（消息队列），可选缓冲区大小 |
| `thread.pick(channel1, channel2, ...)` | 从多个通道中选择一个可接收的 |
| `thread.on(event, fn)` | 注册事件处理 |
| `thread.over(event, fn)` | 一次性事件处理 |
| `thread.self()` | 返回当前线程对象 |
| `thread.current()` | `self` 的别名 |
| `thread.mutex()` | 创建互斥锁 |
| `thread.cond()` | 创建条件变量 |
| `thread.rwlock()` | 创建读写锁 |
| `thread.semaphore([count])` | 创建信号量 |

#### 线程对象方法

| 方法 | 说明 |
|------|------|
| `thread:join()` | 等待线程结束 |
| `thread:detach()` | 分离线程 |
| `thread:name()` | 获取线程名称 |
| `thread:id()` | 获取线程 ID |

#### 通道对象方法

| 方法 | 说明 |
|------|------|
| `channel:send(value)` / `channel:push(value)` | 发送数据（阻塞） |
| `channel:receive()` / `channel:pop()` | 接收数据（阻塞） |
| `channel:try_send(value)` | 尝试发送（非阻塞，返回 bool） |
| `channel:try_recv()` | 尝试接收（非阻塞） |
| `channel:peek()` | 查看队首元素但不移除 |
| `channel:close()` | 关闭通道 |

#### 互斥锁方法

| 方法 | 说明 |
|------|------|
| `mutex:lock()` | 加锁 |
| `mutex:unlock()` | 解锁 |
| `mutex:trylock()` | 尝试加锁（非阻塞） |

#### 条件变量方法

| 方法 | 说明 |
|------|------|
| `cond:wait(mutex)` | 等待信号 |
| `cond:signal()` | 唤醒一个等待线程 |
| `cond:broadcast()` | 唤醒所有等待线程 |

#### 读写锁方法

| 方法 | 说明 |
|------|------|
| `rwlock:rdlock()` | 获取读锁 |
| `rwlock:wrlock()` | 获取写锁 |
| `rwlock:unlock()` | 释放锁 |

### 3.4 fs — 文件系统操作

| 属性 | 值 |
|------|-----|
| **require 名称** | `require("fs")` |
| **源文件** | `src/stdlib/lfs.c` |
| **加载条件** | 始终 |

#### 文件读写

| 函数 | 说明 |
|------|------|
| `fs.read(filename)` | 读取整个文件内容 |
| `fs.write(filename, data)` | 写入文件（覆盖） |
| `fs.append(filename, data)` | 追加写入文件 |

#### 目录操作

| 函数 | 说明 |
|------|------|
| `fs.listdir(path)` | 列出目录内容 |
| `fs.mkdir(path)` | 创建目录 |
| `fs.rmdir(path)` | 删除目录 |
| `fs.chdir(path)` | 切换当前工作目录 |
| `fs.getcwd()` | 获取当前工作目录 |

#### 文件信息

| 函数 | 说明 |
|------|------|
| `fs.exists(path)` | 检查路径是否存在 |
| `fs.isdir(path)` | 检查是否为目录 |
| `fs.isfile(path)` | 检查是否为文件 |
| `fs.size(path)` | 获取文件大小 |
| `fs.modified(path)` | 获取最后修改时间 |

#### 文件操作

| 函数 | 说明 |
|------|------|
| `fs.copyfile(src, dst)` | 复制文件 |
| `fs.renamefile(old, new)` | 重命名文件 |
| `fs.deletefile(path)` | 删除文件 |

### 3.5 logtable — 日志表

| 属性 | 值 |
|------|-----|
| **require 名称** | `require("logtable")` |
| **源文件** | `src/utils/logtable.c` |
| **加载条件** | 始终 |

提供格式化日志输出、日志级别管理、日志旋转等表格化日志功能。

---

## 4. VM 和字节码模块

直接操作虚拟机内部状态和字节码的底层模块。

### 4.1 vm — VM 内省

| 属性 | 值 |
|------|-----|
| **require 名称** | `require("vm")` |
| **源文件** | `src/vm/lvmlib.c` |
| **加载条件** | 始终 |

提供对 Lua VM 内部状态的访问和操作，支持字节码级别的 VM 内省和分析。

| 函数 | 说明 |
|------|------|
| `vm.info()` | 获取 VM 运行时信息（指令数、内存使用等） |
| `vm.getstack()` | 获取调用栈信息 |
| `vm.getregistry()` | 访问 VM 注册表 |

### 4.2 jit — 即时编译

| 属性 | 值 |
|------|-----|
| **require 名称** | `require("jit")` |
| **源文件** | `src/vm/jit/core/ljit.c` |
| **加载条件** | 非 `LUA_NOJIT` 编译 |

基于 sljit 的真实 JIT 编译，运行时将热点字节码编译为原生机器码。

| 函数 | 说明 |
|------|------|
| `jit.on()` | 启用 JIT 编译 |
| `jit.off()` | 禁用 JIT 编译 |
| `jit.status()` | 返回 JIT 是否启用（boolean） |
| `jit.compile(fn)` | 手动触发函数 JIT 编译 |
| `jit.stats()` | 获取 JIT 统计信息 |

### 4.3 ByteCode — 字节码操作和分析

| 属性 | 值 |
|------|-----|
| **require 名称** | `require("ByteCode")` |
| **源文件** | `src/vm/lbytecode.c` |
| **加载条件** | 始终 |

提供字节码级别的操作接口：指令读取/修改、字节码遍历、指令分析。

#### 函数/原型操作

| 函数 | 说明 |
|------|------|
| `ByteCode.CheckFunction(fn)` | 检查是否为函数 |
| `ByteCode.GetProto(fn)` | 获取 Proto 对象 |
| `ByteCode.Dump(fn)` | 将函数转储为字节码字符串 |
| `ByteCode.Load(bc)` | 从字节码字符串加载函数 |

#### 指令操作

| 函数 | 说明 |
|------|------|
| `ByteCode.GetCodeCount(proto)` | 获取指令数 |
| `ByteCode.GetCode(proto, index)` | 获取指定位置指令 |
| `ByteCode.SetCode(proto, index, code)` | 设置指定位置指令 |
| `ByteCode.GetInstruction(proto, pc)` | 获取指令 |
| `ByteCode.SetInstruction(proto, pc, instr)` | 设置指令 |
| `ByteCode.GetOpCode(instr)` | 获取指令操作码 |
| `ByteCode.GetArgs(instr)` | 获取指令参数 |
| `ByteCode.Make(...)` | 构造字节码指令 |

#### 常量/变量操作

| 函数 | 说明 |
|------|------|
| `ByteCode.GetConstant(proto, idx)` | 获取常量 |
| `ByteCode.GetConstants(proto)` | 获取所有常量 |
| `ByteCode.GetLine(proto, pc)` | 获取源码行号 |
| `ByteCode.GetParamCount(proto)` | 获取参数数量 |

#### 上值/局部变量

| 函数 | 说明 |
|------|------|
| `ByteCode.GetUpvalue(proto, idx)` | 获取上值信息 |
| `ByteCode.GetUpvalues(proto)` | 获取所有上值 |
| `ByteCode.GetLocal(proto, idx)` | 获取局部变量信息 |
| `ByteCode.GetLocals(proto)` | 获取所有局部变量 |

#### 嵌套函数

| 函数 | 说明 |
|------|------|
| `ByteCode.GetNestedProto(proto, idx)` | 获取嵌套函数原型 |
| `ByteCode.GetNestedProtos(proto)` | 获取所有嵌套函数 |

#### 安全

| 函数 | 说明 |
|------|------|
| `ByteCode.IsGC(proto)` | 判断是否可 GC |
| `ByteCode.Lock(proto)` | 锁定字节码 |
| `ByteCode.IsLocked(proto)` | 检查是否锁定 |
| `ByteCode.MarkOriginal(proto)` | 标记为原始 |
| `ByteCode.IsTampered(proto)` | 检查是否被篡改 |

### 4.4 vmprotect — 基于 VM 的代码保护

| 属性 | 值 |
|------|-----|
| **require 名称** | `require("vmprotect")` |
| **源文件** | `src/vm/lvmpro.c` |
| **加载条件** | 始终 |

基于 VM 的代码保护，使用自定义指令集，运行时字节码与标准格式不兼容，增强反逆向能力。

| 函数 | 说明 |
|------|------|
| `vmprotect.protect(fn)` | 对函数进行 VM 代码保护 |

### 4.5 vmcustom — 自定义操作码扩展系统

| 属性 | 值 |
|------|-----|
| **require 名称** | `require("vmcustom")` |
| **源文件** | `src/vm/lvmustom.c` |
| **加载条件** | 始终 |

允许用户注册自定义操作码处理函数，扩展 VM 指令集。

| 函数 | 说明 |
|------|------|
| `vmcustom.register(opcode, handler)` | 注册自定义操作码处理函数 |
| `vmcustom.unregister(opcode)` | 取消注册自定义操作码 |

### 4.6 nativevm — 原生 VM 接口

| 属性 | 值 |
|------|-----|
| **require 名称** | `require("nativevm")` |
| **源文件** | `src/vm/lnativevm.c` |
| **加载条件** | 始终 |

提供与原生 VM 执行相关的接口和功能。

### 4.7 nativeparser — 原生解析器接口

| 属性 | 值 |
|------|-----|
| **require 名称** | `require("nativeparser")` |
| **源文件** | `src/vm/lnativeparser.c` |
| **加载条件** | 始终 |

提供原生解析器功能和接口。

---

## 5. 编译和转换模块

代码编译、翻译和转换相关工具。

### 5.1 tcc — 字节码到 C 代码生成

| 属性 | 值 |
|------|-----|
| **require 名称** | `require("tcc")` |
| **源文件** | `src/compiler/lbctc.c` |
| **加载条件** | 始终 |

将 Lua 源码编译为字节码，再将字节码转换为 C 源代码。生成的 C 代码可通过外部 C 编译器编译为原生可执行程序或动态库。

> 注意：此模块名称为历史遗留，与 Tiny C Compiler 无关。

| 函数 | 说明 |
|------|------|
| `tcc.compile(code [, options])` | 将 Lua 源码编译为 C 源代码字符串 |
| `tcc.compute_flags(opts)` | 计算混淆标志位 |

**options 表支持字段：**

| 字段 | 类型 | 说明 |
|------|------|------|
| `use_pure_c` | bool | 使用纯 C 模式 |
| `obfuscate` | bool | 启用混淆 |
| `flatten` | bool | 控制流扁平化 |
| `string_encryption` | bool | 字符串加密 |
| `inline` | bool | 内联优化 |
| `flags` | int | 混淆标志组合 |
| `seed` | int | 混淆种子 |

### 5.2 translator — 代码翻译工具

| 属性 | 值 |
|------|-----|
| **require 名称** | `require("translator")` |
| **源文件** | `src/utils/ltranslator.c` |
| **加载条件** | 始终 |

提供 Lua 代码到其他语言（或格式）的翻译转换功能。

### 5.3 asyncio — 异步 I/O 和 Promise

| 属性 | 值 |
|------|-----|
| **require 名称** | `require("asyncio")` |
| **源文件** | `src/utils/laio.c` + `src/utils/leventloop.c` + `src/utils/lpromise.c` |
| **加载条件** | 始终 |

提供事件循环、Promise 等异步编程支持。

#### 事件循环

| 函数 | 说明 |
|------|------|
| `asyncio.run(fn)` | 启动事件循环 |
| `asyncio.sleep(seconds)` | 异步延迟 |
| `asyncio.spawn(fn)` | 创建异步任务 |
| `asyncio.timer(interval, callback)` | 创建定时器 |

#### Promise

| 函数 | 说明 |
|------|------|
| `asyncio.new(executor)` | 创建 Promise |
| `asyncio.resolve(value)` | 创建已解决的 Promise |
| `asyncio.reject(reason)` | 创建已拒绝的 Promise |
| `asyncio.all(promises)` | 等待所有 Promise 完成 |
| `asyncio.race(promises)` | 等待任意 Promise 完成 |

**Promise 对象方法：**

| 方法 | 说明 |
|------|------|
| `promise:then(onFulfilled [, onRejected])` | 注册回调 |
| `promise:catch(onRejected)` | 注册错误回调 |
| `promise:finally(onFinally)` | 注册最终回调 |

### 5.4 lexer — 词法分析和 AST 操作

| 属性 | 值 |
|------|-----|
| **require 名称** | `require("lexer")` |
| **源文件** | `src/compiler/llexerlib.c` |
| **加载条件** | 始终 |

对 Lua 代码进行词法分析和 AST 操作。

#### Token 操作

| 函数 | 说明 |
|------|------|
| `lexer.lex(code)` | 对 Lua 代码进行词法分析，返回 token 序列 |
| `lexer.token2str(token)` | Token 转字符串 |
| `lexer.find_match(tokens, pattern)` | 在 token 流中匹配模式 |
| `lexer.extract_tokens(tokens, from, to)` | 提取子 token 序列 |
| `lexer.replace_tokens(tokens, from, to, replacement)` | 替换 token 序列 |
| `lexer.split_sequence(tokens, delimiter)` | 按分隔符拆分 token 序列 |
| `lexer.insert_tokens(tokens, pos, new_tokens)` | 插入 token |
| `lexer.remove_tokens(tokens, from, to)` | 删除 token |
| `lexer.find_tokens(tokens, type)` | 查找特定类型的 token |
| `lexer.gmatch(tokens, pattern)` | 全局匹配 token |
| `lexer.reconstruct(tokens)` | 从 token 序列重建源码字符串 |

#### AST 操作

| 函数 | 说明 |
|------|------|
| `lexer.build_tree(tokens)` | 构建 AST 语法树 |
| `lexer.flatten_tree(tree)` | 展平 AST 树为 token 序列 |
| `lexer.split_statements(tokens)` | 拆分语句 |
| `lexer.parse_local(tokens)` | 解析局部变量声明 |
| `lexer.find_label(tokens, name)` | 查找标签 |
| `lexer.get_block_bounds(tokens)` | 获取块边界 |

#### 高级分析

| 函数 | 说明 |
|------|------|
| `lexer.build_cfg(tokens)` | 构建控制流图 |
| `lexer.analyze_liveness(tokens)` | 活性分析 |
| `lexer.mutate_expressions(tokens)` | 变异表达式 |
| `lexer.obfuscate(tokens)` | 混淆 token 序列 |
| `lexer.emit_vmp_instructions(tokens)` | 发射 VMP 指令 |

### 5.5 ast — AST 抽象语法树操作

| 属性 | 值 |
|------|-----|
| **require 名称** | `require("ast")` |
| **源文件** | `src/stdlib/lastlib.c` |
| **加载条件** | 始终 |

等同于 `lexer` 模块的 AST 操作部分，提供语法树解析、序列化、遍历功能。

| 函数 | 说明 |
|------|------|
| `ast.parse(code)` | 解析 Lua 代码为 AST |
| `ast.serialize(tree)` | 序列化 AST 为二进制格式 |
| `ast.deserialize(data)` | 从二进制格式反序列化 AST |
| `ast.walk(tree, visitor)` | 遍历 AST 树，调用访问者回调 |

---

## 6. 密码学模块

内置密码算法库，提供加密、哈希、签名等功能。

### 6.1 crypto — 密码算法库

| 属性 | 值 |
|------|-----|
| **require 名称** | `require("crypto")` |
| **源文件** | `src/utils/lcrypto.c` |
| **加载条件** | 始终 |

整合 SHA-256、AES、HMAC、CRC32、CSPRNG 等算法，采用子表结构组织。

#### crypto.sha256 — SHA-256 哈希

| 函数 | 说明 |
|------|------|
| `crypto.sha256(data)` | SHA-256 哈希，返回 hex 字符串 |
| `crypto.sha256.raw(data)` | SHA-256 哈希，返回原始字节 |

#### crypto.hmac — HMAC 消息认证

| 函数 | 说明 |
|------|------|
| `crypto.hmac.sha256(key, data)` | HMAC-SHA256，返回 hex 字符串 |
| `crypto.hmac.sha256_raw(key, data)` | HMAC-SHA256，返回原始字节 |

#### crypto.aes — AES 加密

| 函数 | 说明 |
|------|------|
| `crypto.aes.encrypt(key, data, mode, iv)` | AES 加密 |
| `crypto.aes.decrypt(key, data, mode, iv)` | AES 解密 |

支持的 mode: `"ECB"`, `"CBC"`, `"CTR"`。CBC/CTR 模式需要提供 16 字节 IV。

#### crypto.random — 安全随机数

| 函数 | 说明 |
|------|------|
| `crypto.random.bytes(n)` | 生成 n 字节原始随机数据 |
| `crypto.random.hex(n)` | 生成 n 字节随机 hex 字符串 |
| `crypto.random.int(min, max)` | 生成 `[min, max]` 随机整数 |
| `crypto.random.seed(n)` | 手动设置随机种子 |

#### crypto.crc32 — CRC32 校验

| 函数 | 说明 |
|------|------|
| `crypto.crc32(data)` | 计算 CRC32 校验码 |

### 6.2 uuid — UUID 生成

| 属性 | 值 |
|------|-----|
| **require 名称** | `require("uuid")` |
| **源文件** | `src/utils/luuid.c` |
| **加载条件** | 始终 |

| 函数 | 说明 |
|------|------|
| `uuid.v1()` | 生成基于时间戳的 UUID（版本 1） |
| `uuid.v3(namespace, name)` | 生成基于 MD5 命名空间的 UUID（版本 3） |
| `uuid.v4()` | 生成随机 UUID（版本 4） |
| `uuid.v5(namespace, name)` | 生成基于 SHA-1 命名空间的 UUID（版本 5） |
| `uuid.v7()` | 生成时间有序 UUID（版本 7） |
| `uuid.generate()` | 通用生成方法 |
| `uuid.parse(str)` | 解析 UUID 字符串为组件 |
| `uuid.is_valid(str)` | 检查字符串是否为有效 UUID |

### 6.3 rsa — RSA 非对称加密

| 属性 | 值 |
|------|-----|
| **require 名称** | `require("rsa")` |
| **源文件** | `src/utils/lrsa.c` |
| **加载条件** | 始终 |

#### rsa.key — 密钥管理

| 函数 | 说明 |
|------|------|
| `rsa.key.generate(bits)` | 生成 RSA 密钥对 |
| `rsa.key.from_components(modulus, exponent)` | 从组件创建密钥 |

#### rsa.public — 公钥操作

| 函数 | 说明 |
|------|------|
| `rsa.public.encrypt(key, data)` | 公钥加密 |
| `rsa.public.verify(key, data, signature)` | 公钥验签 |

#### rsa.private — 私钥操作

| 函数 | 说明 |
|------|------|
| `rsa.private.decrypt(key, data)` | 私钥解密 |
| `rsa.private.sign(key, data)` | 私钥签名 |

#### rsa.encode — 密钥编码

| 函数 | 说明 |
|------|------|
| `rsa.encode.public(key)` | 导出公钥为字符串 |
| `rsa.encode.private(key)` | 导出私钥为字符串 |

### 6.4 ecc — ECC 椭圆曲线加密

| 属性 | 值 |
|------|-----|
| **require 名称** | `require("ecc")` |
| **源文件** | `src/utils/lecc.c` |
| **加载条件** | 始终 |

#### 顶层函数

| 函数 | 说明 |
|------|------|
| `ecc.sign(private_key, data)` | ECDSA 签名 |
| `ecc.verify(public_key, data, signature)` | ECDSA 验签 |
| `ecc.ecdh(private_key, public_key)` | ECDH 密钥交换 |
| `ecc.recover(signature, data)` | 从签名恢复公钥 |

#### ecc.key — 密钥管理

| 函数 | 说明 |
|------|------|
| `ecc.key.generate()` | 生成 ECC 密钥对 |
| `ecc.key.from_private(key_data)` | 从私钥数据恢复密钥对 |

#### ecc.encode — 密钥编码

| 函数 | 说明 |
|------|------|
| `ecc.encode.public(key)` | 导出公钥 |
| `ecc.encode.private(key)` | 导出私钥 |

#### ecc.decode — 密钥解码

| 函数 | 说明 |
|------|------|
| `ecc.decode.public(data)` | 从字符串导入公钥 |
| `ecc.decode.private(data)` | 从字符串导入私钥 |

---

## 7. WASM 相关模块

WebAssembly 运行时和编译工具。

### 7.1 wasm3 — WebAssembly 运行时（wasm3）

| 属性 | 值 |
|------|-----|
| **require 名称** | `require("wasm3")` |
| **源文件** | `src/wasm/lwasm3.c` |
| **加载条件** | 始终 |

基于 wasm3 解释器的 WebAssembly 运行时，支持 WASM 模块加载、函数调用、内存读写、WASI 支持。

| 函数 | 说明 |
|------|------|
| `wasm3.parse(wasm_bytes)` | 解析 WASM 模块 |
| `wasm3.instantiate(module [, imports])` | 实例化模块 |
| `wasm3.load_file(filename)` | 从文件加载 WASM 模块 |

**实例对象方法：**

| 方法 | 说明 |
|------|------|
| `instance:get_function(name)` | 获取导出函数 |
| `instance:get_memory()` | 获取内存对象 |
| `instance:call(name, ...)` | 调用导出函数 |

### 7.2 wasmtime — WebAssembly 运行时（wasmtime）

| 属性 | 值 |
|------|-----|
| **require 名称** | `require("wasmtime")` |
| **源文件** | `src/wasm/lwasmtime.c` |
| **加载条件** | 非 Emscripten 平台 |

基于 wasmtime 高性能 JIT 运行时的 WebAssembly 引擎，支持 WASM GC 提案。

| 函数 | 说明 |
|------|------|
| `wasmtime.parse(wasm_bytes)` | 解析 WASM 模块 |
| `wasmtime.instantiate(module [, imports])` | 实例化模块 |
| `wasmtime.load_file(filename)` | 从文件加载 WASM 模块 |

### 7.3 lua2wasm — Lua 到 WASM 编译器

| 属性 | 值 |
|------|-----|
| **require 名称** | `require("lua2wasm")` |
| **源文件** | `src/lua2wasm/lua2wasmlib.c` |
| **加载条件** | 始终 |

将 Lua 源码编译为 WebAssembly 模块的完整编译器。

| 函数 | 说明 |
|------|------|
| `lua2wasm.compile(code [, options])` | 将 Lua 代码编译为 WASM 二进制 |

### 7.4 quickjs — QuickJS JavaScript 引擎

| 属性 | 值 |
|------|-----|
| **require 名称** | `require("quickjs")` |
| **源文件** | `src/bin/lquickjs.c` |
| **加载条件** | 始终 |

集成 QuickJS JavaScript 引擎，可在 Lua 中执行 JavaScript 代码，实现 Lua ↔ JS 双向互操作。

| 函数 | 说明 |
|------|------|
| `quickjs.eval(js_code)` | 执行 JavaScript 代码 |
| `quickjs.eval_file(filename)` | 执行 JavaScript 文件 |
| `quickjs.new_context()` | 创建新的 JS 执行上下文 |

---

## 8. 内部与平台相关模块

### 8.1 bigint — 大整数

| 属性 | 值 |
|------|-----|
| **类型** | 内置 VM 类型，由 `src/utils/lbigint.c` 提供运行时支持 |
| **加载条件** | 始终 |

任意精度整数运算，内置于 VM 核心。大整数是 Lua VM 的内置数据类型，支持完整的算术运算（加减乘除、取模、幂运算、位运算、比较等）。

### 8.2 test — 内部测试库

| 属性 | 值 |
|------|-----|
| **源文件** | `src/stdlib/ltests.c` |
| **加载条件** | 调试模式 |

提供内存分配调试、对象打印、各类型对象计数等内部测试和调试功能，用于回归测试和开发调试。

### 8.3 libc — C 标准库调用（Android）

| 属性 | 值 |
|------|-----|
| **require 名称** | `require("libc")` |
| **源文件** | `src/stdlib/libc/` |
| **加载条件** | 仅 Android 平台 |

允许从 Lua 直接调用 C 标准库函数。

---

## 附录：模块注册总表

以下为 `linit.c` 中注册的所有库及其加载条件：

| # | require 名称 | 类别 | 加载条件 | 说明 |
|---|-------------|------|---------|------|
| 1 | `_G`（base） | 标准库 | 始终 | 基础函数库 |
| 2 | `package` | 标准库 | 始终 | 模块管理 |
| 3 | `coroutine` | 标准库 | 始终 | 协程 |
| 4 | `debug` | 标准库 | 始终 | 调试 |
| 5 | `io` | 标准库 | 始终 | 文件 I/O |
| 6 | `math` | 标准库 | 始终 | 数学函数 |
| 7 | `patch` | 标准库 | 始终 | 热修复补丁 |
| 8 | `os` | 标准库 | 始终 | 操作系统 |
| 9 | `string` | 标准库 | 始终 | 字符串处理 |
| 10 | `table` | 标准库 | 始终 | 表操作 |
| 11 | `utf8` | 标准库 | 始终 | UTF-8 编码 |
| 12 | `bool` | 扩展模块 | 始终 | 布尔增强 |
| 13 | `userdata` | 扩展模块 | 始终 | 二进制数据序列化 |
| 14 | `vm` | VM 模块 | 始终 | VM 内省 |
| 15 | `bit` / `bit32` | 扩展模块 | 始终 | 位运算 |
| 16 | `ptr` | 扩展模块 | 始终 | 指针操作 |
| 17 | `struct` | 扩展模块 | 始终 | C 风格结构体 |
| 18 | `thread` | 系统集成 | 始终 | 多线程 |
| 19 | `http` | 系统集成 | 始终 | HTTP 网络 |
| 20 | `fs` | 系统集成 | 始终 | 文件系统操作 |
| 21 | `vmprotect` | VM 模块 | 始终 | VM 代码保护 |
| 22 | `tcc` | 编译转换 | 始终 | 字节码转 C |
| 23 | `ByteCode` | VM 模块 | 始终 | 字节码操作 |
| 24 | `wasm3` | WASM | 始终 | WASM 运行时（wasm3） |
| 25 | `wasmtime` | WASM | 非 Emscripten | WASM 运行时（wasmtime） |
| 26 | `lua2wasm` | WASM | 始终 | Lua 转 WASM |
| 27 | `lexer` | 编译转换 | 始终 | 词法分析 |
| 28 | `quickjs` | WASM | 始终 | JS 引擎集成 |
| 29 | `asyncio` | 编译转换 | 始终 | 异步 I/O |
| 30 | `jit` | VM 模块 | 非 `LUA_NOJIT` | JIT 编译 |
| 31 | `vmcustom` | VM 模块 | 始终 | 自定义操作码 |
| 32 | `nativevm` | VM 模块 | 始终 | 原生 VM 接口 |
| 33 | `nativeparser` | VM 模块 | 始终 | 原生解析器 |
| 34 | `translator` | 编译转换 | 始终 | 代码翻译 |
| 35 | `logtable` | 系统集成 | 始终 | 日志表 |
| 36 | `crypto` | 密码学 | 始终 | 密码算法 |
| 37 | `uuid` | 密码学 | 始终 | UUID 生成 |
| 38 | `rsa` | 密码学 | 始终 | RSA 加密 |
| 39 | `ecc` | 密码学 | 始终 | ECC 加密 |
| 40 | `map` | 扩展模块 | 始终 | Map 数据结构 |
| 41 | `ast` | 编译转换 | 始终 | AST 操作 |
| 42 | `process` | 系统集成 | 仅 Linux | 进程管理 |
| 43 | `libc` | 平台相关 | 仅 Android | C 标准库调用 |

**语法级内置特性（非 require 模块）：**

| 特性 | 运行时支持 | 说明 |
|------|-----------|------|
| `class` | `src/stdlib/lclass.c` | 面向对象类系统 |
| `superstruct` | `src/stdlib/lsuper.c` | 增强表定义 |
| `bigint` | `src/utils/lbigint.c` | 大整数运算 |

**类型系统扩展：**

| 类型 | 常量值 | 描述 |
|------|--------|------|
| `LUA_TSTRUCT` | 9 | C 风格结构体 |
| `LUA_TPOINTER` | 10 | 原始指针 |
| `LUA_TCONCEPT` | 11 | 类型谓词概念 |
| `LUA_TNAMESPACE` | 12 | 命名空间 |
| `LUA_TSUPERSTRUCT` | 13 | 增强表 |
| `LUA_TMAP` | 14 | 哈希 Map 容器 |

---

## 相关文档

- [LXCLUA-NCore 主文档](README_CN.md)
- [模块详细说明](MODULES.md)
- [API 参考手册](API_REFERENCE.md)
- [语法参考手册](SYNTAX_REFERENCE.md)
- [异步编程指南](NATIVE_ASYNC_AWAIT.md)
- [内联汇编教程](ASM_TUTORIAL_CN.md)