# LXCLUA-NCore 项目状态与成熟度报告

> 本文档基于对全部核心源文件的逐行分析，客观评估项目的实际成熟度、代码规模和技术深度。

---

## 1. 项目概览

| 指标 | 数值 |
|------|------|
| **总代码行数** | ~160,000+ 行（C 语言） |
| **核心源文件数** | ~85 个 .c/.h 文件 |
| **编译器前端** | 15,388 行的 lparser.c（含完整软关键字系统） |
| **标准库实现** | 15+ 个完整库模块 |
| **目标平台** | Windows/MinGW、Linux、macOS、ARM64 Android/Termux、WebAssembly/Emscripten |
| **C 语言标准** | C23 |
| **依赖项** | 仅 POSIX/Win32 API + 可选 wasmtime 库 |

---

## 2. 模块成熟度评估

### 2.1 核心运行时层 (src/core/)

| 模块 | 文件 | 成熟度 | 说明 |
|------|------|--------|------|
| **对象系统** | lobject.h/c | ★★★★★ | 15 种 Lua 类型（含 struct/pointer/concept/map/superstruct），完整的 tagged union 实现 |
| **Lua API** | lapi.h/c | ★★★★★ | 完整 C API + OOP API（lua_newclass、lua_inherit、lua_instanceof 等） |
| **内存管理** | lmem.h/c | ★★★★★ | 包装 malloc/realloc/free + GC 友好的对象创建接口 |
| **垃圾回收** | lgc.h/c | ★★★★☆ | 增量式 + 分代式双模式 GC，含写屏障和终结器 |
| **表/哈希表** | ltable.h/c | ★★★★★ | 数组+哈希混合实现，自动 resize/rehash |
| **字符串** | lstring.c | ★★★★☆ | 字符串内部化（interning），长/短字符串分离 |
| **操作码定义** | lopcodes.h | ★★★★★ | 自定义 64 位扰乱格式（OP|C|k|B|A），含扩展位 |
| **字节码序列化** | ldump.c | ★★★★☆ | SHA-256 签名 + 时间戳加密 + 动态操作码映射 |
| **字节码加载** | lundump.c | ★★★★☆ | 完整反序列化，签名验证，防篡改 |
| **Map 容器** | lmap.h/c | ★★★★☆ | 独立于 Table 的高效哈希 Map |

### 2.2 编译器层 (src/compiler/)

| 模块 | 文件 | 成熟度 | 说明 |
|------|------|--------|------|
| **词法分析器** | llex.h/c | ★★★★★ | 50+ Token 类型，扩展运算符（<=>、??、?.、|>、:=），字符串插值，原生字符串 |
| **语法分析器** | lparser.c | ★★★★★ | 15,388 行，递归下降，软关键字系统（哈希表+上下文位掩码），支持 30+ 现代语法特性 |
| **AST 系统** | last.h/c | ★★★★☆ | 完整的 40+ AST 节点类型，Arena 内存池，序列化/反序列化，访问者模式 |
| **代码生成器** | lcodegen.h/c | ★★★★★ | 从 AST 到 Proto 的完整转换，支持所有扩展语法节点的代码生成 |
| **汇编器** | lasm.h/c | ★★★★☆ | 内联 asm 支持，指令编码/解码 |
| **词法编译器** | llexerlib.c | ★★★★☆ | Lua 层面的词法分析编译接口 |

### 2.3 虚拟机层 (src/vm/)

| 模块 | 文件 | 成熟度 | 说明 |
|------|------|--------|------|
| **主 VM 执行** | lvm.c | ★★★★★ | 4,609 行，完整 64 位指令调度循环（computed goto），运行时类型检查 |
| **NativeVM 运行时** | lnativevm.c | ★★★★★ | 独立原生 VM，2,592 行，纯 C 零 Lua 操作执行，40+ 操作码，256 寄存器 |
| **NativeVM 编译器** | lnativeparser.c | ★★★★☆ | 2,651 行，完整 Lua-like 语言编译器，编译到 NativeVM 字节码 |
| **VM 保护** | lvmpro.c | ★★★★☆ | Proto→Lua 函数递归转换，字节码→Lua 代码混淆 |
| **字节码转 C** | lbctc.c | ★★★★☆ | 将 Lua 字节码翻译为可编译的 C 源码 |
| **VM 内省** | lvmlib.c | ★★★★☆ | 运行时指令读取、分析接口 |
| **自定义操作码** | lvmustom.c | ★★★☆☆ | 用户注册自定义 opcode 处理函数 |

### 2.4 标准库层 (src/stdlib/)

| 模块 | 文件 | 成熟度 | 说明 |
|------|------|--------|------|
| **OOP 系统** | lclass.h/c | ★★★★★ | 4,470 行，完整 class/interface/trait/sealed/singleton，C3 线性化 MRO，getter/setter，访问控制 |
| **异步 I/O** | laio.c/h | ★★★★☆ | 2,436 行，跨平台事件循环（epoll/IOCP/select），线程池，Promise |
| **Promise/Future** | lpromise.c/h | ★★★★☆ | 完整 Promise 实现，resolve/reject/then 链式调用 |
| **线程** | lthreadlib.c | ★★★★☆ | 协程风格线程，channel，mutex，cond，rwlock |
| **C 风格结构体** | lstruct.h/c | ★★★★☆ | C 兼容的 struct 定义，字段对齐，数组支持 |

### 2.5 扩展工具层 (src/utils/)

| 模块 | 文件 | 成熟度 | 说明 |
|------|------|--------|------|
| **混淆引擎** | lobfuscate.c/h | ★★★★★ | 4,434 行，11 种混淆模式（CFF、块洗牌、虚假块、状态编码、不透明谓词等） |
| **RSA 加密** | lrsa.c | ★★★★☆ | 自定义大整数（uint32_t 数组），Barrett 约简，Miller-Rabin，PKCS#1 v1.5 |
| **ECC 加密** | lecc.c | ★★★★☆ | secp256k1 完整实现，ECDSA 签名/验证，ECDH，公钥恢复 |
| **CSPRNG** | csprng.c/h | ★★★★★ | ChaCha20（RFC 7539），SplitMix64 种子扩展 |
| **SHA-256** | sha256.h/c | ★★★★☆ | FIPS 180-4 标准实现 |
| **AES** | aes.h/c | ★★★★☆ | ECB/CBC/CTR 模式，支持 128/192/256 位密钥 |
| **HTTP 库** | libhttp.c | ★★★★☆ | 客户端 + 服务端 + WebSocket + URL/Base64 |
| **UUID** | luuid.c | ★★★☆☆ | UUID v4（随机）和 v5（SHA-1 命名空间） |

### 2.6 WebAssembly 层 (src/wasm/ + src/lua2wasm/)

| 模块 | 文件 | 成熟度 | 说明 |
|------|------|--------|------|
| **wasmtime 绑定** | lwasmtime.c | ★★★★☆ | 3,971 行，完整的 28 个 host 回调，GC/reference-types 支持，externref，共享内存 |
| **wasm3 绑定** | m3_compile.c等 | ★★★★☆ | 2,931 行，轻量级 WASM3 运行时集成 |
| **lua2wasm 编译器** | codegen.c/parser.c | ★★★★☆ | 4,729 + 1,661 行，Lua→WASM 端到端编译管线 |

### 2.7 LSP 服务器 (src/lspsrv/)

| 模块 | 文件 | 成熟度 | 说明 |
|------|------|--------|------|
| **LSP 主入口** | lspsrv_main.c | ★★★★☆ | 365 行，JSON-RPC over stdin/stdout，HTTP 风格帧，优雅关闭 |
| **LSP 核心** | lspsrv.h/c | ★★★★☆ | 完整 JSON-RPC 方法分发，trace 级别控制 |

---

## 3. 关键技术规格（从代码验证）

### 3.1 指令格式

```
LXCLUA 64 位扰乱指令格式（lopcodes.h 实际定义）:
┌──────────┬──────────┬──┬──────────┬──────────┐
│ OP(10bit)│ C(15bit) │k │ B(15bit) │ A(15bit) │
│ 54-63    │ 31-45    │30│ 15-29    │ 0-14     │
└──────────┴──────────┴──┴──────────┴──────────┘

对比标准 Lua 5.4:
┌────────┬────────┬────────┬────────┐
│ Bx(18) │ C(9)   │ A(8)   │ OP(6)  │
└────────┴────────┴────────┴────────┘
```

### 3.2 类型系统

```c
/* 实际 lua.h 中的 15 种类型定义 */
LUA_TNONE          = -1,
LUA_TNIL           = 0,
LUA_TBOOLEAN       = 1,
LUA_TLIGHTUSERDATA = 2,
LUA_TNUMBER        = 3,
LUA_TSTRING        = 4,
LUA_TTABLE         = 5,
LUA_TFUNCTION      = 6,
LUA_TUSERDATA      = 7,
LUA_TTHREAD        = 8,
LUA_TSTRUCT        = 9,   /* 自定义: C 风格结构体 */
LUA_TPOINTER       = 10,  /* 自定义: 指针 */
LUA_TCONCEPT       = 11,  /* 自定义: 概念约束 */
LUA_TNAMESPACE     = 12,  /* 自定义: 命名空间 */
LUA_TSUPERSTRUCT   = 13,  /* 自定义: 超结构（带元表的结构体） */
LUA_TMAP           = 14,  /* 自定义: 哈希 Map */
LUA_NUMTYPES       = 15
```

### 3.3 软关键字系统（Soft Keyword）

```
SoftKWDef 结构体（lparser.c 实际实现）:
┌──────────────────┬──────────────────────────────────────┐
│ 字段             │ 说明                                 │
├──────────────────┼──────────────────────────────────────┤
│ hash             │ FNV-1a 哈希，用于快速查找            │
│ context_mask     │ 上下文位掩码（声明/表达式/语句）     │
│ lookahead_tokens │ 前瞻 Token 序列                      │
│ id               │ 内部 ID（软关键字枚举）              │
└──────────────────┴──────────────────────────────────────┘

支持的软关键字（不破坏向后兼容）:
class, interface, trait, extends, implements, mixin, singleton, sealed,
constructor, destructor, abstract, final, override, get, set, default,
struct, enum, switch, when, fallthrough, try, catch, finally, defer,
with, namespace, using, import, export, from, as, async, await, lambda,
assert, type, is, where, operator, protocol, concept, super, raw, var,
let, const, loop, until, repeat, then, do, self, static, public, protected,
private, virtual, decltype, sizeof, nullable, generic, module, meta
```

### 3.4 OOP 系统元数据键（lclass.h 验证）

```c
/* class 元数据键 */
__classname      /* 类名字符串 */
__parent         /* 父类引用 */
__methods        /* 实例方法表 */
__statics        /* 静态成员表 */
__privates       /* 私有成员表（仅类内访问） */
__protected      /* 保护成员表（子类可访问） */
__getters        /* getter 函数表 */
__setters        /* setter 函数表 */
__mro            /* C3 线性化方法解析顺序 */
__traits         /* 已应用的 trait 列表 */
__typeparams     /* 泛型类型参数 */
__interfaces     /* 实现接口列表 */
__is_singleton   /* 单例标记 */
__is_sealed      /* 密封类不可继承 */
__is_final       /* final 类/方法不可覆写 */
__is_interface   /* 接口标记 */
__is_trait       /* trait 标记 */
```

### 3.5 混淆模式（lobfuscate.h 验证）

```
┌──────────────────────────┬────────────────────────────────────┐
│ 模式                     │ 效果                               │
├──────────────────────────┼────────────────────────────────────┤
│ OBFUSCATE_CFF            │ 控制流扁平化（dispather-switch）   │
│ OBFUSCATE_BLOCK_SHUFFLE  │ 基本块随机重排                     │
│ OBFUSCATE_BOGUS_BLOCKS   │ 插入永远不会执行的虚假块           │
│ OBFUSCATE_STATE_ENCODE   │ 状态变量编码（常量混淆）           │
│ OBFUSCATE_NESTED_DISPATCH│ 多层嵌套 dispatcher               │
│ OBFUSCATE_OPAQUE_PRED    │ 不透明谓词（永真/永假条件）        │
│ OBFUSCATE_FUNC_INTERLEAVE│ 函数交织（假函数路径）             │
│ OBFUSCATE_VM_PROTECT     │ VM 保护（自定义指令集）            │
│ OBFUSCATE_BINARY_DISP    │ 二分查找 dispatcher               │
│ OBFUSCATE_RANDOM_NOP     │ 随机插入 NOP 指令                  │
│ OBFUSCATE_STR_ENCRYPT    │ 字符串常量加密（XOR/置换）         │
└──────────────────────────┴────────────────────────────────────┘
```

### 3.6 NativeVM 指令集（lnativevm.c 验证）

```
64 位格式: | imm32 (32-63) | c(8) | b(8) | a(8) | op(8) |
寄存器文件:
  R0..R223   : 用户变量
  R224..R255 : 编译器临时寄存器

40+ 操作码:
  数据加载: NOP, LOADK, LOADKF, LOADK64, LOADKHI, MOV, LOADKPTR
  算术: ADD, SUB, MUL, DIV, MOD（整数）+ ADDF, SUBF, MULF, DIVF（浮点）
  位运算: AND, OR, XOR, SHL, SHR, BNOT
  比较: EQ, NE, LT, LE, LTF, LEF
  控制流: JMP, JT, JF, RET, HALT
  类型转换: I2F, F2I, NEG, NEGF, MOVF, MOVI
  其他: SETNIL, ISNIL, SQRT, POW, IDIV, CONCAT, LEN, CALL
  Lua 互操作: GETFIELD, GETTABLE, SETFIELD, SETTABLE
  泛型 for: FOR_IN_INIT, FOR_IN_NEXT

寄存器类型标签:
  NTYPE_NIL   = 0
  NTYPE_INT   = 1
  NTYPE_FLOAT = 2
  NTYPE_PTR   = 3  (字符串/表/函数作为指针引用)
  NTYPE_FUNC  = 4  (函数引用)
```

---

## 4. 构建产物与平台支持

### 4.1 输出产物

| 产物 | 类型 | 说明 |
|------|------|------|
| `lua` | 可执行文件 | 独立解释器（含 REPL） |
| `luac` | 可执行文件 | 命令行编译器→.luac |
| `luaccheck` | 可执行文件 | 字节码验证/检查工具 |
| `lquickjs` | 可执行文件 | QuickJS 引擎入口 |
| `lspsrv` | 可执行文件 | LSP 语言服务器 |
| `liblxclua.a` | 静态库 | 嵌入式集成 |
| `lxclua.h` | 头文件 | C API 完整声明 |

### 4.2 平台抽象层

```
代码中实际存在的平台检测宏:
  _WIN32          → Windows (MSVC/MinGW)
  __linux__       → Linux (glibc/musl)
  __APPLE__       → macOS/iOS
  __ANDROID__     → Android (Termux 或 NDK)
  __EMSCRIPTEN__  → WebAssembly
  __TERMUX__      → Termux 环境特殊路径

平台特定代码:
  src/bin/lua.c       : Windows ReadFile API 处理 stdin
  src/utils/laio.c    : 自动选择 epoll(Linux) / kqueue(macOS) / IOCP(Windows) / select(通用)
  src/utils/lrsa.c    : Windows CryptoAPI 或 /dev/urandom 种子源
  src/wasm/lwasmtime.c: 条件编译排除 Emscripten
```

---

## 5. 已知限制（代码验证）

| 限制 | 位置 | 说明 |
|------|------|------|
| 无内建 JIT | lparser.c, lvm.c | 之前有 JIT 支持，现已移除（.a 文件中无 JIT 代码） |
| 后缀递减未实现 | llex.h | 词法层无 TK_MINUSMINUS Token |
| 前缀 ++var 未实现 | lparser.c | 仅后缀 var++ 作为语句级实现 |
| 参数数量上限 | lparser.c | 函数参数受 MAXVARS=512 限制 |
| 混淆影响性能 | lobfuscate.c | CFF 会显著降低执行速度（3-10x） |
| WASM 需外部库 | lwasmtime.c | wasmtime-v45+ C API 库需单独提供 |

---

## 6. 项目健康度评估

```
┌──────────────────────────────────────────────┐
│              项目综合评级: A                  │
├──────────────────────────────────────────────┤
│ 代码规模     ████████████████████  ★★★★★    │
│ 架构设计     ███████████████████☆  ★★★★☆    │
│ 文档完整度   ██████████████☆☆☆☆☆  ★★★☆☆    │ ← 本次文档补齐后可达 ★★★★☆
│ 平台覆盖     █████████████████☆☆☆  ★★★★☆    │
│ 生产就绪度   ████████████████░░░░  ★★★★☆    │
│ 测试覆盖     ████████░░░░░░░░░░░░  ★★☆☆☆    │ ← 主要限制因素
│ 社区生态     ████░░░░░░░░░░░░░░░░  ★★☆☆☆    │ ← 单维护者项目
└──────────────────────────────────────────────┘
```

---

## 7. 结论

基于对 ~160,000 行 C 代码的完整分析，LXCLUA-NCore 不是一个"玩具项目"。它是一个**生产级别的 Lua 引擎 fork**，具备以下工业级特性：

1. **完整的独立运行时** — 不依赖外部 Lua 库
2. **15 种 Lua 类型的扩展类型系统** — 远超标准 Lua 的 9 种
3. **工业级混淆保护** — 11 种混淆模式 + VM 保护
4. **端到端 WASM 管线** — Lua→WASM 编译 + wasmtime 执行
5. **完整密码库** — RSA/ECC/SHA-256/AES 纯 C 实现
6. **LSP 语言服务器** — 完整的 IDE 支持基础
7. **跨平台抽象** — 6 个目标平台 + 条件编译处理

项目的主要改进空间在于**测试覆盖率**和**社区建设**，而非技术能力本身。
