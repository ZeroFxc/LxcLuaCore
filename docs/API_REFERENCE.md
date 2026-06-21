# LXCLUA-NCore 标准库与扩展库 API 参考

本文档基于各库源文件的实际 `luaL_Reg` 注册数组，完整列出每个 `require("xxx")` 库的所有导出函数。

---

## 标准库

以下库与标准 Lua 5.4+ 行为基本一致：

| require 名 | 源文件 | 说明 |
|-----------|--------|------|
| `base` | lbaselib.c | 基础函数 (assert, error, ipairs, next, pairs, pcall, print, select, tonumber, tostring, type, xpcall等) |
| `package` | loadlib.c | 模块加载 (require, module, package.searchpath等) |
| `coroutine` | lcorolib.c | 协程 (create, resume, yield, status, wrap, running, close等) |
| `debug` | ldblib.c | 调试 (getinfo, getlocal, setlocal, getupvalue, setupvalue, traceback等) |
| `io` | liolib.c | 文件I/O (open, close, read, write, lines, tmpfile等) |
| `math` | lmathlib.c | 数学函数 (abs, sin, cos, sqrt, random, pi, huge等) |
| `os` | loslib.c | 操作系统 (clock, date, time, execute, exit, getenv等) |
| `string` | lstrlib.c | 字符串 (byte, char, find, format, gmatch, gsub, len, lower, match, rep, reverse, sub, upper等) |
| `table` | ltablib.c | 表操作 (concat, insert, remove, sort, pack, unpack等) |
| `utf8` | lutf8lib.c | UTF8支持 (char, codes, codepoint, len, offset等) |
| `patch` | lpatchlib.c | Lua补丁库 |

---

## 扩展库 API

### thread - 多线程库

**require**: `require("thread")`
**源文件**: `src/stdlib/lthreadlib.c`

#### 模块级函数

| 函数 | 说明 |
|------|------|
| `thread.create(fn)` | 创建线程，执行 `fn()` |
| `thread.createx(data, fn)` | 创建线程，传递 `data` 给 `fn` |
| `thread.channel([size])` | 创建通道(消息队列)，可选缓冲区大小 |
| `thread.pick(channel1, channel2, ...)` | 从多个通道中选择一个可接收的 |
| `thread.on(event, fn)` | 注册事件处理 |
| `thread.over(event, fn)` | 一次性事件处理 |
| `thread.self()` | 返回当前线程对象 |
| `thread.current()` | self 的别名 |

#### 线程对象方法

| 方法 | 说明 |
|------|------|
| `thread:join()` | 等待线程结束 |
| `thread:detach()` | 分离线程(join后不可调用) |
| `thread:name()` | 获取线程名称 |
| `thread:id()` | 获取线程ID |

#### 通道对象方法

| 方法 | 说明 |
|------|------|
| `channel:send(value)` / `channel:push(value)` | 发送数据到通道(阻塞) |
| `channel:receive()` / `channel:pop()` | 从通道接收数据(阻塞) |
| `channel:try_send(value)` | 尝试发送(非阻塞，返回 bool) |
| `channel:try_recv()` | 尝试接收(非阻塞，返回 value 或 nil) |
| `channel:peek()` | 查看队首元素但不移除 |
| `channel:recv_op()` | 接收操作符 |
| `channel:close()` | 关闭通道 |

```lua
local thread = require("thread")

-- 基本线程
local t = thread.create(function()
    print("Hello from thread!")
end)
t:join()

-- 通道通信
local ch = thread.channel(10)
local worker = thread.create(function()
    ch:send("done")
end)
local result = ch:receive()
worker:join()

-- 互斥锁
local m = thread.mutex()
m:lock()
-- 临界区
m:unlock()

-- 条件变量
local cond = thread.cond()
cond:wait(m)      -- 等待信号
cond:signal()     -- 唤醒一个
cond:broadcast()  -- 唤醒所有

-- 读写锁
local rw = thread.rwlock()
rw:rdlock()   -- 读锁
rw:wrlock()   -- 写锁
rw:unlock()
```

---

### http - HTTP 和网络库

**require**: `require("http")`
**源文件**: `src/utils/libhttp.c`

#### 模块级函数

| 函数 | 说明 |
|------|------|
| `http.request(method, url[, options])` | 发起 HTTP 请求 |
| `http.server()` | 创建 HTTP 服务器 |
| `http.websocket(url)` | 创建 WebSocket 连接 |

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
| `server:start(port[, host])` | 启动服务器 |
| `server:stop()` | 停止服务器 |

#### HTTP 响应对象方法

| 方法 | 说明 |
|------|------|
| `res:write_head(status[, headers])` | 写入状态码和响应头 |
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

```lua
local http = require("http")

-- HTTP 客户端
local res = http.request("GET", "https://httpbin.org/get")
print(res.status, res.body)

-- HTTP 服务器
local server = http.server()
server:route("GET", "/hello", function(req, res)
    res:json({message = "Hello, " .. (req.query.name or "World")})
end)
server:start(8080)
```

---

### crypto - 密码算法库

**require**: `require("crypto")`
**源文件**: `src/utils/lcrypto.c`

#### crypto.sha256 子表

| 函数 | 说明 |
|------|------|
| `crypto.sha256(data)` | SHA-256 哈希，返回 hex 字符串 |
| `crypto.sha256.raw(data)` | SHA-256 哈希，返回原始字节 |

直接调用 `crypto.sha256(data)` 可省略 `.sha256`。

#### crypto.hmac 子表

| 函数 | 说明 |
|------|------|
| `crypto.hmac.sha256(key, data)` | HMAC-SHA256，返回 hex 字符串 |
| `crypto.hmac.sha256_raw(key, data)` | HMAC-SHA256，返回原始字节 |

#### crypto.aes 子表

| 函数 | 说明 |
|------|------|
| `crypto.aes.encrypt(key, data, mode, iv)` | AES 加密 |
| `crypto.aes.decrypt(key, data, mode, iv)` | AES 解密 |

支持的 mode: `"ECB"`, `"CBC"`, `"CTR"`。CBC/CTR 模式需要提供 16 字节 IV。

#### crypto.random 子表

| 函数 | 说明 |
|------|------|
| `crypto.random.bytes(n)` | 生成 n 字节原始随机数据 |
| `crypto.random.hex(n)` | 生成 n 字节随机 hex 字符串 |
| `crypto.random.int(min, max)` | 生成 [min, max] 随机整数 |
| `crypto.random.seed(n)` | 手动设置随机种子 |

#### crypto.crc32

| 函数 | 说明 |
|------|------|
| `crypto.crc32(data)` | 计算 CRC32 校验码 |

---

### uuid - UUID 生成

**require**: `require("uuid")`
**源文件**: `src/utils/luuid.c`

| 函数 | 说明 |
|------|------|
| `uuid.v4()` | 生成随机 UUID (版本4) |
| `uuid.v5(namespace, name)` | 生成 SHA-1 命名空间 UUID (版本5) |
| `uuid.generate()` | 通用生成方法 |
| `uuid.parse(str)` | 解析 UUID 字符串 |
| `uuid.is_valid(str)` | 检查字符串是否为有效 UUID |

```lua
local uuid = require("uuid")
local id = uuid.v4()   -- "550e8400-e29b-41d4-a716-446655440000"
```

---

### rsa - RSA 非对称加密

**require**: `require("rsa")`
**源文件**: `src/utils/lrsa.c`

#### rsa.key 子表

| 函数 | 说明 |
|------|------|
| `rsa.key.generate(bits)` | 生成 RSA 密钥对 |
| `rsa.key.from_components(modulus, exponent)` | 从组件创建密钥 |

#### rsa.public 子表

| 函数 | 说明 |
|------|------|
| `rsa.public.encrypt(key, data)` | 公钥加密 |
| `rsa.public.verify(key, data, signature)` | 公钥验签 |

#### rsa.private 子表

| 函数 | 说明 |
|------|------|
| `rsa.private.decrypt(key, data)` | 私钥解密 |
| `rsa.private.sign(key, data)` | 私钥签名 |

#### rsa.encode 子表

| 函数 | 说明 |
|------|------|
| `rsa.encode.public(key)` | 导出公钥为字符串 |
| `rsa.encode.private(key)` | 导出私钥为字符串 |

---

### ecc - ECC 椭圆曲线加密

**require**: `require("ecc")`
**源文件**: `src/utils/lecc.c`

#### 顶层函数

| 函数 | 说明 |
|------|------|
| `ecc.sign(private_key, data)` | ECDSA 签名 |
| `ecc.verify(public_key, data, signature)` | ECDSA 验签 |
| `ecc.ecdh(private_key, public_key)` | ECDH 密钥交换 |
| `ecc.recover(signature, data)` | 从签名恢复公钥 |

#### ecc.key 子表

| 函数 | 说明 |
|------|------|
| `ecc.key.generate()` | 生成 ECC 密钥对 |
| `ecc.key.from_private(key_data)` | 从私钥数据恢复密钥对 |

#### ecc.encode 子表

| 函数 | 说明 |
|------|------|
| `ecc.encode.public(key)` | 导出公钥 |
| `ecc.encode.private(key)` | 导出私钥 |

#### ecc.decode 子表

| 函数 | 说明 |
|------|------|
| `ecc.decode.public(data)` | 从字符串导入公钥 |
| `ecc.decode.private(data)` | 从字符串导入私钥 |

---

### fs - 文件系统库

**require**: `require("fs")`
**源文件**: `src/stdlib/lfs.c`

```lua
local fs = require("fs")

-- 文件读写
local content = fs.read("file.txt")
fs.write("output.txt", "Hello World")
fs.append("log.txt", "New line\n")

-- 目录操作
fs.mkdir("/path/to/dir")
fs.rmdir("/path/to/dir")
local files = fs.listdir("/path/to/dir")

-- 文件信息
local exists = fs.exists("file.txt")
local is_dir = fs.isdir("path")
local is_file = fs.isfile("file.txt")
local size = fs.size("file.txt")

-- 文件操作
fs.copyfile("src", "dst")
fs.renamefile("old", "new")
fs.deletefile("file.txt")
```

---

### process - 进程管理

**require**: `require("process")` (仅 Linux)
**源文件**: `src/stdlib/lproclib.c`

```lua
local process = require("process")
local proc = process.execute("ls", "-la")
local output = proc:read("a")
```

---

### lexer - 词法分析与 AST 操作

**require**: `require("lexer")`
**源文件**: `src/compiler/llexerlib.c`

| 函数 | 说明 |
|------|------|
| `lexer.lex(code)` | 对 Lua 代码进行词法分析 |
| `lexer.token2str(token)` | Token 转字符串 |
| `lexer.find_match(tokens, pattern)` | 在 token 流中匹配模式 |
| `lexer.extract_tokens(tokens, from, to)` | 提取子 token 序列 |
| `lexer.replace_tokens(tokens, from, to, replacement)` | 替换 token 序列 |
| `lexer.split_sequence(tokens, delimiter)` | 按分隔符拆分 token 序列 |
| `lexer.build_tree(tokens)` | 构建 AST 语法树 |
| `lexer.flatten_tree(tree)` | 展平 AST 树为 token 序列 |
| `lexer.gmatch(tokens, pattern)` | 全局匹配 token |
| `lexer.reconstruct(tokens)` | 从 token 序列重建源码字符串 |
| `lexer.find_tokens(tokens, type)` | 查找特定类型的 token |
| `lexer.insert_tokens(tokens, pos, new_tokens)` | 插入 token |
| `lexer.remove_tokens(tokens, from, to)` | 删除 token |
| `lexer.split_statements(tokens)` | 拆分语句 |
| `lexer.parse_local(tokens)` | 解析局部变量声明 |
| `lexer.find_label(tokens, name)` | 查找标签 |
| `lexer.get_block_bounds(tokens)` | 获取块边界 |
| `lexer.obfuscate(tokens)` | 混淆 token 序列 |
| `lexer.build_cfg(tokens)` | 构建控制流图 |
| `lexer.analyze_liveness(tokens)` | 活性分析 |
| `lexer.mutate_expressions(tokens)` | 变异表达式 |
| `lexer.emit_vmp_instructions(tokens)` | 发射 VMP 指令 |

---

### ByteCode - 字节码操作

**require**: `require("ByteCode")`
**源文件**: `src/vm/lbytecode.c`

| 函数 | 说明 |
|------|------|
| `ByteCode.CheckFunction(fn)` | 检查是否为函数 |
| `ByteCode.GetProto(fn)` | 获取 Proto 对象 |
| `ByteCode.GetCodeCount(proto)` | 获取指令数 |
| `ByteCode.GetCode(proto, index)` | 获取指定位置指令 |
| `ByteCode.SetCode(proto, index, code)` | 设置指定位置指令 |
| `ByteCode.GetLine(proto, pc)` | 获取源码行号 |
| `ByteCode.GetParamCount(proto)` | 获取参数数量 |
| `ByteCode.IsGC(proto)` | 判断是否可 GC |
| `ByteCode.GetOpCode(instr)` | 获取指令操作码 |
| `ByteCode.GetArgs(instr)` | 获取指令参数 |
| `ByteCode.Make(...)` | 构造字节码 |
| `ByteCode.Dump(fn)` | 将函数转储为字节码字符串 |
| `ByteCode.GetConstant(proto, idx)` | 获取常量 |
| `ByteCode.GetConstants(proto)` | 获取所有常量 |
| `ByteCode.GetUpvalue(proto, idx)` | 获取上值信息 |
| `ByteCode.GetUpvalues(proto)` | 获取所有上值 |
| `ByteCode.GetLocal(proto, idx)` | 获取局部变量信息 |
| `ByteCode.GetLocals(proto)` | 获取所有局部变量 |
| `ByteCode.GetNestedProto(proto, idx)` | 获取嵌套函数原型 |
| `ByteCode.GetNestedProtos(proto)` | 获取所有嵌套函数 |
| `ByteCode.GetInstruction(proto, pc)` | 获取指令 |
| `ByteCode.SetInstruction(proto, pc, instr)` | 设置指令 |
| `ByteCode.Lock(proto)` | 锁定字节码 |
| `ByteCode.IsLocked(proto)` | 检查是否锁定 |
| `ByteCode.MarkOriginal(proto)` | 标记为原始 |
| `ByteCode.IsTampered(proto)` | 检查是否被篡改 |

---

### vm - VM 内省

**require**: `require("vm")`
**源文件**: `src/vm/lvmlib.c`

提供对 Lua VM 内部状态的访问和操作。

```lua
local vm = require("vm")

-- 获取 VM 信息
local info = vm.info()
print(info.instruction_count)
print(info.memory_usage)
```

---

### vmprotect - VM 代码保护

**require**: `require("vmprotect")`
**源文件**: `src/vm/lvmpro.c`

| 函数 | 说明 |
|------|------|
| `vmprotect.protect(fn)` | 对函数进行 VM 代码保护 |

---

### tcc - 字节码转 C 代码生成

**require**: `require("tcc")`
**源文件**: `src/compiler/lbctc.c`

| 函数 | 说明 |
|------|------|
| `tcc.compile(code[, options])` | 将 Lua 源码编译为 C 源代码字符串 |
| `tcc.compute_flags(opts)` | 计算混淆标志位 |

options 表支持字段:
- `use_pure_c` (bool): 使用纯 C 模式
- `obfuscate` (bool): 启用混淆
- `flatten` (bool): 控制流扁平化
- `string_encryption` (bool): 字符串加密
- `inline` (bool): 内联优化
- `flags` (int): 混淆标志组合
- `seed` (int): 混淆种子

---

### jit - 即时编译

**require**: `require("jit")` (非 `LUA_NOJIT` 编译)
**源文件**: `src/vm/jit/core/ljit.c`

基于 sljit 的真实 JIT 编译，运行时将热点字节码编译为原生机器码。

| 函数 | 说明 |
|------|------|
| `jit.on()` | 启用 JIT 编译 |
| `jit.off()` | 禁用 JIT 编译 |
| `jit.status()` | 返回 JIT 是否启用 (boolean) |

```lua
local jit = require("jit")
jit.on()          -- 启用 JIT
print(jit.status())  -- true
jit.off()         -- 禁用 JIT
```

---

### struct - C 风格结构体

**require**: `require("struct")`
**源文件**: `src/stdlib/lstruct.c`

```lua
local struct = require("struct")

-- 定义结构体
local Point = struct.define({
    {name = "x", type = "int"},
    {name = "y", type = "int"}
})

-- 创建实例
local p = Point.new(10, 20)
print(p.x, p.y)  -- 10, 20
```

---

### ptr - 指针操作

**require**: `require("ptr")`
**源文件**: `src/stdlib/lptrlib.c`

提供原始指针操作，用于底层内存管理。

```lua
local ptr = require("ptr")

local p = ptr.alloc(100)     -- 分配 100 字节
ptr.store(p, 0, 42)          -- 写入整数
local val = ptr.load(p, 0)   -- 读取整数
ptr.free(p)                   -- 释放内存
```

---

### bit / bit32 - 位运算

**require**: `require("bit")` 或 `require("bit32")`
**源文件**: `src/stdlib/lbitlib.c`

提供标准 Lua 5.2 bit32 库的全部函数: `band`, `bor`, `bxor`, `bnot`, `lshift`, `rshift`, `arshift`, `btest`, `extract`, `replace`, `lrotate`, `rrotate`。

---

### bool - 布尔增强

**require**: `require("bool")`
**源文件**: `src/stdlib/lboolib.c`

提供布尔类型的额外操作。

---

### userdata - 二进制数据序列化

**require**: `require("userdata")`
**源文件**: `src/stdlib/ludatalib.c`

提供 userdata 类型的元方法和工具函数。

---


### translator - 代码翻译

**require**: `require("translator")`
**源文件**: `src/stdlib/ltranslator.c`

提供代码翻译相关功能。

---

### bigint - 大整数

**类型内置于 VM 核心** (`src/utils/lbigint.c`)

大整数是 Lua VM 的内置类型，可通过数字字面量或 `require("bigint")` 使用:

```lua
local bi = require("bigint")

local a = bigint.new("12345678901234567890")
local b = bigint.new("98765432109876543210")
local sum = a + b
print(sum:tostring())  -- "111111111011111111100"
```

---

### wasm3 - WebAssembly 运行时

**require**: `require("wasm3")`
**源文件**: `src/wasm3/`

```lua
local wasm3 = require("wasm3")
local module = wasm3.parse(wasm_bytes)
local instance = wasm3.instantiate(module, {
    env = { print = print }
})
```

---

### wasmtime - WebAssembly 运行时 (原生平台)

**require**: `require("wasmtime")` (非 Emscripten)
**源文件**: `src/wasmtime/`

基于 wasmtime 的 WebAssembly 运行时。

---

### lua2wasm - Lua 转 WASM 编译器

**require**: `require("lua2wasm")`
**源文件**: `src/lua2wasm/`

将 Lua 代码编译为 WebAssembly 模块。

---

### quickjs - JavaScript 引擎集成

**require**: `require("quickjs")`
**源文件**: `src/quickjs/`

集成 QuickJS JavaScript 引擎，可在 Lua 中执行 JS 代码。

---

### asyncio - 异步 I/O

**require**: `require("asyncio")`
**源文件**: `src/asyncio/`

提供事件循环、Promise 等异步编程支持：

```lua
local async = require("asyncio")

-- Promise
local p = async.new(function(resolve, reject)
    resolve("done")
end)

p:then(function(value)
    print(value)
end):catch(function(err)
    print(err)
end)

-- 事件循环
async.run(function()
    -- 异步代码
end)
```

---

### logtable - 日志表

**require**: `require("logtable")`
**源文件**: `src/stdlib/logtable.c`

---

### libc - C 标准库调用 (Android)

**require**: `require("libc")` (仅 Android)
**源文件**: `src/stdlib/libc/`

允许从 Lua 直接调用 C 标准库函数。

---

### vmcustom - 自定义操作码扩展

**require**: `require("vmcustom")`
**源文件**: `src/vm/vmcustom/`

允许用户自定义 VM 操作码。

---

### nativevm - 原生 VM 接口

**require**: `require("nativevm")`
**源文件**: `src/vm/nativevm/`

---

### nativeparser - 原生解析器

**require**: `require("nativeparser")`
**源文件**: `src/parser/nativeparser/`

---

## 附录: 完整库注册表

以下为 `linit.c` 中注册的所有库及其加载条件:

| require 名称 | 条件 | 说明 |
|-------------|------|------|
| `base` | 始终 | 基础库 |
| `package` | 始终 | 包管理 |
| `coroutine` | 始终 | 协程 |
| `debug` | 始终 | 调试 |
| `io` | 始终 | 文件IO |
| `math` | 始终 | 数学 |
| `patch` | 始终 | 补丁库 |
| `os` | 始终 | 操作系统 |
| `string` | 始终 | 字符串 |
| `table` | 始终 | 表操作 |
| `utf8` | 始终 | UTF-8 |
| `bool` | 始终 | 布尔增强 |
| `userdata` | 始终 | userdata工具 |
| `vm` | 始终 | VM内省 |
| `bit` / `bit32` | 始终 | 位运算 |
| `ptr` | 始终 | 指针操作 |
| `struct` | 始终 | 结构体 |
| `thread` | 始终 | 多线程 |
| `http` | 始终 | HTTP网络 |
| `fs` | 始终 | 文件系统 |
| `vmprotect` | 始终 | VM保护 |
| `tcc` | 始终 | 字节码转C |
| `ByteCode` | 始终 | 字节码操作 |
| `wasm3` | 始终 | WASM运行时 |
| `wasmtime` | 非Emscripten | WASM运行时 |
| `lua2wasm` | 始终 | Lua转WASM |
| `lexer` | 始终 | 词法分析 |
| `quickjs` | 始终 | JS引擎 |
| `asyncio` | 始终 | 异步IO |
| `jit` | 非LUA_NOJIT | JIT编译 |
| `vmcustom` | 始终 | 自定义opcode |
| `nativevm` | 始终 | 原生VM |
| `nativeparser` | 始终 | 原生解析器 |
| `translator` | 始终 | 代码翻译 |
| `logtable` | 始终 | 日志表 |
| `crypto` | 始终 | 密码算法 |
| `uuid` | 始终 | UUID |
| `rsa` | 始终 | RSA |
| `ecc` | 始终 | ECC |
| `process` | Linux | 进程管理 |
| `libc` | Android | C标准库 |

---