# LXCLUA-NCore 架构文档

## 1. 项目概述

**LXCLUA-NCore** 是基于 Lua 5.5 定制开发的高性能嵌入式脚本引擎，由 DifierLine 开发和维护。它在 Lua 5.5 的基础上进行了深度扩展，主要增强方向包括：

- **安全编译**：动态操作码映射、时间戳加密、SHA-256 完整性校验
- **代码混淆**：控制流扁平化、基本块洗牌、虚假块、VM 保护和字符串加密
- **语法扩展**：类、接口、Switch、Try-Catch、箭头函数、管道操作符、空值合并、可选链等现代语言特性
- **JIT 编译**：基于 SLJIT 的跨平台即时编译，支持 x86/x64、ARM32/64、MIPS、RISC-V、LoongArch 等架构
- **字节码转 C (tcc)**：将 Lua 字节码转换为 C 源代码，便于嵌入 C 项目或外部编译优化
- **扩展类型系统**：新增 struct、pointer、namespace、superstruct、map 等类型
- **多语言互操作**：集成 QuickJS JavaScript 引擎、wasm3/wasmtime WebAssembly 运行时
- **LSP 服务器**：内置语言服务器协议实现，提供 IDE 级智能支持
- **Lua→WASM 编译器**：将 Lua 源码编译为 WebAssembly 模块

项目采用 C 语言 (C23) 编写，支持跨平台编译（Linux、Windows/mingw、macOS、Android/Termux、WebAssembly/Emscripten）。

---

## 2. 整体架构

LXCLUA-NCore 采用 **五层分层架构**，从下到上依次为：核心层、VM 运行时层、编译器层、扩展层、应用层。各层之间通过明确的 API 接口进行通信。

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│                              应用层 (Application Layer)                          │
│  ┌──────────┐  ┌──────────┐  ┌───────────┐  ┌──────────┐  ┌──────────┐         │
│  │ lxclua   │  │  luac    │  │ luaccheck │  │ lbcdump  │  │ lquickjs │         │
│  │ (解释器) │  │ (编译器) │  │(字节码检查)│  │(反汇编器)│  │(JS集成)  │         │
│  └──────────┘  └──────────┘  └───────────┘  └──────────┘  └──────────┘         │
│                          src/bin/                                                │
├─────────────────────────────────────────────────────────────────────────────────┤
│                              扩展子系统 (Extension Subsystems)                    │
│  ┌──────────────┐  ┌──────────────────────┐  ┌──────────────────────┐            │
│  │ LSP 服务器   │  │ WASM 运行时           │  │ lua2wasm 编译器      │            │
│  │ src/lspsrv/  │  │ src/wasm/ (wasm3 +    │  │ src/lua2wasm/        │            │
│  │              │  │   wasmtime)           │  │ (Lua→WASM 编译管线)  │            │
│  └──────────────┘  └──────────────────────┘  └──────────────────────┘            │
├─────────────────────────────────────────────────────────────────────────────────┤
│                              扩展层 (Extension Layer)                            │
│  ┌────────────────────────────┐  ┌──────────────────────────────────────┐        │
│  │    标准库 (stdlib/)        │  │       工具库 (utils/)                 │        │
│  │  base, math, string,       │  │  crypto, http, thread, fs, process,  │        │
│  │  table, io, os, coroutine, │  │  struct, ptr, ecc, rsa, uuid, bigint,│        │
│  │  debug, package, utf8,     │  │  namespace, translator, obfuscate,   │        │
│  │  bit, bit32, map, class    │  │  asyncio, promise, eventloop, json   │        │
│  └────────────────────────────┘  └──────────────────────────────────────┘        │
├─────────────────────────────────────────────────────────────────────────────────┤
│                              编译器层 (Compiler Layer)                            │
│  ┌──────────┐ ┌──────────┐ ┌───────────┐ ┌───────────┐ ┌──────────┐            │
│  │  llex    │ │ lparser  │ │last_parse │ │last_serial│ │last_visit│            │
│  │(词法分析)│ │(语法分析)│ │(AST解析)  │ │(AST序列化)│ │(AST访问) │            │
│  └──────────┘ └──────────┘ └───────────┘ └───────────┘ └──────────┘            │
│  ┌──────────┐ ┌──────────┐ ┌───────────┐                                          │
│  │ lcodegen │ │  lasm    │ │  lbctc    │                                          │
│  │(代码生成)│ │(汇编器)  │ │(字节码→C) │                                          │
│  └──────────┘ └──────────┘ └───────────┘                                          │
│                          src/compiler/                                            │
├─────────────────────────────────────────────────────────────────────────────────┤
│                            VM 运行时层 (VM Runtime Layer)                         │
│  ┌──────────┐ ┌──────────┐ ┌──────────────────────────────┐ ┌──────────┐        │
│  │   lvm    │ │  lgc     │ │       JIT 编译器 (jit/)       │ │ ldebug   │        │
│  │(VM解释器)│ │(垃圾回收)│ │  frontend│ir│optimize│codegen│ │(调试支持)│        │
│  │          │ │          │ │          │ │regalloc│sljit   │ │          │        │
│  └──────────┘ └──────────┘ └──────────────────────────────┘ └──────────┘        │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐                           │
│  │ ltable   │ │ lstring  │ │  lfunc   │ │ lstate   │                           │
│  │(表操作)  │ │(字符串)  │ │(函数)    │ │(状态管理)│                           │
│  └──────────┘ └──────────┘ └──────────┘ └──────────┘                           │
│                          src/vm/ + src/core/ (部分)                               │
├─────────────────────────────────────────────────────────────────────────────────┤
│                              核心层 (Core Layer)                                  │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐ │
│  │   lapi   │ │ lauxlib  │ │ lobject  │ │lopcodes  │ │ lundump  │ │  ldump   │ │
│  │(C API)   │ │(辅助库)  │ │(对象系统)│ │(操作码)  │ │(字节码加载)│ │(字节码  │ │
│  │          │ │          │ │          │ │          │ │          │ │ 序列化) │ │
│  └──────────┘ └──────────┘ └──────────┘ └──────────┘ └──────────┘ └──────────┘ │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐              │
│  │  lfunc   │ │   ltm    │ │   lzio   │ │   lmap   │ │  lmem    │              │
│  │(函数原型)│ │(元方法)  │ │(缓冲IO)  │ │(Map容器) │ │(内存管理)│              │
│  └──────────┘ └──────────┘ └──────────┘ └──────────┘ └──────────┘              │
│                          src/core/                                                │
└─────────────────────────────────────────────────────────────────────────────────┘
```

### 层级依赖关系

```
应用层 ──→ 扩展层 ──→ 编译器层 ──→ VM 运行时层 ──→ 核心层
  │           │           │              │              │
  └───────────┴───────────┴──────────────┴──────────────┘
                        全部依赖核心层
```

- **核心层**：最底层，不依赖任何其他层，提供所有上层所需的公共基础
- **VM 运行时层**：依赖核心层，实现字节码执行和运行时的核心功能
- **编译器层**：依赖核心层和 VM 运行时层，将源码编译为可执行字节码
- **扩展层**：依赖核心层和 VM 运行时层，提供 Lua 标准库和扩展工具库
- **应用层**：依赖所有下层，提供最终用户可执行程序

---

## 3. 各层详细说明

### 3.1 核心层 (`src/core/`)

核心层是 LXCLUA-NCore 的基石，提供所有上层模块依赖的基础设施。

| 模块 | 文件 | 功能描述 |
|------|------|----------|
| **lapi** | `lapi.c`, `lapi.h` | Lua C API 实现，包括状态操作、栈操作、类型转换、函数调用、垃圾回收控制、OOP API（类、继承、属性、命名空间）、tcc 支持函数 |
| **lauxlib** | `lauxlib.c`, `lauxlib.h` | 辅助库，提供缓冲区管理、类型检查、错误处理等便捷函数 |
| **lobject** | `lobject.c`, `lobject.h` | 对象系统，定义 Lua 所有值类型（TValue）的底层表示和操作 |
| **lopcodes** | `lopcodes.c`, `lopcodes.h` | 操作码定义，定义 VM 指令集的操作码和操作数格式（64位指令） |
| **lundump** | `lundump.c`, `lundump.h` | 字节码加载器，反序列化预编译的 Lua 字节码 |
| **ldump** | `ldump.c` | 字节码序列化器，将函数原型序列化为二进制字节码 |
| **lfunc** | `lfunc.c`, `lfunc.h` | 函数原型管理，闭包创建、上值操作 |
| **ltm** | `ltm.c`, `ltm.h` | 元方法（metamethod）系统，管理类型的元表操作 |
| **lzio** | `lzio.c`, `lzio.h` | 缓冲输入/输出，提供带缓冲的字符流读取 |
| **lmap** | `lmap.c`, `lmap.h` | Map 容器类型（LUA_TMAP），基于哈希表的高效键值对存储 |
| **lmem** | `lmem.c`, `lmem.h` | 内存管理，包装内存分配/释放操作 |
| **lua.h** | `lua.h` | 主 API 头文件，定义所有公开类型、常量和函数声明 |
| **luaconf.h** | `luaconf.h` | 配置头文件，数值类型、路径、平台适配等编译时常量 |
| **llimits.h** | `llimits.h` | 内部限制定义，最大栈深度等 |
| **lprefix.h** | `lprefix.h` | 编译前缀，统一包含平台头文件 |
| **lcode** | `lcode.c`, `lcode.h` | 中间代码生成器，将 AST 节点转换为 VM 指令 |

### 3.2 VM 运行时层 (`src/vm/` + `src/core/` 部分模块)

VM 运行时层负责字节码的执行和运行时环境管理。

#### 3.2.1 VM 核心

| 模块 | 文件 | 功能描述 |
|------|------|----------|
| **lvm** | `src/vm/lvm.c`, `src/vm/lvm.h` | VM 解释器主循环，执行字节码指令，64 位指令格式，支持 XCLUA 指令集 |
| **lgc** | `src/core/lgc.c`, `src/core/lgc.h` | 垃圾回收器（GC），支持增量式和分代式两种模式 |
| **ldebug** | `src/core/ldebug.c`, `src/core/ldebug.h` | 调试支持，钩子函数、调用栈信息、局部变量/上值访问 |
| **ltable** | `src/core/ltable.c`, `src/core/ltable.h` | 表（table）操作，哈希表实现、数组部分管理 |
| **lstring** | `src/core/lstring.c`, `src/core/lstring.h` | 字符串管理，字符串内部化、哈希、比较 |
| **lstate** | `src/core/lstate.c`, `src/core/lstate.h` | Lua 状态机，全局状态管理、线程创建与销毁 |
| **ldo** | `src/core/ldo.c`, `src/core/ldo.h` | 函数调用与协程调度，保护模式调用、错误恢复 |

#### 3.2.2 JIT 编译器 (`src/vm/jit/`)

基于 SLJIT 的跨平台即时编译子系统，将热点字节码直接编译为原生机器码。

```
src/vm/jit/
├── core/                    # JIT 核心
│   ├── ljit.c/h             # JIT 编译器主入口，编译触发、缓存管理
│   ├── ljit_internal.h      # JIT 内部定义
│   └── ljit_debug.h         # JIT 调试支持
├── frontend/                # 前端：字节码 → IR 转换
│   ├── ljit_analyze.c/h     # 热点分析，字节码分析，确定编译范围
│   └── ljit_translate.c     # 字节码翻译器，将字节码转换为 IR
├── ir/                      # 中间表示 (IR)
│   ├── ljit_ir.c/h          # IR 指令定义与操作
│   ├── ljit_ir_list.c       # IR 指令链表管理
│   ├── ljit_ir_label.c      # IR 标签管理
│   └── ljit_ir_bb.c         # 基本块 (Basic Block) 管理
├── optimize/                # 优化器
│   ├── ljit_opt.c/h         # 优化器主调度
│   ├── ljit_opt_const.c     # 常量折叠与传播
│   ├── ljit_opt_dce.c       # 死代码消除 (Dead Code Elimination)
│   ├── ljit_opt_peep.c      # 窥孔优化 (Peephole Optimization)
│   ├── ljit_opt_cse.c       # 公共子表达式消除 (CSE)
│   └── ljit_opt_inline.c    # 函数内联 (Inlining)
├── regalloc/                # 寄存器分配
│   ├── ljit_regalloc.c/h    # 寄存器分配器主调度
│   ├── ljit_reg_live.c      # 活跃变量分析
│   ├── ljit_reg_graph.c     # 干涉图构建
│   ├── ljit_reg_color.c     # 图着色算法
│   ├── ljit_reg_spill.c     # 溢出处理
│   └── ljit_reg_alloc.c     # 寄存器分配结果
├── codegen/                 # 代码生成
│   ├── ljit_codegen.c/h     # 代码生成主调度
│   ├── ljit_cg_arith.c      # 算术运算代码生成
│   ├── ljit_cg_ctrl.c       # 控制流代码生成
│   ├── ljit_cg_table.c      # 表操作代码生成
│   ├── ljit_cg_conv.c       # 类型转换代码生成
│   ├── ljit_cg_closure.c    # 闭包操作代码生成
│   └── ljit_cg_oop.c        # OOP 操作代码生成
└── sljit/                   # SLJIT 后端适配层
    ├── ljit_sljit.c/h       # SLJIT 绑定层
    └── ljit_sljit_mac.h     # SLJIT 宏定义
```

**JIT 编译流水线**：

```
字节码 ──→ [前端分析] ──→ IR ──→ [优化器] ──→ [寄存器分配] ──→ [代码生成] ──→ 原生码
              │                │         │              │               │
         ljit_analyze     ljit_ir   ljit_opt_*   ljit_regalloc   ljit_codegen
         ljit_translate             (5种优化)     (图着色)        ljit_cg_*
```

#### 3.2.3 VM 工具模块

| 模块 | 文件 | 功能描述 |
|------|------|----------|
| **lbytecode** | `src/vm/lbytecode.c` | 字节码操作和分析库 (ByteCode 模块) |
| **lvmlib** | `src/vm/lvmlib.c` | VM 内省库 (vm 模块)，提供字节码级别的 VM 内省 |
| **lvmpro** | `src/vm/lvmpro.c` | VM 保护库 (vmprotect 模块)，基于 VM 的代码保护 |
| **lvmustom** | `src/vm/lvmustom.c` | 自定义操作码扩展系统 (vmcustom 模块) |
| **lnativevm** | `src/vm/lnativevm.c` | 原生 VM 接口 (nativevm 模块) |
| **lnativeparser** | `src/vm/lnativeparser.c` | 原生解析器接口 (nativeparser 模块) |
| **ljumptab** | `src/vm/ljumptab.h` | VM 指令跳转表（computed goto 优化） |

### 3.3 编译器层 (`src/compiler/`)

编译器层负责将 LXCLUA 源码编译为可执行的字节码。

#### 编译流水线

```
源码 (.lua)
    │
    ▼
┌──────────┐    词法分析：将字符流分解为 Token 序列
│  llex    │    支持扩展运算符（<=>、??、?.、|>、:= 等）
└────┬─────┘    支持字符串插值、原生字符串、Shell 测试表达式
     │ Token 流
     ▼
┌──────────┐    语法分析：将 Token 序列解析为 AST
│ lparser  │    支持 class、interface、struct、enum、switch、try-catch
└────┬─────┘    支持箭头函数、Lambda、C 风格函数、泛型、async/await
     │         支持列表/字典推导式、解构赋值、预处理器指令、内联汇编
     │ AST
     ▼
┌───────────┐   AST 解析器：将 Lua 源码解析为结构化 AST 树
│last_parse │   提供可编程的 AST 操作接口
└─────┬─────┘
      │
      ▼
┌──────────┐    代码生成：将 AST 转换为中间代码（VM 指令序列）
│ lcodegen │    处理操作符优先级、寄存器分配、跳转标签
└────┬─────┘
     │
     ▼
┌──────────┐    汇编器：将中间代码汇编为最终字节码
│  lasm    │    支持内联汇编（asm）语法
└────┬─────┘
     │
     ▼
  字节码 (.luac)
```

#### 编译器模块详解

| 模块 | 文件 | 功能描述 |
|------|------|----------|
| **llex** | `llex.c`, `llex.h` | 词法分析器，将源码字符流分解为 Token 流，支持扩展运算符（`<=>`, `??`, `?.`, `|>`, `:=` 等）、字符串插值 `${}`、原生字符串 `_raw""`、Shell 测试 `[ ]` |
| **lparser** | `lparser.c`, `lparser.h` | 语法分析器，递归下降解析，支持 class/interface/struct/enum、switch/when、try-catch-finally、defer/with、namespace/using、箭头函数/Lambda/C 风格函数、泛型、async/await、列表/字典推导式、解构赋值、预处理器指令、内联 asm 等 |
| **last_parse** | `last_parse.c`, `last_parse.h` | AST 解析器入口，将 Lua 源码解析为结构化 AST 树 |
| **last_serialize** | `last_serialize.c`, `last_serialize.h` | AST 序列化器，将 AST 树序列化为可存储/传输的格式 |
| **last_visitor** | `last_visitor.c`, `last_visitor.h` | AST 访问者模式，提供遍历和操作 AST 的接口 |
| **last** | `last.c`, `last.h` | AST 通用定义和工具函数 |
| **lcodegen** | `lcodegen.c`, `lcodegen.h` | 代码生成器，将 AST 转换为 VM 中间代码，处理操作符优先级、寄存器分配、跳转标签 |
| **lasm** | `lasm.c`, `lasm.h` | 汇编器，将中间代码汇编为最终字节码，支持内联汇编语法 |
| **lbctc** | `lbctc.c`, `lbctc.h` | 字节码到 C 代码转换器 (tcc)，将 Lua 字节码转换为 C 源代码 |
| **llexer_compiler** | `llexer_compiler.c`, `llexer_compiler.h` | 词法分析器编译器后端，供 lexer 库使用 |
| **llexerlib** | `llexerlib.c` | 词法分析库（lexer 模块），提供 Lua 层面的词法分析接口 |

### 3.4 扩展层 (`src/stdlib/` + `src/utils/`)

#### 3.4.1 标准库 (`src/stdlib/`)

| 模块 | 文件 | 对应 Lua 库 | 功能描述 |
|------|------|------------|----------|
| **lbaselib** | `lbaselib.c` | `base` | 基础函数库（print, type, error, pcall, assert 等） |
| **lmathlib** | `lmathlib.c` | `math` | 数学库（三角函数、对数、随机数等） |
| **lstrlib** | `lstrlib.c` | `string` | 字符串库（模式匹配、格式化、PCRE2 正则） |
| **ltablib** | `ltablib.c` | `table` | 表操作库（insert, remove, sort, concat 等） |
| **liolib** | `liolib.c` | `io` | I/O 库（文件读写、标准输入输出） |
| **loslib** | `loslib.c` | `os` | 操作系统库（时间、日期、系统命令） |
| **lcorolib** | `lcorolib.c` | `coroutine` | 协程库（create, resume, yield, wrap 等） |
| **ldblib** | `ldblib.c` | `debug` | 调试库（栈跟踪、变量访问、钩子管理） |
| **loadlib** | `loadlib.c` | `package` | 模块加载库（require, module, package.path） |
| **lutf8lib** | `lutf8lib.c` | `utf8` | UTF-8 编码支持库 |
| **lbitlib** | `lbitlib.c` | `bit`/`bit32` | 位运算库（AND, OR, XOR, 移位等） |
| **lboolib** | `lboolib.c` | `bool` | 布尔增强库 |
| **lmaplib** | `lmaplib.c` | `map` | Map 容器库 |
| **lclass** | `lclass.c`, `lclass.h` | `class` | 类系统支持库 |
| **lstruct** | `lstruct.c`, `lstruct.h` | — | C 风格结构体支持 |
| **lsuper** | `lsuper.c`, `lsuper.h` | — | SuperStruct 增强表支持 |
| **lptrlib** | `lptrlib.c` | `ptr` | 指针操作库 |
| **ludatalib** | `ludatalib.c` | `userdata` | 二进制数据序列化库 |
| **lthreadlib** | `lthreadlib.c` | `thread` | 多线程库（互斥锁、条件变量、读写锁） |
| **lfs** | `lfs.c` | `fs` | 文件系统操作库 |
| **lproclib** | `lproclib.c` | `process` | 进程管理库 |
| **lastlib** | `lastlib.c` | `lexer` | AST 操作库入口 |
| **ljit_stubs** | `ljit_stubs.c` | — | JIT 桩模块（WASM 等无 JIT 平台使用） |
| **linit** | `src/core/linit.c` | — | 库初始化注册表 |

#### 3.4.2 工具库 (`src/utils/`)

| 模块 | 文件 | 对应 Lua 库 | 功能描述 |
|------|------|------------|----------|
| **crypto** | `lcrypto.c`, `sha256.c/h`, `aes.c/h`, `crc.c/h`, `csprng.c/h` | `crypto` | 密码算法库（SHA-256, AES, HMAC, CRC32, CSPRNG） |
| **uuid** | `luuid.c` | `uuid` | UUID 生成（v4 随机, v7 时间有序） |
| **rsa** | `lrsa.c` | `rsa` | RSA 非对称加密 |
| **ecc** | `lecc.c` | `ecc` | ECC 椭圆曲线加密 |
| **http** | `libhttp.c` | `http` | HTTP 客户端/服务端和 Socket 库 |
| **thread** | `lthread.c`, `lthread.h` | — | 线程基础设施（lthreadlib 的底层支持） |
| **asyncio** | `laio.c`, `laio.h` | `asyncio` | 异步 I/O 库 |
| **promise** | `lpromise.c`, `lpromise.h` | — | Promise 支持 |
| **eventloop** | `leventloop.c`, `leventloop.h` | — | 事件循环 |
| **namespace** | `lnamespace.c`, `lnamespace.h` | — | 命名空间运行时支持 |
| **bigint** | `lbigint.c`, `lbigint.h` | `bigint` | 大整数运算库 |
| **translator** | `ltranslator.c`, `ltranslator.h` | `translator` | 代码翻译工具 |
| **obfuscate** | `lobfuscate.c`, `lobfuscate.h` | — | 代码混淆核心（CFF, 块洗牌, 虚假块, 字符串加密） |
| **json** | `json_parser.c`, `json_parser.h` | — | JSON 解析器 |
| **lctype** | `lctype.c`, `lctype.h` | — | 字符类型分类 |
| **logtable** | `logtable.c` | `logtable` | 日志表支持 |
| **lpatchlib** | `lpatchlib.c` | — | 热修复补丁库 |

### 3.5 应用层 (`src/bin/`)

| 程序 | 源文件 | 功能描述 |
|------|------|----------|
| **lxclua** | `lua.c` | LXCLUA 解释器，支持交互式 REPL 和脚本执行 |
| **luac** | `luac.c` | Lua 字节码编译器，将 .lua 源码编译为 .luac 字节码 |
| **luaccheck** | `luaccheck.c` | 字节码检查器，验证字节码文件完整性、解密混淆字节码 |
| **lbcdump** | `lbcdump.c` | 字节码反汇编器，将字节码文件转换为可读的指令列表 |
| **lquickjs** | `lquickjs.c` | QuickJS JavaScript 引擎集成，使 Lua 可调用 JS 代码 |

---

## 4. 数据流

### 4.1 源码到执行

```
                    编译器层                         VM 运行时层
┌──────────┐    ┌──────────────────────────────┐    ┌──────────────────────┐
│  .lua    │───→│ llex ──→ lparser ──→ lcodegen│───→│  lvm 解释执行        │
│  源码    │    │  │                    │       │    │  (字节码解释器)      │
└──────────┘    │  │                    ▼       │    │         │            │
                │  │            ┌──────────┐    │    │    ┌────▼─────┐      │
                │  │            │last_parse│    │    │    │ ljit     │      │
                │  │            │ (AST)    │    │    │    │ (JIT编译)│      │
                │  │            └──────────┘    │    │    └──────────┘      │
                │  └────────────────────────────┘    └──────────────────────┘
                │         ▲                                 │
                │         │ llexer_compiler                 │
                │    ┌────┴──────┐                   ┌──────▼──────┐
                │    │ lastlib   │                   │  lgc        │
                │    │(lexer模块)│                   │ (垃圾回收)  │
                │    └───────────┘                   └─────────────┘
                └──────────────────────────────────────────────────┘
```

### 4.2 字节码持久化

```
源码 ──→ [编译] ──→ 字节码(.luac) ──→ [lundump] ──→ [lvm] ──→ 执行结果
                       │                                    │
                       │                                    │
                  ┌────▼─────┐                        ┌─────▼──────┐
                  │  ldump   │                        │  ljit      │
                  │(序列化)  │                        │(JIT编译)   │
                  └──────────┘                        └────────────┘
                       │
                  ┌────▼─────┐
                  │lobfuscate│ (可选混淆)
                  └──────────┘
```

### 4.3 字节码转 C (tcc)

```
源码 ──→ [编译] ──→ 字节码 ──→ [lbctc] ──→ C 源代码 ──→ [GCC/Clang] ──→ 原生可执行程序
```

### 4.4 Lua → WASM 编译管线

```
源码 ──→ [lexer_l2w] ──→ [parser_l2w] ──→ AST ──→ [codegen_l2w] ──→ WAT ──→ [wat2wasm] ──→ WASM
```

### 4.5 JIT 编译管线

```
字节码 ──→ [ljit_analyze] ──→ [ljit_translate] ──→ IR ──→ [ljit_opt_*] ──→ [ljit_regalloc] ──→ [ljit_codegen] ──→ 原生码
             (热点检测)          (字节码→IR)         (5种优化)    (图着色寄存器分配)   (SLJIT 后端)
```

---

## 5. 第三方依赖

### 5.1 PCRE2 正则表达式引擎 (`pcre2/`)

- **版本**：PCRE2 10.x
- **用途**：提供 Perl 兼容的正则表达式支持，增强 Lua 字符串库的 `string.match`/`string.gmatch` 等函数
- **集成方式**：作为独立子目录静态编译进 `liblxclua.a`
- **JIT 支持**：PCRE2 自身包含 SLJIT 加速，在无 JIT 平台（如 WASM）使用 `pcre2_jit_stubs.c` 替代
- **包含文件**：约 30+ 个源文件，涵盖编译、匹配、JIT、序列化、Unicode 支持等

### 5.2 QuickJS JavaScript 引擎 (`quickjs/`)

- **版本**：2024-01-13
- **用途**：在 Lua 环境中嵌入 JavaScript 解释器，通过 `require("quickjs")` 调用
- **集成方式**：作为独立子目录编译，链接到主程序
- **核心文件**：`quickjs.c`, `quickjs.h`, `quickjs-libc.c`, `libregexp.c`, `libunicode.c`, `cutils.c`, `dtoa.c`
- **Lua 绑定**：`src/bin/lquickjs.c` 提供 QuickJS 的 Lua C 模块封装

### 5.3 SLJIT 跨平台 JIT 后端 (`src/jit/`)

- **用途**：提供跨平台的即时编译后端，支持将 IR 编译为原生机器码
- **支持架构**：x86-32, x86-64, ARM-32 (ARM/T2), ARM-64, MIPS-32, MIPS-64, PPC-32, PPC-64, RISC-V-32, RISC-V-64, LoongArch-64, S390X
- **核心文件**：`sljitLir.c/h`（LIR 层）、`sljitNative*.c`（各架构原生代码生成）、`allocator_src/`（可执行内存分配器）
- **Lua 绑定**：
  - `src/jit/sljitLir.c` 编译进 `CORE_O`
  - `src/vm/jit/sljit/ljit_sljit.c` 提供 Lua JIT 到 SLJIT 的适配层

### 5.4 wasm3 WebAssembly 解释器 (`src/wasm/`)

- **用途**：在 Lua 环境中运行 WebAssembly 模块，通过 `require("wasm3")` 调用
- **核心文件**：`m3_*.c/h`（约 15 个文件），涵盖解析、编译、执行、环境管理、WASI 支持
- **Lua 绑定**：`src/wasm/lwasm3.c`

### 5.5 wasmtime WebAssembly 运行时

- **版本**：v45.0.1
- **用途**：支持 WASM GC 提案的 WebAssembly 运行时，通过 `require("wasmtime")` 调用
- **集成方式**：预编译的 C API 库，平台特定（Windows/MinGW, Linux, Android）
- **Lua 绑定**：`src/wasm/lwasmtime.c`

---

## 6. 扩展子系统

### 6.1 LSP 服务器 (`src/lspsrv/`)

LXCLUA-NCore 内置了完整的语言服务器协议 (LSP) 实现，提供 IDE 级智能支持。

| 模块 | 文件 | 功能描述 |
|------|------|----------|
| **主入口** | `lspsrv_main.c` | LSP 服务器主循环，JSON-RPC 通信 |
| **协议处理** | `lspsrv_proto.c` | LSP 协议消息解析与路由 |
| **JSON 编解码** | `lspsrv_json.c` | JSON-RPC 消息的编码与解码 |
| **文档管理** | `lspsrv_doc.c` | 文档同步、增量更新、诊断 |
| **词法分析** | `lspsrv_lexer.c` | 实时词法分析，提供语义高亮 Token |
| **关键字数据库** | `lspsrv_kwdb.c` | 关键字和内置函数信息库 |
| **代码补全** | `lspsrv_complete.c` | 智能代码补全 |
| **悬停提示** | `lspsrv_hover.c` | 符号悬停信息 |
| **高级特性** | `lspsrv_features.c` | 跳转定义、查找引用、重命名等 |
| **工具函数** | `lspsrv_util.c` | 通用工具函数 |
| **公共头文件** | `lspsrv.h` | 所有模块共享的类型定义和接口 |

**构建产物**：`lxclua-lsp` / `lxclua-lsp.exe`，独立可执行程序，不依赖 wasmtime 运行时。

### 6.2 WASM 运行时 (`src/wasm/`)

提供两种 WebAssembly 运行时集成：

| 运行时 | 绑定文件 | 调用方式 | 特点 |
|--------|----------|----------|------|
| **wasm3** | `lwasm3.c` | `require("wasm3")` | 纯解释器，轻量级，跨平台，WASI 支持 |
| **wasmtime** | `lwasmtime.c` | `require("wasmtime")` | 高性能 JIT 运行时，支持 WASM GC 提案 |

**WASM 支持基础设施**：
- `lxclua_wasm.c`：LXCLUA 的 WASM 导出接口封装
- `m3_*.c/h`：wasm3 核心引擎（约 15 个模块）

### 6.3 lua2wasm 编译器 (`src/lua2wasm/`)

将 Lua 源码编译为 WebAssembly 模块的完整编译器管线。

| 模块 | 文件 | 功能描述 |
|------|------|----------|
| **词法分析** | `lexer.c`, `lexer.h` | Lua 子集词法分析器 |
| **语法分析** | `parser.c`, `parser.h` | 递归下降解析器，生成 AST |
| **AST 定义** | `ast.c`, `ast.h` | AST 节点类型定义和操作 |
| **代码生成** | `codegen.c`, `codegen.h` | AST → WAT 文本格式代码生成 |
| **WAT 构建器** | `wat_builder.c`, `wat_builder.h` | WAT 文本格式的构建和输出 |
| **内置函数** | `builtins.c`, `builtins.h` | WASM 内置函数（内存管理、字符串等） |
| **WAT→WASM** | `wat2wasm.c`, `wat2wasm.h` | WAT 文本格式到 WASM 二进制的汇编器 |
| **Lua 模块** | `lua2wasmlib.c` | `require("lua2wasm")` 的 Lua C 模块入口 |
| **CLI 主程序** | `main.c` | 独立 `lua2wasm` 命令行工具 |
| **WAT2WASM CLI** | `wat2wasm_cli.c` | 独立 `wat2wasm` 命令行工具 |
| **内存分配** | `xalloc.c`, `xalloc.h` | 跨平台内存分配包装 |
| **WASM 预置** | `prelude.wat`, `prelude_wat.h` | WASM 模块预置代码 |

---

## 7. 构建系统

### 7.1 Makefile 结构

`Makefile` 是项目的核心构建系统，支持多种平台：

| 目标 | 命令 | 描述 |
|------|------|------|
| 默认 | `make` | 自动检测平台 |
| Linux | `make linux` | GCC + glibc，动态链接 |
| Windows | `make mingw` | MinGW-w64，生成 .exe 和 .dll |
| Windows 静态 | `make mingw-static` | MinGW，纯静态链接 |
| Android | `make termux` | Termux 环境，Clang + C23 |
| macOS | `make macosx` | Darwin 平台 |
| WebAssembly | `make wasm` | Emscripten，生成 .js + .wasm |
| WASM LSP | `make wasmlsp` | LSP 服务器的 WASM 构建 |
| WASM C 库 | `make wasm-c` | 将 C 文件编译为 WASM 模块 |
| LSP 服务器 | `make lsp` | 仅编译 LSP 服务器 |

### 7.2 构建产物

| 产物 | 描述 |
|------|------|
| `liblxclua.a` | Lua 静态库（含所有核心、扩展、JIT、WASM 运行时） |
| `lxclua` / `lxclua.exe` | LXCLUA 解释器 |
| `luac` / `luac.exe` | Lua 字节码编译器 |
| `luaccheck` / `luaccheck.exe` | 字节码检查器 |
| `lxclua.dll` | Windows 动态链接库 |
| `lxclua-lsp` / `lxclua-lsp.exe` | LSP 语言服务器 |
| `lxclua.js` / `lxclua.wasm` | WebAssembly 构建产物 |
| `lua2wasm` / `lua2wasm.exe` | Lua→WASM 编译器 CLI |
| `wat2wasm` / `wat2wasm.exe` | WAT→WASM 汇编器 CLI |

### 7.3 链接关系

```
liblxclua.a
├── CORE_O (核心 + VM + JIT + 编译器)
│   ├── 核心层: lapi, lcode, ldebug, ldo, ldump, lfunc, lgc, lmap, lmem, lobject,
│   │           lopcodes, lstate, lstring, ltable, ltm, lundump, lzio
│   ├── 编译器: llex, lparser, lasm, last, last_parse, last_visitor, last_serialize,
│   │           lcodegen, lobfuscate
│   ├── 扩展类型: lthread, lstruct, lnamespace, lbigint, lsuper
│   ├── VM 工具: lvmustom
│   └── JIT: sljitLir, ljit, ljit_ir, ljit_ir_list, ljit_ir_label, ljit_ir_bb,
│            ljit_sljit, ljit_codegen, ljit_cg_*, ljit_regalloc, ljit_reg_*,
│            ljit_opt, ljit_opt_*, ljit_analyze, ljit_translate
├── LIB_O (标准库 + 工具库)
│   ├── 标准库: lauxlib, lbaselib, lcorolib, ldblib, liolib, lmathlib, loadlib,
│   │           loslib, lstrlib, ltablib, lutf8lib, lmaplib, linit
│   ├── 扩展库: lboolib, lbitlib, lptrlib, ludatalib, lvmlib, lvmustom, lnativevm,
│   │           lnativeparser, lclass, ltranslator, llexerlib, llexer_compiler
│   ├── 工具库: sha256, aes, crc, csprng, lcrypto, luuid, lrsa, lecc, lthreadlib,
│   │           libhttp, lfs, lproclib, logtable, json_parser, lpatchlib
│   ├── VM 相关: lvmlib, lvmpro, lbytecode, lbctc
│   └── 异步: leventloop, lpromise, laio
├── LIB_O_WASM (WASM 运行时)
│   ├── lwasm3, lwasmtime
│   └── wasm3 核心: m3_*.o (15 个文件)
├── QJS_O (QuickJS)
│   └── quickjs, libregexp, libunicode, cutils, quickjs-libc, dtoa
├── LUA2WASM_O (Lua→WASM 编译器)
│   ├── 核心: ast, lexer_l2w, parser_l2w, wat_builder, codegen_l2w, builtins_l2w, xalloc_l2w
│   ├── 汇编: wat2wasm_core
│   └── 模块: lua2wasmlib
└── PCRE2_O (PCRE2 正则引擎)
    └── pcre2_*.o (约 30 个文件)
```

---

## 8. 类型系统扩展

LXCLUA-NCore 在 Lua 原有 9 种类型基础上新增了 5 种类型：

| 类型常量 | 值 | 类型名 | 描述 |
|----------|---|--------|------|
| `LUA_TNIL` | 0 | nil | 空值 |
| `LUA_TBOOLEAN` | 1 | boolean | 布尔值 |
| `LUA_TLIGHTUSERDATA` | 2 | lightuserdata | 轻量用户数据 |
| `LUA_TNUMBER` | 3 | number | 数值（默认 double） |
| `LUA_TSTRING` | 4 | string | 字符串 |
| `LUA_TTABLE` | 5 | table | 表 |
| `LUA_TFUNCTION` | 6 | function | 函数 |
| `LUA_TUSERDATA` | 7 | userdata | 完整用户数据 |
| `LUA_TTHREAD` | 8 | thread | 协程 |
| **`LUA_TSTRUCT`** | **9** | **struct** | C 风格结构体 |
| **`LUA_TPOINTER`** | **10** | **pointer** | 原始指针类型 |
| **`LUA_TCONCEPT`** | **11** | **concept** | 类型谓词概念 |
| **`LUA_TNAMESPACE`** | **12** | **namespace** | 命名空间类型 |
| **`LUA_TSUPERSTRUCT`** | **13** | **superstruct** | 增强表定义 |
| **`LUA_TMAP`** | **14** | **map** | 哈希 Map 容器 |

---

## 9. 代码混淆系统

LXCLUA-NCore 提供多层代码混淆保护，在 `lobfuscate.c` 中实现：

| 混淆标志 | 位掩码 | 描述 |
|----------|--------|------|
| `OBFUSCATE_CFF` | `1<<0` | 控制流扁平化 (Control Flow Flattening) |
| `OBFUSCATE_BLOCK_SHUFFLE` | `1<<1` | 基本块随机化洗牌 |
| `OBFUSCATE_BOGUS_BLOCKS` | `1<<2` | 插入虚假基本块 |
| `OBFUSCATE_STATE_ENCODE` | `1<<3` | 状态变量编码混淆 |
| `OBFUSCATE_NESTED_DISPATCHER` | — | 多层调度器嵌套 |
| `OBFUSCATE_OPAQUE_PREDICATES` | — | 不透明谓词插入 |
| `OBFUSCATE_FUNC_INTERLEAVE` | — | 函数交错合并 |
| `OBFUSCATE_VM_PROTECT` | — | VM 保护（自定义指令集） |
| `OBFUSCATE_BINARY_DISPATCHER` | — | 二分查找调度器 |
| `OBFUSCATE_RANDOM_NOP` | — | 随机 NOP 指令插入 |
| `OBFUSCATE_STR_ENCRYPT` | — | 字符串常量加密 |

通过 `lua_dump_obfuscated()` API 在字节码序列化时应用混淆。

---

## 10. 安全特性

| 特性 | 实现位置 | 描述 |
|------|----------|------|
| 动态操作码映射 | `lvm.c` | 每次编译使用不同的操作码到指令的映射 |
| 时间戳加密 | `lundump.c` | 字节码嵌入加密时间戳，防止重放 |
| SHA-256 完整性校验 | `sha256.c` | 字节码签名验证，防止篡改 |
| 字节码签名 | `LUA_SIGNATURE` | 自定义魔数 `\x1bXCF` 替代标准 `\x1bLua` |
| 字符串加密 | `lobfuscate.c` | 编译时加密字符串常量，运行时解密 |
| VM 保护 | `lvmpro.c` | 自定义指令集，运行时字节码与标准格式不兼容 |

---

## 11. 平台支持

| 平台 | 编译器 | 架构 | 备注 |
|------|--------|------|------|
| Linux | GCC (gnu11) | x86_64 | 支持 JIT、wasmtime |
| Windows | MinGW-w64 (gnu11) | x86_64 | 支持 JIT、wasmtime、DLL 导出 |
| macOS | Clang/GCC | x86_64/ARM64 | 支持 readline |
| Android (Termux) | Clang (c23) | aarch64 | 支持 wasmtime |
| WebAssembly | Emscripten (c23) | wasm32 | 无 JIT（使用 ljit_stubs），无 wasmtime |
| iOS | Clang | ARM64 | POSIX 兼容 |

---

## 12. 目录结构总览

```
lua/
├── src/
│   ├── core/           # 核心层：API、对象、操作码、内存、GC、表、字符串等
│   ├── vm/             # VM 运行时层：解释器、JIT 编译器、字节码工具
│   │   └── jit/        # JIT 子系统：前端、IR、优化器、寄存器分配、代码生成
│   ├── compiler/       # 编译器层：词法分析、语法分析、AST、代码生成、汇编
│   ├── stdlib/         # 标准库：base、math、string、table、io、os 等
│   ├── utils/          # 工具库：crypto、http、thread、fs、process 等
│   ├── bin/            # 应用层：lxclua、luac、luaccheck、lbcdump、lquickjs
│   ├── jit/            # SLJIT 后端：跨平台 JIT 原生代码生成
│   ├── wasm/           # WASM 运行时：wasm3 解释器、wasmtime 绑定
│   ├── lua2wasm/       # Lua→WASM 编译器
│   └── lspsrv/         # LSP 语言服务器
├── pcre2/              # PCRE2 正则表达式引擎（第三方）
├── quickjs/            # QuickJS JavaScript 引擎（第三方）
├── docs/               # 文档
├── Makefile            # 构建系统
├── lua.hpp             # C++ 头文件
├── Android.mk          # Android NDK 构建
└── LICENSE             # MIT 许可证
```