# LXCLUA-NCore 模块详细说明

> 基于 Lua 5.5 定制开发的高性能嵌入式脚本引擎，由 DifierLine 开发和维护。

---

## 目录

- [1. src/core/ — 核心运行时](#1-srccore--核心运行时)
- [2. src/compiler/ — 编译器](#2-srccompiler--编译器)
- [3. src/vm/ — 虚拟机](#3-srcvm--虚拟机)
- [4. src/vm/jit/ — JIT 编译器](#4-srcvmjit--jit-编译器)
- [5. src/stdlib/ — 标准库](#5-srcstdlib--标准库)
- [6. src/utils/ — 工具库](#6-srcutils--工具库)
- [7. src/lspsrv/ — LSP 服务器](#7-srclspsrv--lsp-服务器)
- [8. src/lua2wasm/ — Lua 到 WASM 编译器](#8-srclua2wasm--lua-到-wasm-编译器)
- [9. src/wasm/ — WASM 运行时](#9-srcwasm--wasm-运行时)
- [10. src/bin/ — 应用程序](#10-srcbin--应用程序)

---

## 1. src/core/ — 核心运行时

**目录**：`src/core/`  
**文件数**：41 个  
**层级**：最底层，不依赖任何其他模块，为所有上层提供公共基础设施。

### 文件列表

| 文件 | 类型 | 功能描述 |
|------|------|----------|
| `lua.h` | 头文件 | 主 API 头文件，定义所有公开类型（`lua_State`、`lua_Number`、`lua_Integer`、`lua_CFunction` 等）、常量（类型常量 `LUA_T*`、状态码 `LUA_OK`/`LUA_ERR*`、操作码 `LUA_OP*`）、函数声明（栈操作、调用、协程、GC、调试、OOP API、tcc 支持、混淆导出）和常用宏（`lua_pop`、`lua_pushcfunction`、`lua_isfunction` 等）。 |
| `luaconf.h` | 头文件 | 编译配置头文件，定义数值类型（`LUA_INTEGER`/`LUA_NUMBER` 的底层类型选择）、平台适配（`LUA_USE_WINDOWS`/`LUA_USE_POSIX`/`LUA_USE_LINUX`/`LUA_USE_MACOSX`）、路径配置（`LUA_PATH_DEFAULT`/`LUA_CPATH_DEFAULT`）、DLL 导出标记（`LUA_API`/`LUALIB_API`/`LUAMOD_API`）、内部可见性（`LUAI_FUNC`）、兼容性开关（`LUA_COMPAT_5_3`/`LUA_COMPAT_UNPACK`）、堆栈限制（`LUAI_MAXSTACK`）、扩展空间大小（`LUA_EXTRASPACE`）等。 |
| `lapi.h` | 头文件 | 内部 Lua API 辅助宏，定义栈溢出检查（`api_incr_top`）、结果调整（`adjustresults`）、栈元素数量检查（`api_checknelems`/`api_checkpop`）、错误断言（`api_check`）等。 |
| `lapi.c` | 源文件 | Lua C API 实现，包括状态创建/销毁（`lua_newstate`/`lua_close`）、栈操作（`lua_gettop`/`lua_settop`/`lua_push*`/`lua_rotate`/`lua_copy`）、类型转换（`lua_to*`/`lua_is*`）、表操作（`lua_gettable`/`lua_settable`/`lua_rawget`/`lua_rawset`）、函数调用（`lua_callk`/`lua_pcallk`）、协程（`lua_yieldk`/`lua_resume`）、GC 控制（`lua_gc`）、调试接口（`lua_getstack`/`lua_getinfo`）、OOP API（`lua_newclass`/`lua_inherit`/`lua_newobject`/`lua_instanceof`）、tcc 支持（`lua_tcc_*`）、混淆导出（`lua_dump_obfuscated`）等。 |
| `lauxlib.h` | 头文件 | 辅助库头文件，声明缓冲区管理、类型检查、错误处理等便捷函数。 |
| `lauxlib.c` | 源文件 | 辅助库实现，提供 `luaL_newstate`、`luaL_openlibs`、`luaL_loadfile`/`luaL_loadstring`、栈类型检查（`luaL_check*`）、参数错误（`luaL_argerror`）、缓冲区系统（`luaL_Buffer`）、引用系统（`luaL_ref`/`luaL_unref`）等 C API 辅助功能。 |
| `lcode.h` | 头文件 | 中间代码生成器头文件，定义 `BinOpr`、`UnOpr` 枚举、`FuncState` 结构体及各代码生成函数声明。 |
| `lcode.c` | 源文件 | 中间代码生成器（旧解析器 lparser 用），将 AST 节点转换为 VM 指令序列，处理表达式编译、跳转链、常量表管理、寄存器分配等。 |
| `ldebug.h` | 头文件 | 调试接口头文件，声明钩子函数管理、调用栈信息获取、局部变量/上值访问等函数。 |
| `ldebug.c` | 源文件 | 调试支持实现，包括钩子函数触发（`luaD_hook`）、调用栈信息获取（`lua_getstack`/`lua_getinfo`）、局部变量操作（`lua_getlocal`/`lua_setlocal`）、上值操作（`lua_getupvalue`/`lua_setupvalue`）、错误追踪（`luaG_traceexec`）等。 |
| `ldo.h` | 头文件 | 函数调用与栈操作头文件，声明保护模式调用、错误恢复、协程调度等函数。 |
| `ldo.c` | 源文件 | 函数调用与协程调度实现，包括保护模式调用（`luaD_pcall`）、错误恢复（`luaD_rawrunprotected`）、栈扩展（`luaD_growstack`）、协程创建/恢复/挂起（`lua_newthread`/`lua_resume`/`lua_yield`）等。 |
| `ldump.c` | 源文件 | 字节码序列化器（含加密），将函数原型（`Proto`）序列化为二进制字节码，支持 SHA-256 签名、时间戳加密、动态操作码映射等安全特性。 |
| `lfunc.h` | 头文件 | 函数原型管理头文件，声明闭包创建、上值操作、函数原型管理等函数。 |
| `lfunc.c` | 源文件 | 函数原型管理实现，包括闭包创建（`luaF_newCclosure`/`luaF_newLclosure`）、上值创建/查找（`luaF_findupval`）、函数原型管理（`luaF_newproto`）等。 |
| `lgc.h` | 头文件 | 垃圾回收器头文件，声明增量式和分代式 GC 的接口函数。 |
| `lgc.c` | 源文件 | 垃圾回收器实现，支持增量式（`LUA_GCINC`）和分代式（`LUA_GCGEN`）两种模式。包括标记阶段（`luaC_mark`）、清除阶段（`luaC_sweep`）、写屏障（`luaC_barrier_*`）、终结器（`luaC_runfinalizer`）等。 |
| `linit.c` | 源文件 | 库初始化注册表，定义 `loadedlibs` 数组，将所有标准库注册到 Lua 状态机中。 |
| `llimits.h` | 头文件 | 内部数值限制定义，包括最大栈深度、类型转换宏、整数类型选择等编译时常量。 |
| `lmap.h` | 头文件 | Map 数据结构头文件，声明 `LUA_TMAP` 类型的哈希 Map 容器操作。 |
| `lmap.c` | 源文件 | Map 容器实现，基于哈希表的高效键值对存储，支持 `luaH_mapget`、`luaH_mapset`、`luaH_mapnew` 等操作。 |
| `lmem.h` | 头文件 | 内存管理头文件，声明内存分配/释放、对象创建、数组管理等功能。 |
| `lmem.c` | 源文件 | 内存管理实现，包装内存分配/释放操作（`luaM_malloc`/`luaM_free`/`luaM_realloc`），提供对象创建（`luaM_new`/`luaM_newvector`）和错误处理（`luaM_error`）等。 |
| `lobject.h` | 头文件 | 对象系统头文件，定义 Lua 所有值类型的底层表示：`Value`（联合体）、`TValue`（带标签的值）、`GCObject`（可回收对象基类）、`Table`、`String`、`Proto`、`Closure`、`UpVal`、`Struct`、`SuperStruct`、`Namespace` 等结构体，以及类型测试宏（`ttis*`）、值访问器（`ivalue`/`fltvalue`/`gcvalue` 等）。 |
| `lobject.c` | 源文件 | 对象系统实现，包括类型转换（`luaO_*`）、对象创建（`luaO_new*`）、字符串比较、哈希计算等底层操作。 |
| `lopcodes.h` | 头文件 | 操作码定义头文件，定义 VM 指令集（64 位指令格式）的所有操作码枚举（`OpCode`）、操作数类型（`OpArg*`）、指令构造/解码宏（`CREATE_*`/`GETARG_*`）、操作码属性（`opmode`）等。 |
| `lopcodes.c` | 源文件 | 操作码实现，包括操作码名称表（`opnames`）、操作码属性表（`opmodes`）、指令验证函数（`luaP_opmodes`）等。 |
| `lopnames.h` | 头文件 | 操作码名称字符串定义，将操作码枚举映射为可读的名称字符串。 |
| `lprefix.h` | 头文件 | 预编译头，统一包含平台头文件（`<stddef.h>`、`<stdlib.h>`、`<string.h>`、`<stdio.h>` 等），定义基础类型别名（`lu_byte`、`lu_mem` 等）和基础宏（`cast`、`lua_assert`）。 |
| `lstate.h` | 头文件 | 全局状态头文件，定义 `global_State`（全局状态）和 `lua_State`（线程状态）结构体，包含 GC 状态、注册表、元表、字符串表、随机数种子等。 |
| `lstate.c` | 源文件 | 全局状态管理实现，包括状态创建（`lua_newstate`）、状态关闭（`lua_close`）、栈初始化（`luaD_reallocstack`）、线程创建（`lua_newthread`）、随机数初始化等。 |
| `lstring.h` | 头文件 | 字符串表头文件，声明字符串内部化、哈希、比较等函数。 |
| `lstring.c` | 源文件 | 字符串管理实现，包括字符串内部化（`luaS_new`/`luaS_newlstr`）、长字符串创建、字符串哈希计算（`luaS_hash`）、字符串比较、字符串缓存等。 |
| `ltable.h` | 头文件 | 表实现头文件，声明哈希表操作、数组部分管理、表遍历等函数。 |
| `ltable.c` | 源文件 | 表实现，包括表创建（`luaH_new`）、键值查找（`luaH_get`/`luaH_getint`/`luaH_getstr`）、键值设置（`luaH_set`/`luaH_setint`）、表遍历（`luaH_next`）、数组部分扩展（`luaH_resize`）、Rehash 等。 |
| `ltm.h` | 头文件 | 元方法头文件，声明元方法索引、快速访问宏、元方法调用函数。 |
| `ltm.c` | 源文件 | 元方法实现，包括元方法名称表（`luaT_typenames`）、元方法快速访问（`luaT_gettm`/`luaT_gettmbyobj`）、元方法调用（`luaT_callTM`/`luaT_callbinTM`）等。 |
| `lualib.h` | 头文件 | 库加载头文件，声明 `luaL_openlibs`（打开所有标准库）及各标准库的打开函数（`luaopen_base`、`luaopen_math`、`luaopen_string` 等）。 |
| `lundump.h` | 头文件 | 字节码加载头文件，声明字节码反序列化、签名验证、解密等函数。 |
| `lundump.c` | 源文件 | 字节码加载器实现，反序列化预编译的 Lua 字节码，包括签名验证（`LUA_SIGNATURE` `\x1bXCF`）、时间戳解密、SHA-256 完整性校验、动态操作码映射还原等安全特性。 |
| `lzio.h` | 头文件 | 缓冲输入/输出头文件，定义 `ZIO`（缓冲输入流）和 `Mbuffer`（内存缓冲区）结构体。 |
| `lzio.c` | 源文件 | 缓冲输入/输出实现，提供带缓冲的字符流读取（`luaZ_read`/`luaZ_fill`）、内存缓冲区管理（`luaZ_initbuffer`/`luaZ_resizebuffer`）等。 |

---

## 2. src/compiler/ — 编译器

**目录**：`src/compiler/`  
**文件数**：21 个  
**依赖**：核心层（`src/core/`）、VM 运行时层（部分）  
**功能**：将 LXCLUA 源码编译为可执行的字节码。

### 编译流水线

```
源码 (.lua) ──→ llex (词法分析) ──→ lparser (语法分析) ──→ lcodegen (代码生成) ──→ lasm (汇编) ──→ 字节码 (.luac)
                              │
                              └──→ last_parse (AST 解析) ──→ last_serialize (AST 序列化) ──→ last_visitor (AST 访问)
```

### 文件列表

| 文件 | 类型 | 功能描述 |
|------|------|----------|
| `llex.h` | 头文件 | 词法分析器头文件，定义 Token 类型枚举（`TK_*`）、词法状态结构体（`LexState`）、`SemInfo` 语义值联合体等。 |
| `llex.c` | 源文件 | 词法分析器实现，将源码字符流分解为 Token 序列。支持扩展运算符（`<=>`、`??`、`?.`、`|>`、`:=` 等）、字符串插值 `${}`、原生字符串 `_raw""`、Shell 测试表达式 `[ ]`、正则表达式字面量、多行注释、数字格式（二进制 `0b`、十六进制 `0x`、科学计数法）等。 |
| `lparser.h` | 头文件 | 语法分析器头文件，定义 `FuncState`、`BlockCnt`、`Dyndata` 等结构体以及解析函数声明。 |
| `lparser.c` | 源文件 | 语法分析器（旧解析器），递归下降解析。支持 class/interface/struct/enum、switch/when、try-catch-finally、defer/with、namespace/using、箭头函数/Lambda/C 风格函数、泛型、async/await、列表/字典推导式、解构赋值、预处理器指令、内联 asm 等现代语言特性。 |
| `last.h` | 头文件 | AST 数据结构定义，定义所有 AST 节点类型：`AstNode`（基类）、`AstExpr`（表达式节点，含 40+ 种表达式类型）、`AstStmt`（语句节点，含 40+ 种语句类型）、`AstFunc`（函数定义节点）、`AstChunk`（编译单元）、`AstPool`（Arena 内存池）等。还包括二元运算符枚举（`AstBinOp`）、一元运算符枚举（`AstUnOp`）、匹配模式（`AstMatchPat`）、类成员（`AstClassMember`）等。 |
| `last.c` | 源文件 | AST 通用定义和工具函数实现，包括 AST 内存池初始化/释放（`ast_pool_init`/`ast_pool_free`）、节点分配（`ast_pool_alloc`/`ast_node_new`）、节点构造函数（`ast_new_expr_*`/`ast_new_stmt_*`）、调试打印（`ast_dump_expr`/`ast_dump_stmt`/`ast_dump_func`/`ast_dump_chunk`）等。 |
| `last_parse.h` | 头文件 | AST 解析器头文件，声明从词法 Token 流构建 AST 树的入口函数。 |
| `last_parse.c` | 源文件 | AST 解析器实现，将词法分析器产出的 Token 流解析为结构化 AST 树，构建 `AstChunk`→`AstFunc`→`AstStmt`/`AstExpr` 的完整层级结构。 |
| `last_serialize.h` | 头文件 | AST 序列化器头文件，声明 AST 树的序列化/反序列化函数。 |
| `last_serialize.c` | 源文件 | AST 序列化器实现，将 AST 树序列化为可存储/传输的二进制格式，支持从二进制格式反序列化还原 AST。 |
| `last_visitor.h` | 头文件 | AST 访问者模式头文件，声明 AST 遍历和操作接口。 |
| `last_visitor.c` | 源文件 | AST 访问者模式实现，提供遍历 AST 节点的通用接口，支持自定义访问者回调函数。 |
| `lcodegen.h` | 头文件 | 代码生成器头文件，定义 `CodegenState`（代码生成上下文，含循环层级栈、label/goto 表）、`LoopJump` 结构体，声明 `luaY_codegen_func`/`luaY_codegen_chunk` 入口函数。 |
| `lcodegen.c` | 源文件 | AST 到字节码代码生成器实现，将 AST 树转换为 VM 中间代码（Proto），处理操作符优先级、寄存器分配、跳转标签、循环 break/continue、label/goto、复合赋值、自增/自减、match 表达式、try-catch 等所有 AST 节点类型的代码生成。 |
| `lasm.h` | 头文件 | LXCLUA 汇编器头文件，声明汇编相关函数和结构体。 |
| `lasm.c` | 源文件 | LXCLUA 汇编器实现，将中间代码汇编为最终字节码，支持内联汇编（`asm`）语法，处理指令编码、操作数格式。 |
| `lbctc.h` | 头文件 | 字节码到 C 代码生成器头文件，声明 tcc 转换入口函数。 |
| `lbctc.c` | 源文件 | 字节码到 C 代码转换器（tcc），将 Lua 字节码转换为 C 源代码，便于嵌入 C 项目或外部编译优化。 |
| `lbctc_api_list.h` | 头文件 | 字节码到 C API 列表，定义 tcc 转换过程中使用的辅助 API 函数声明集合。 |
| `llexer_compiler.h` | 头文件 | 词法编译器接口头文件，声明供 lexer 库使用的编译器后端接口。 |
| `llexer_compiler.c` | 源文件 | 词法编译器接口实现，提供 Lua 层面的词法分析编译功能。 |
| `llexerlib.c` | 源文件 | 词法分析器 Lua 库（`lexer` 模块），提供 `require("lexer")` 的库入口，封装词法分析/编译功能给 Lua 代码使用。 |

---

## 3. src/vm/ — 虚拟机

**目录**：`src/vm/`（不含 JIT 子目录）  
**文件数**：9 个  
**依赖**：核心层（`src/core/`）  
**功能**：字节码的解释执行和运行时环境管理。

### 文件列表

| 文件 | 类型 | 功能描述 |
|------|------|----------|
| `lvm.h` | 头文件 | 虚拟机主头文件，声明 VM 核心函数：`luaV_execute`（主执行循环）、`luaV_equalobj`（对象相等比较）、`luaV_lessthan`/`luaV_lessequal`（比较）、`luaV_concat`（字符串连接）、`luaV_objlen`（取长度）、`luaV_getinst`（获取指令）、类型转换宏（`tonumber`/`tointeger`）、快速表访问宏（`luaV_fastget`/`luaV_fastset`）、异步系统设置函数等。 |
| `lvm.c` | 源文件 | 虚拟机主循环实现，执行字节码指令。64 位指令格式，支持 XCLUA 扩展指令集。包含所有操作码的 case 分支（computed goto 优化），以及类型转换、算术运算、比较、表访问、函数调用、协程切换等运行时操作。 |
| `lvmlib.c` | 源文件 | VM 内省 Lua 库（`vm` 模块），提供 `require("vm")` 的库入口，支持字节码级别的 VM 内省和分析功能。 |
| `lvmpro.c` | 源文件 | VM 保护库（`vmprotect` 模块），基于 VM 的代码保护，提供自定义指令集混淆，运行时字节码与标准格式不兼容，增强反逆向能力。 |
| `lvmustom.c` | 源文件 | 自定义操作码扩展系统（`vmcustom` 模块），允许用户注册自定义操作码处理函数，扩展 VM 指令集。 |
| `lbytecode.c` | 源文件 | 字节码操作和分析库（`ByteCode` 模块），提供字节码级别操作接口，如指令读取/修改、字节码遍历、指令分析等。 |
| `lnativevm.c` | 源文件 | 原生 VM 接口（`nativevm` 模块），提供与原生 VM 执行相关的接口和功能。 |
| `lnativeparser.c` | 源文件 | 原生解析器接口（`nativeparser` 模块），提供原生解析器功能和接口。 |
| `ljumptab.h` | 头文件 | VM 指令跳转表，使用 computed goto 技术优化 VM 主循环的分支预测性能，将操作码直接映射到代码标签地址。 |

---

## 4. src/vm/jit/ — JIT 编译器

**目录**：`src/vm/jit/`  
**文件数**：35 个（分布在 7 个子目录中）  
**依赖**：核心层、VM 运行时层、SLJIT 后端（`src/jit/`）  
**功能**：基于 SLJIT 的跨平台即时编译，将热点字节码直接编译为原生机器码。

### JIT 编译流水线

```
字节码 ──→ [前端分析] ──→ IR ──→ [优化器] ──→ [寄存器分配] ──→ [代码生成] ──→ 原生码
              │                │         │              │               │
         ljit_analyze     ljit_ir   ljit_opt_*   ljit_regalloc   ljit_codegen
         ljit_translate             (5种优化)     (图着色)        ljit_cg_*
```

### 4.1 jit/core/ — JIT 核心

| 文件 | 类型 | 功能描述 |
|------|------|----------|
| `ljit.h` | 头文件 | JIT 编译器主头文件，声明 JIT 全局开关变量（`XCLUA_JIT_ENABLED`、`XCLUA_JIT_HOTCOUNT` 等）和核心 API：`luaJIT_init`（初始化）、`luaJIT_free`（释放）、`luaJIT_compile`（编译函数）、`luaJIT_free_trace`（释放 trace）、`luaJIT_enable`/`luaJIT_disable`（启用/禁用）等。 |
| `ljit.c` | 源文件 | JIT 编译器主入口实现，负责编译触发（热点检测）、编译缓存管理、trace 生命周期管理、JIT 开关控制。 |
| `ljit_internal.h` | 头文件 | JIT 内部定义，声明 JIT 子系统内部使用的数据结构、常量和函数。 |
| `ljit_debug.h` | 头文件 | JIT 调试支持宏，提供 JIT 编译过程的调试输出和断言检查。 |

### 4.2 jit/frontend/ — 前端：字节码 → IR

| 文件 | 类型 | 功能描述 |
|------|------|----------|
| `ljit_analyze.h` | 头文件 | 热点分析器头文件，声明字节码分析、编译范围确定等函数。 |
| `ljit_analyze.c` | 源文件 | 热点分析实现，分析字节码执行频率，确定哪些代码段值得 JIT 编译，决定编译范围（trace 边界）。 |
| `ljit_translate.c` | 源文件 | 字节码翻译器实现，将选定的字节码序列翻译为 JIT IR（中间表示）指令。 |

### 4.3 jit/ir/ — 中间表示 (IR)

| 文件 | 类型 | 功能描述 |
|------|------|----------|
| `ljit_ir.h` | 头文件 | IR 指令定义头文件，声明 IR 指令类型枚举、IR 指令结构体、IR 操作数类型等。 |
| `ljit_ir.c` | 源文件 | IR 指令定义与操作实现，包括 IR 指令的创建、复制、销毁等操作。 |
| `ljit_ir_list.c` | 源文件 | IR 指令链表管理，维护 IR 指令的双向链表，支持插入、删除、遍历等操作。 |
| `ljit_ir_label.c` | 源文件 | IR 标签管理，管理 IR 中的跳转目标和标签。 |
| `ljit_ir_bb.c` | 源文件 | 基本块（Basic Block）管理，划分 IR 指令序列为基本块，构建控制流图。 |

### 4.4 jit/optimize/ — 优化器

| 文件 | 类型 | 功能描述 |
|------|------|----------|
| `ljit_opt.h` | 头文件 | 优化器主头文件，声明优化调度和优化 pass 接口。 |
| `ljit_opt.c` | 源文件 | 优化器主调度实现，协调各优化 pass 的执行顺序，迭代优化 IR 直到收敛。 |
| `ljit_opt_const.c` | 源文件 | 常量折叠与传播优化，在编译时计算常量表达式结果，消除不必要的运行时计算。 |
| `ljit_opt_dce.c` | 源文件 | 死代码消除（Dead Code Elimination），移除对程序结果无影响的 IR 指令。 |
| `ljit_opt_peep.c` | 源文件 | 窥孔优化（Peephole Optimization），在局部指令窗口内识别并替换低效指令模式。 |
| `ljit_opt_cse.c` | 源文件 | 公共子表达式消除（Common Subexpression Elimination），识别并复用重复计算的表达式。 |
| `ljit_opt_inline.c` | 源文件 | 函数内联（Inlining），将小函数体直接展开到调用点，减少函数调用开销。 |

### 4.5 jit/regalloc/ — 寄存器分配

| 文件 | 类型 | 功能描述 |
|------|------|----------|
| `ljit_regalloc.h` | 头文件 | 寄存器分配器主头文件，声明寄存器分配调度和接口。 |
| `ljit_regalloc.c` | 源文件 | 寄存器分配器主调度实现，协调活跃变量分析、干涉图构建、着色、溢出等阶段。 |
| `ljit_reg_live.c` | 源文件 | 活跃变量分析，计算每个 IR 变量在程序各点的活跃状态（live range）。 |
| `ljit_reg_graph.c` | 源文件 | 干涉图构建，根据活跃变量分析结果构建寄存器干涉图（interference graph）。 |
| `ljit_reg_color.c` | 源文件 | 图着色算法，对干涉图进行着色，为每个变量分配物理寄存器。 |
| `ljit_reg_spill.c` | 源文件 | 溢出处理，当寄存器不足时将部分变量溢出到栈内存，插入 spill/reload 指令。 |
| `ljit_reg_alloc.c` | 源文件 | 寄存器分配结果实现，将寄存器分配结果应用到 IR 指令中。 |

### 4.6 jit/codegen/ — 代码生成

| 文件 | 类型 | 功能描述 |
|------|------|----------|
| `ljit_codegen.h` | 头文件 | 代码生成主头文件，声明代码生成调度接口。 |
| `ljit_codegen.c` | 源文件 | 代码生成主调度实现，将优化后的 IR 通过 SLJIT 后端生成为原生机器码。 |
| `ljit_cg_arith.c` | 源文件 | 算术运算代码生成，处理加法、减法、乘法、除法、取模等算术 IR 指令。 |
| `ljit_cg_ctrl.c` | 源文件 | 控制流代码生成，处理分支、跳转、循环等控制流 IR 指令。 |
| `ljit_cg_table.c` | 源文件 | 表操作代码生成，处理表读写、表创建等表相关 IR 指令。 |
| `ljit_cg_conv.c` | 源文件 | 类型转换代码生成，处理整数/浮点数/字符串等类型转换 IR 指令。 |
| `ljit_cg_closure.c` | 源文件 | 闭包操作代码生成，处理闭包创建、上值访问等闭包相关 IR 指令。 |
| `ljit_cg_oop.c` | 源文件 | 面向对象操作代码生成，处理类创建、继承、方法调用等 OOP 相关 IR 指令。 |

### 4.7 jit/sljit/ — SLJIT 后端适配

| 文件 | 类型 | 功能描述 |
|------|------|----------|
| `ljit_sljit.h` | 头文件 | SLJIT 绑定层头文件，声明将 IR 映射到 SLJIT 指令的接口函数。 |
| `ljit_sljit.c` | 源文件 | SLJIT 绑定层实现，将 JIT 内部 IR 翻译为 SLJIT 的 LIR 指令，调用 SLJIT 后端生成原生代码。 |
| `ljit_sljit_mac.h` | 头文件 | SLJIT 宏定义，封装 SLJIT 底层 API 调用，简化代码生成逻辑。 |

---

## 5. src/stdlib/ — 标准库

**目录**：`src/stdlib/`  
**文件数**：28 个  
**依赖**：核心层、VM 运行时层  
**功能**：Lua 标准库和扩展库，通过 `require()` 加载使用。

### 文件列表

| 文件 | 类型 | 对应 Lua 库 | 功能描述 |
|------|------|------------|----------|
| `lbaselib.c` | 源文件 | `base` | 基础函数库，提供 `print`、`type`、`error`、`pcall`、`xpcall`、`assert`、`select`、`ipairs`、`pairs`、`next`、`rawequal`、`rawget`、`rawset`、`setmetatable`、`getmetatable`、`tonumber`、`tostring` 等内置函数。 |
| `lmathlib.c` | 源文件 | `math` | 数学库，提供三角函数（`sin`、`cos`、`tan` 等）、对数函数（`log`、`log10`、`exp`）、取整函数（`floor`、`ceil`）、随机数（`random`/`randomseed`）、常量（`pi`、`huge`）、最大/最小值（`max`/`min`）、双曲函数等。 |
| `lstrlib.c` | 源文件 | `string` | 字符串库，提供模式匹配（`match`、`gmatch`、`gsub`）、格式化（`format`）、长度（`len`）、大小写转换（`upper`/`lower`）、字符编码（`byte`/`char`）、查找（`find`）、反转（`reverse`）、分割（`split`）、PCRE2 正则表达式支持等。 |
| `ltablib.c` | 源文件 | `table` | 表操作库，提供 `insert`、`remove`、`sort`、`concat`、`move`、`pack`、`unpack`、`freeze`、`clone`、`size` 等表操作函数。 |
| `liolib.c` | 源文件 | `io` | I/O 库，提供文件读写操作（`open`、`close`、`read`、`write`、`seek`、`flush`）、标准输入输出（`stdin`、`stdout`、`stderr`）、临时文件（`tmpfile`）、管道（`popen`）等。 |
| `loslib.c` | 源文件 | `os` | 操作系统库，提供时间日期（`time`、`date`、`clock`）、系统命令（`execute`、`exit`、`getenv`、`setenv`）、文件操作（`rename`、`remove`、`tmpname`）、时钟精度（`difftime`）等。 |
| `lcorolib.c` | 源文件 | `coroutine` | 协程库，提供 `create`、`resume`、`yield`、`wrap`、`status`、`running`、`isyieldable` 等协程操作函数。 |
| `ldblib.c` | 源文件 | `debug` | 调试库，提供栈跟踪（`traceback`）、变量访问（`getlocal`/`setlocal`、`getupvalue`/`setupvalue`）、钩子管理（`sethook`/`gethook`）、调试信息（`getinfo`）、内存和 GC 调试等。 |
| `loadlib.c` | 源文件 | `package` | 模块加载库，提供 `require`、`module`、`package.path`、`package.cpath`、`package.loaded`、`package.searchers`、`package.preload`、`package.searchpath`、DLL 加载等模块管理功能。 |
| `lutf8lib.c` | 源文件 | `utf8` | UTF-8 编码支持库，提供 UTF-8 字符操作（`char`、`codes`、`codepoint`、`len`、`offset`）、字符宽度计算、合法性验证等。 |
| `lbitlib.c` | 源文件 | `bit` / `bit32` | 位运算库，提供位与（`band`）、位或（`bor`）、位异或（`bxor`）、位非（`bnot`）、左移（`lshift`）、右移（`rshift`）、算术右移（`arshift`）、位测试（`btest`）等。 |
| `lboolib.c` | 源文件 | `bool` | 布尔增强库，提供布尔值相关的增强操作和工具函数。 |
| `lmaplib.c` | 源文件 | `map` | Map 容器库，提供 `LUA_TMAP` 类型的操作接口，包括键值对增删改查、遍历、大小查询等。 |
| `lclass.h` | 头文件 | — | 类系统头文件，声明类创建、继承、接口实现、属性管理等内部函数。 |
| `lclass.c` | 源文件 | `class` | 类系统支持库，提供 `class` 关键字运行时支持，包括类定义、继承链、接口实现、属性 getter/setter、方法调用、super 访问等。 |
| `lstruct.h` | 头文件 | — | C 风格结构体头文件，声明 `LUA_TSTRUCT` 类型的内部结构和操作函数。 |
| `lstruct.c` | 源文件 | — | C 风格结构体支持，提供 `struct` 关键字运行时支持，包括结构体定义、字段访问、内存布局、对齐等。 |
| `lsuper.h` | 头文件 | — | SuperStruct 增强表头文件，声明 `LUA_TSUPERSTRUCT` 类型的内部结构和操作函数。 |
| `lsuper.c` | 源文件 | — | SuperStruct 增强表支持，提供 `superstruct` 关键字运行时支持，增强表具有强类型约束、字段验证、默认值等特性。 |
| `lptrlib.c` | 源文件 | `ptr` | 指针操作库，提供原始指针类型的创建、解引用、偏移、类型转换等操作。 |
| `ludatalib.c` | 源文件 | `userdata` | userdata 二进制序列化库，支持 userdata 的二进制序列化/反序列化、内存布局分析等。 |
| `lthreadlib.c` | 源文件 | `thread` | 多线程库，提供互斥锁（`mutex`）、条件变量（`condition`）、读写锁（`rwlock`）等线程同步原语。 |
| `lfs.c` | 源文件 | `fs` | 文件系统操作库，提供目录遍历、文件属性获取、路径操作、文件系统监控等。 |
| `lproclib.c` | 源文件 | `process` | 进程管理库，提供进程创建、进程间通信、信号处理、环境变量管理等。 |
| `lastlib.c` | 源文件 | `lexer` | AST 操作 Lua 库入口，封装 `llexer_compiler` 和 `llexerlib` 功能，提供 `require("lexer")` 的库入口。 |
| `ljit_stubs.c` | 源文件 | — | JIT 桩模块，在无 JIT 平台（如 WASM）使用，提供空实现 JIT API 函数，保持接口兼容性。 |
| `ltests.h` | 头文件 | — | 测试库头文件，定义调试内存控制结构体（`Memcontrol`）、测试宏（`LUA_DEBUG`、`LUAI_ASSERT`）、锁测试宏（`lua_lock`/`lua_unlock`）、测试配置（缩小缓冲区、字符串表等以触发边界条件）等。 |
| `ltests.c` | 源文件 | `T`（调试用） | 测试库实现，提供内存分配调试（`debug_realloc`）、内存检查（`lua_checkmemory`）、对象打印（`lua_printobj`）、各类型对象计数（`objcount`）等调试功能。 |

---

## 6. src/utils/ — 工具库

**目录**：`src/utils/`  
**文件数**：38 个  
**依赖**：核心层、VM 运行时层  
**功能**：提供密码学、网络、异步 I/O、代码混淆、JSON 解析等扩展工具库。

### 文件列表

| 文件 | 类型 | 对应 Lua 库 | 功能描述 |
|------|------|------------|----------|
| `sha256.h` | 头文件 | — | SHA-256 哈希算法头文件，声明 SHA-256 上下文结构体和计算函数。 |
| `sha256.c` | 源文件 | — | SHA-256 哈希算法实现，用于字节码完整性校验和通用密码学用途。 |
| `aes.h` | 头文件 | — | AES 加密算法头文件，声明 AES 上下文和加密/解密函数。 |
| `aes.c` | 源文件 | — | AES 加密算法实现，支持多种密钥长度和加密模式（ECB、CBC 等）。 |
| `crc.h` | 头文件 | — | CRC 校验头文件，声明 CRC 计算函数。 |
| `crc.c` | 源文件 | — | CRC 校验实现，支持 CRC32 等多种校验算法。 |
| `csprng.h` | 头文件 | — | 密码学安全随机数头文件，声明安全随机数生成函数。 |
| `csprng.c` | 源文件 | — | 密码学安全随机数实现，提供高质量随机数生成，用于加密密钥生成等场景。 |
| `encrypt_bytecode.c` | 源文件 | — | 字节码加密工具，提供字节码加密的独立工具函数，可在编译时和运行时使用。 |
| `json_parser.h` | 头文件 | — | JSON 解析器头文件，声明 JSON 解析和序列化函数。 |
| `json_parser.c` | 源文件 | — | JSON 解析器实现，将 JSON 字符串解析为 Lua 表，或将 Lua 表序列化为 JSON 字符串。 |
| `lcrypto.c` | 源文件 | `crypto` | 密码学算法库入口，整合 SHA-256、AES、HMAC、CRC32、CSPRNG 等算法，提供 `require("crypto")` 的统一接口。 |
| `lctype.h` | 头文件 | — | 字符类型分类头文件，声明字符分类函数（字母、数字、空白、十六进制等）。 |
| `lctype.c` | 源文件 | — | 字符类型分类实现，提供高效的字符类型判断表查询。 |
| `lbigint.h` | 头文件 | — | 大整数头文件，声明大整数数据结构和运算函数。 |
| `lbigint.c` | 源文件 | `bigint` | 大整数运算库，支持任意精度整数运算，包括加减乘除、取模、幂运算、位运算、比较等。 |
| `lecc.c` | 源文件 | `ecc` | 椭圆曲线加密库，提供 ECC 密钥生成、签名、验证、密钥交换等非对称加密功能。 |
| `lrsa.c` | 源文件 | `rsa` | RSA 非对称加密库，提供 RSA 密钥生成、加密、解密、签名、验证等功能。 |
| `lsha1.c` | 源文件 | — | SHA-1 哈希算法实现，提供 SHA-1 消息摘要计算功能。 |
| `luuid.c` | 源文件 | `uuid` | UUID 生成库，支持 v4（随机）和 v7（时间有序）UUID 生成。 |
| `laio.h` | 头文件 | — | 异步 I/O 头文件，声明异步 I/O 操作接口和数据结构。 |
| `laio.c` | 源文件 | `asyncio` | 异步 I/O 库，提供非阻塞 I/O 操作，支持文件异步读写、网络异步通信等。 |
| `leventloop.h` | 头文件 | — | 事件循环头文件，声明事件循环结构和操作函数。 |
| `leventloop.c` | 源文件 | — | 事件循环实现，提供事件驱动的异步编程模型，管理定时器、I/O 事件、空闲回调等。 |
| `lpromise.h` | 头文件 | — | Promise 头文件，声明 Promise 数据结构和操作函数。 |
| `lpromise.c` | 源文件 | — | Promise 实现，提供 Promise/A+ 规范的异步编程支持，包括 `then`、`catch`、`finally`、`all`、`race`、`resolve`、`reject` 等。 |
| `libhttp.c` | 源文件 | `http` | HTTP 客户端/服务端和 Socket 库，提供 HTTP 请求（GET、POST 等）、HTTP 服务器、WebSocket、TCP/UDP Socket 通信等功能。 |
| `lthread.h` | 头文件 | — | 线程工具头文件，声明底层线程创建、同步、互斥锁等基础设施。 |
| `lthread.c` | 源文件 | — | 线程基础设施实现，提供 `lthreadlib` 的底层支持，包括线程创建、锁管理、条件变量等。 |
| `lnamespace.h` | 头文件 | — | 命名空间头文件，声明 `LUA_TNAMESPACE` 类型的内部结构和操作函数。 |
| `lnamespace.c` | 源文件 | — | 命名空间运行时支持，提供 `namespace` 关键字运行时支持，包括命名空间创建、成员访问、嵌套、using 导入等。 |
| `lobfuscate.h` | 头文件 | — | 代码混淆头文件，声明混淆标志常量（`OBFUSCATE_CFF`、`OBFUSCATE_BLOCK_SHUFFLE` 等）、混淆函数接口。 |
| `lobfuscate.c` | 源文件 | — | 代码混淆核心实现，提供控制流扁平化（CFF）、基本块洗牌、虚假块插入、状态编码混淆、VM 保护、字符串加密等多层混淆保护。 |
| `logtable.c` | 源文件 | `logtable` | 日志表支持，提供格式化日志输出、日志级别管理、日志旋转等表格化日志功能。 |
| `lpatchlib.c` | 源文件 | — | 热修复补丁库，支持运行时替换函数实现，无需重启即可更新代码逻辑。 |
| `ltranslator.h` | 头文件 | — | 翻译器头文件，声明代码翻译转换接口和数据结构。 |
| `ltranslator.c` | 源文件 | `translator` | 代码翻译工具，提供 Lua 代码到其他语言（或格式）的翻译转换功能。 |
| `unidata.h` | 头文件 | — | Unicode 数据表，提供 Unicode 字符属性数据，如字符分类、大小写映射、数字值等。 |

---

## 7. src/lspsrv/ — LSP 服务器

**目录**：`src/lspsrv/`  
**文件数**：11 个  
**依赖**：核心层、编译器层  
**功能**：内置语言服务器协议（LSP）实现，提供 IDE 级智能支持。  
**构建产物**：`lxclua-lsp` / `lxclua-lsp.exe`

### 文件列表

| 文件 | 类型 | 功能描述 |
|------|------|----------|
| `lspsrv.h` | 头文件 | LSP 服务器主头文件，所有模块共享的类型定义和接口声明，包括 LSP 数据结构、消息类型、协议常量等。 |
| `lspsrv_main.c` | 源文件 | LSP 服务器主入口，实现主循环和 JSON-RPC 通信，处理 stdin/stdout 的 LSP 消息收发、初始化握手、关闭退出等生命周期。 |
| `lspsrv_json.c` | 源文件 | JSON-RPC 消息解析器，实现 JSON-RPC 2.0 协议的请求、响应、通知消息的编码与解码。 |
| `lspsrv_proto.c` | 源文件 | LSP 协议处理，实现 LSP 协议消息的解析与路由分发，将收到的 JSON-RPC 消息分发到对应的功能处理模块。 |
| `lspsrv_doc.c` | 源文件 | 文档管理，实现文档同步（`textDocument/didOpen`、`didChange`、`didClose`）、增量更新、诊断信息管理（`textDocument/publishDiagnostics`）。 |
| `lspsrv_lexer.c` | 源文件 | 实时词法分析，对文档内容进行词法分析，提供语义高亮 Token（`textDocument/semanticTokens`）、语法错误检测等。 |
| `lspsrv_kwdb.c` | 源文件 | 关键字数据库，维护 LXCLUA 关键字、内置函数、标准库函数的信息库，提供关键字/函数名查询、签名信息、文档注释等。 |
| `lspsrv_complete.c` | 源文件 | 智能代码补全，实现 `textDocument/completion` 请求，根据上下文提供变量名、函数名、关键字、成员方法等补全候选项。 |
| `lspsrv_hover.c` | 源文件 | 悬停提示与跳转，实现 `textDocument/hover`（悬停信息）、`textDocument/definition`（跳转定义）、`textDocument/typeDefinition`（类型定义跳转）等。 |
| `lspsrv_features.c` | 源文件 | 高级功能特性，实现 `textDocument/references`（查找引用）、`textDocument/rename`（重命名）、`textDocument/signatureHelp`（签名帮助）、`textDocument/codeAction`（代码操作）、`textDocument/formatting`（格式化）等。 |
| `lspsrv_util.c` | 源文件 | 通用工具函数，提供字符串处理、路径操作、文件读写、缓存管理、内存池等各模块共享的工具函数。 |

---

## 8. src/lua2wasm/ — Lua 到 WASM 编译器

**目录**：`src/lua2wasm/`  
**文件数**：21 个（含 1 个 `.wat` 预置模块）  
**依赖**：核心层  
**功能**：将 Lua 源码编译为 WebAssembly 模块的完整编译器管线。  
**构建产物**：`lua2wasm` / `lua2wasm.exe`、`wat2wasm` / `wat2wasm.exe`

### 编译管线

```
Lua 源码 ──→ lexer (词法分析) ──→ parser (语法分析) ──→ AST ──→ codegen (代码生成) ──→ WAT ──→ wat2wasm (汇编) ──→ WASM
```

### 文件列表

| 文件 | 类型 | 功能描述 |
|------|------|----------|
| `main.c` | 源文件 | CLI 主程序入口，实现 `lua2wasm` 命令行工具，解析命令行参数，调用编译管线将 Lua 源码编译为 WASM 模块。 |
| `lua2wasmlib.c` | 源文件 | Lua 模块入口，提供 `require("lua2wasm")` 的 Lua C 模块封装，允许在 Lua 代码中调用编译功能。 |
| `emscripten_entry.c` | 源文件 | Emscripten 入口，提供 WebAssembly/Emscripten 平台下的编译入口适配。 |
| `lexer.h` | 头文件 | 词法分析器头文件，声明 Lua 子集词法分析器的 Token 类型和接口。 |
| `lexer.c` | 源文件 | 词法分析器实现，将 Lua 源码字符流分解为 Token 序列，支持 Lua 子集语法。 |
| `parser.h` | 头文件 | 语法分析器头文件，声明递归下降解析器接口和 AST 构建函数。 |
| `parser.c` | 源文件 | 语法分析器实现，递归下降解析 Token 流，生成 AST 树。 |
| `ast.h` | 头文件 | AST 数据结构头文件，定义 lua2wasm 编译器专用的 AST 节点类型和操作。 |
| `ast.c` | 源文件 | AST 数据结构实现，包括 AST 节点创建、内存管理、节点遍历等。 |
| `codegen.h` | 头文件 | 代码生成器头文件，声明 AST → WAT 文本格式的代码生成接口。 |
| `codegen.c` | 源文件 | 代码生成器实现，遍历 AST 树生成 WAT（WebAssembly Text Format）文本格式代码。 |
| `wat_builder.h` | 头文件 | WAT 构建器头文件，声明 WAT 文本格式的构建和输出接口。 |
| `wat_builder.c` | 源文件 | WAT 构建器实现，提供 WAT 文本格式的流式构建和输出，管理 S-表达式层级、缩进、格式化。 |
| `wat2wasm.h` | 头文件 | WAT→WASM 转换器头文件，声明 WAT 文本格式到 WASM 二进制格式的汇编器接口。 |
| `wat2wasm.c` | 源文件 | WAT→WASM 转换器核心实现，将 WAT 文本格式解析并编码为 WASM 二进制模块，包括类型段、函数段、代码段、导入/导出段等。 |
| `wat2wasm_cli.c` | 源文件 | 独立 `wat2wasm` 命令行工具，提供 WAT→WASM 汇编的 CLI 接口。 |
| `builtins.h` | 头文件 | 内置函数头文件，声明 WASM 内置函数（内存管理、字符串操作、数学函数等）。 |
| `builtins.c` | 源文件 | 内置函数实现，提供 WASM 运行时所需的内置函数，如内存分配（`malloc`/`free`）、字符串操作、数值转换等。 |
| `prelude.wat` | WAT 文件 | WAT 预置模块，包含所有 WASM 模块共用的预置代码（导入声明、内存初始化、内置函数导出等）。 |
| `prelude_wat.h` | 头文件 | 预置模块头文件，将 `prelude.wat` 的内容嵌入为 C 字符串常量，供编译时内联。 |
| `xalloc.h` | 头文件 | 跨平台内存分配头文件，声明包装后的内存分配/释放函数。 |
| `xalloc.c` | 源文件 | 跨平台内存分配实现，提供统一的内存分配接口，封装平台差异。 |

---

## 9. src/wasm/ — WASM 运行时

**目录**：`src/wasm/`  
**文件数**：37 个  
**依赖**：核心层  
**功能**：在 Lua 环境中运行 WebAssembly 模块，集成 wasm3 解释器和 wasmtime 运行时。

### 9.1 Lua 绑定层

| 文件 | 类型 | 功能描述 |
|------|------|----------|
| `lwasm3.c` | 源文件 | wasm3 Lua 绑定，提供 `require("wasm3")` 的库入口，封装 wasm3 解释器引擎，支持 WASM 模块加载、函数调用、内存读写、WASI 支持等。 |
| `lwasmtime.c` | 源文件 | wasmtime Lua 绑定，提供 `require("wasmtime")` 的库入口，封装 wasmtime 高性能 JIT 运行时，支持 WASM GC 提案。 |
| `lxclua_wasm.c` | 源文件 | WASM 功能汇总，LXCLUA 的 WASM 导出接口封装，统一管理 WASM 运行时功能。 |

### 9.2 wasm3 核心引擎

| 文件 | 类型 | 功能描述 |
|------|------|----------|
| `wasm3.h` | 头文件 | wasm3 主头文件，定义 wasm3 公共 API 类型和函数，包括模块加载、函数调用、内存管理等。 |
| `wasm3_defs.h` | 头文件 | wasm3 定义头文件，定义内部使用的类型别名、常量、平台适配宏等。 |
| `m3_config.h` | 头文件 | wasm3 编译配置，控制功能开关（如尾调用、多线程、WASI 等）。 |
| `m3_config_platforms.h` | 头文件 | wasm3 平台配置，各平台特定的编译选项和宏定义。 |
| `m3_core.h` | 头文件 | wasm3 核心引擎头文件，声明运行时核心数据结构（`M3Runtime`、`M3Module`、`M3Function` 等）。 |
| `m3_core.c` | 源文件 | wasm3 核心引擎实现，包括运行时创建/销毁、模块加载、函数查找等核心 API。 |
| `m3_code.h` | 头文件 | wasm3 代码段头文件，声明字节码操作码枚举和代码段结构。 |
| `m3_code.c` | 源文件 | wasm3 代码段实现，管理 WASM 函数体的字节码存储和访问。 |
| `m3_compile.h` | 头文件 | wasm3 编译头文件，声明 WASM 字节码到内部 M3 操作码的编译器接口。 |
| `m3_compile.c` | 源文件 | wasm3 编译实现，将 WASM 二进制字节码翻译为 wasm3 内部操作码序列。 |
| `m3_env.h` | 头文件 | wasm3 环境管理头文件，声明模块环境、导入/导出、函数表等结构。 |
| `m3_env.c` | 源文件 | wasm3 环境管理实现，管理 WASM 模块的导入/导出、函数表、内存、全局变量等。 |
| `m3_exec.h` | 头文件 | wasm3 执行引擎头文件，声明字节码解释执行循环。 |
| `m3_exec.c` | 源文件 | wasm3 执行引擎实现，解释执行 M3 内部操作码（computed goto 优化）。 |
| `m3_exec_defs.h` | 头文件 | wasm3 执行定义，声明执行过程中的操作码处理宏。 |
| `m3_exception.h` | 头文件 | wasm3 异常处理，定义 trap/异常处理机制。 |
| `m3_function.h` | 头文件 | wasm3 函数管理头文件，声明函数类型、函数签名、函数调用等结构。 |
| `m3_function.c` | 源文件 | wasm3 函数管理实现，包括函数创建、参数传递、返回值处理等。 |
| `m3_info.h` | 头文件 | wasm3 信息查询头文件，声明模块信息、函数信息等查询函数。 |
| `m3_info.c` | 源文件 | wasm3 信息查询实现，提供模块结构、函数签名、操作码统计等调试信息。 |
| `m3_math_utils.h` | 头文件 | wasm3 数学工具宏，定义浮点数操作、类型转换等数学辅助宏。 |
| `m3_module.c` | 源文件 | wasm3 模块管理，处理 WASM 模块的段解析、链接、实例化等。 |
| `m3_parse.c` | 源文件 | wasm3 模块解析器，解析 WASM 二进制格式的各个段（类型段、导入段、函数段、代码段、导出段等）。 |
| `m3_bind.h` | 头文件 | wasm3 外部函数绑定头文件，声明 C 函数注册为 WASM 导入函数的接口。 |
| `m3_bind.c` | 源文件 | wasm3 外部函数绑定实现，支持将 C 函数注册为 WASM 模块的导入函数，自动处理参数类型转换。 |

### 9.3 WASI 支持层

| 文件 | 类型 | 功能描述 |
|------|------|----------|
| `m3_api_wasi.h` | 头文件 | WASI API 头文件，声明 WASI 标准接口的实现函数。 |
| `m3_api_wasi.c` | 源文件 | WASI API 实现，提供 WASI（WebAssembly System Interface）标准系统调用，包括文件操作、环境变量、时钟、随机数等。 |
| `m3_api_meta_wasi.c` | 源文件 | WASI 元 API，提供 WASI 模块的自动链接和初始化功能。 |
| `m3_api_libc.h` | 头文件 | WASI libc 头文件，声明 WASI 环境下的标准 C 库 API 实现。 |
| `m3_api_libc.c` | 源文件 | WASI libc 实现，提供 WASI 环境下的类 libc 函数，方便 C 代码编译到 WASM 后运行。 |
| `m3_api_tracer.h` | 头文件 | API 追踪器头文件，声明函数调用追踪接口。 |
| `m3_api_tracer.c` | 源文件 | API 追踪器实现，提供 WASM 函数调用的追踪和调试功能。 |
| `m3_api_uvwasi.c` | 源文件 | uvwasi 集成，提供基于 libuv 的 WASI 实现，支持异步 I/O。 |
| `wasi_core.h` | 头文件 | WASI 核心定义，定义 WASI 标准的错误码、权限、文件类型等核心常量。 |

---

## 10. src/bin/ — 应用程序

**目录**：`src/bin/`  
**文件数**：5 个  
**依赖**：所有下层模块  
**功能**：最终用户可执行程序的入口。

### 文件列表

| 文件 | 构建产物 | 功能描述 |
|------|----------|----------|
| `lua.c` | `lxclua` / `lxclua.exe` | LXCLUA 解释器主程序，提供交互式 REPL（支持 readline 编辑）、命令行脚本执行、`-e` 直接执行代码、`-l` 加载库、`-i` 交互模式、`-v` 版本信息等。支持 `LUA_INIT` 环境变量自动执行初始化代码。 |
| `luac.c` | `luac` / `luac.exe` | LXCLUA 字节码编译器，将 `.lua` 源码编译为 `.luac` 字节码文件。支持 `-o` 指定输出文件、`-s` 去除调试信息、`-l` 列出字节码、`-p` 仅解析不生成、混淆选项等。 |
| `luaccheck.c` | `luaccheck` / `luaccheck.exe` | 字节码验证工具，检查字节码文件的完整性、解密混淆字节码、验证签名、显示字节码信息（版本、时间戳、函数列表等）。 |
| `lbcdump.c` | `lbcdump` / `lbcdump.exe` | 字节码反汇编器，将字节码文件转换为可读的指令列表，显示操作码、操作数、常量表、调试信息等，支持解密混淆字节码后反汇编。 |
| `lquickjs.c` | —（库模块） | QuickJS JavaScript 引擎集成入口，提供 `require("quickjs")` 的 C 模块封装，使 Lua 代码可以调用 QuickJS 引擎执行 JavaScript 代码，实现 Lua ↔ JS 双向互操作。 |

---

## 附录：类型系统扩展

LXCLUA-NCore 在 Lua 原有 9 种类型基础上新增了 6 种类型：

| 类型常量 | 值 | 类型名 | 描述 | 相关模块 |
|----------|---|--------|------|----------|
| `LUA_TNIL` | 0 | nil | 空值 | 标准 Lua |
| `LUA_TBOOLEAN` | 1 | boolean | 布尔值 | 标准 Lua |
| `LUA_TLIGHTUSERDATA` | 2 | lightuserdata | 轻量用户数据 | 标准 Lua |
| `LUA_TNUMBER` | 3 | number | 数值（默认 double） | 标准 Lua |
| `LUA_TSTRING` | 4 | string | 字符串 | 标准 Lua |
| `LUA_TTABLE` | 5 | table | 表 | 标准 Lua |
| `LUA_TFUNCTION` | 6 | function | 函数 | 标准 Lua |
| `LUA_TUSERDATA` | 7 | userdata | 完整用户数据 | 标准 Lua |
| `LUA_TTHREAD` | 8 | thread | 协程 | 标准 Lua |
| **`LUA_TSTRUCT`** | **9** | **struct** | C 风格结构体 | `lstruct.c/h` |
| **`LUA_TPOINTER`** | **10** | **pointer** | 原始指针类型 | `lptrlib.c` |
| **`LUA_TCONCEPT`** | **11** | **concept** | 类型谓词概念 | 编译器+运行时 |
| **`LUA_TNAMESPACE`** | **12** | **namespace** | 命名空间类型 | `lnamespace.c/h` |
| **`LUA_TSUPERSTRUCT`** | **13** | **superstruct** | 增强表定义 | `lsuper.c/h` |
| **`LUA_TMAP`** | **14** | **map** | 哈希 Map 容器 | `lmap.c/h` |

---

## 附录：代码混淆系统

在 `lobfuscate.c` 中实现的多层代码混淆保护：

| 混淆标志 | 位掩码 | 描述 |
|----------|--------|------|
| `OBFUSCATE_CFF` | `1<<0` | 控制流扁平化（Control Flow Flattening） |
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

## 附录：安全特性

| 特性 | 实现位置 | 描述 |
|------|----------|------|
| 动态操作码映射 | `lvm.c` | 每次编译使用不同的操作码到指令的映射 |
| 时间戳加密 | `lundump.c` | 字节码嵌入加密时间戳，防止重放 |
| SHA-256 完整性校验 | `sha256.c` | 字节码签名验证，防止篡改 |
| 字节码签名 | `LUA_SIGNATURE` | 自定义魔数 `\x1bXCF` 替代标准 `\x1bLua` |
| 字符串加密 | `lobfuscate.c` | 编译时加密字符串常量，运行时解密 |
| VM 保护 | `lvmpro.c` | 自定义指令集，运行时字节码与标准格式不兼容 |