# LXCLUA-NCore WASM 运行时集成文档

LXCLUA-NCore 集成了完整的 WebAssembly 工具链，包括两个 WASM 运行时（wasm3 和 wasmtime）、一个 Lua→WASM 编译器（lua2wasm），以及将 Lua C API 导出为 WASM 模块的包装层。

---

## 1. 架构概览

```
┌─────────────────────────────────────────────────────────────────┐
│                        Lua 代码层                                │
│  require("wasm3")    require("wasmtime")    require("lua2wasm") │
├─────────────────────────────────────────────────────────────────┤
│   lwasm3.c          lwasmtime.c             lua2wasmlib.c       │
│   (Lua 绑定)        (Lua 绑定)              (Lua 绑定)           │
├─────────────────────────────────────────────────────────────────┤
│   wasm3 (v0.5.1)    wasmtime (v45.0.1)     lua2wasm 编译器      │
│   解释器引擎         JIT 运行时             lexer → parser →     │
│   m3_*.c 核心       支持 GC 提案             codegen → wat2wasm  │
├─────────────────────────────────────────────────────────────────┤
│   lxclua_wasm.c                                                 │
│   Lua C API → WASM 导出（供 wasm3 嵌入 Lua 引擎）               │
└─────────────────────────────────────────────────────────────────┘
```

**两种运行时对比：**

| 特性 | wasm3 | wasmtime |
|------|-------|----------|
| 类型 | 解释器 | JIT 编译器 |
| WASM GC 提案 | 不支持 | 支持（v45.0.1） |
| 体积 | 极小（纯 C） | 较大（预编译库） |
| 适用场景 | 嵌入式、WASM 自身编译 | 运行 lua2wasm 编译产物 |
| 平台 | 全平台（含 Emscripten） | Windows / Android |
| 预编译库 | 无（源码编译） | 有（x86_64-mingw / aarch64-android） |

---

## 2. wasm3 集成

### 2.1 概述

wasm3 是一个轻量级 WebAssembly 解释器（v0.5.1），以纯 C 实现，代码量极小，适合嵌入式和资源受限环境。在 LXCLUA-NCore 中，wasm3 通过 `lwasm3.c` 暴露为 Lua 模块 `require("wasm3")`。

### 2.2 核心源文件

**Lua 绑定层：**

| 文件 | 说明 |
|------|------|
| `src/wasm/lwasm3.c` | wasm3 的 Lua C 绑定，提供完整 Lua API |
| `src/wasm/wasm3.h` | wasm3 公共 API 头文件（环境、运行时、模块、函数） |
| `src/wasm/m3_env.h` | wasm3 内部环境头文件 |

**wasm3 核心引擎（`m3_*.c`）：**

| 文件 | 功能 |
|------|------|
| `m3_core.c` | 核心定义和初始化 |
| `m3_env.c` | 环境管理 |
| `m3_parse.c` | WASM 二进制解析 |
| `m3_compile.c` | 字节码编译 |
| `m3_exec.c` | 字节码执行 |
| `m3_code.c` | 代码段管理 |
| `m3_function.c` | 函数管理 |
| `m3_module.c` | 模块管理 |
| `m3_bind.c` | 函数绑定 |
| `m3_info.c` | 调试信息 |

**WASI 和 LibC 支持：**

| 文件 | 功能 |
|------|------|
| `m3_api_wasi.c` | WASI 标准接口实现 |
| `m3_api_meta_wasi.c` | WASI 元接口 |
| `m3_api_uvwasi.c` | uvwasi 实现 |
| `m3_api_libc.c` | 标准 C 库桥接 |
| `m3_api_tracer.c` | API 调用追踪 |

### 2.3 Lua 绑定设计

`lwasm3.c` 使用 Lua userdata + metatable 模式封装了四个核心对象：

| Lua 类型 | C 结构体 | Metatable 名称 | 说明 |
|----------|----------|----------------|------|
| Environment | `wasm3_Environment` | `wasm3.environment` | 全局环境，可托管多个运行时 |
| Runtime | `wasm3_Runtime` | `wasm3.runtime` | 执行上下文，持有内存和栈 |
| Module | `wasm3_Module` | `wasm3.module` | 编译后的 WASM 模块 |
| Function | `wasm3_Function` | `wasm3.function` | 可调用的 WASM 函数 |

每个对象通过 `luaL_ref` 保存对父对象的引用，防止父对象被 GC 回收导致悬空指针。例如 `wasm3_Runtime` 持有 `env_ref`（对 Environment 的引用），`wasm3_Function` 持有 `runtime_ref`（对 Runtime 的引用）。

### 2.4 Lua API 用法

```lua
local wasm3 = require("wasm3")

-- ===== Environment =====
-- 创建环境（全局单例，可托管多个运行时）
local env = wasm3.newEnvironment()

-- 解析 WASM 二进制模块
local module = env:parseModule(wasm_bytes)

-- 创建运行时（可选参数：栈大小，默认 64KB）
local runtime = env:newRuntime(128 * 1024)

-- ===== Runtime =====
-- 加载模块到运行时（转移所有权，不可重复加载）
runtime:loadModule(module)

-- 查找导出函数
local func = runtime:findFunction("add")

-- 获取线性内存（返回字符串）
local mem = runtime:getMemory()

-- 获取内存大小（字节）
local size = runtime:getMemorySize()

-- 调试信息（仅 DEBUG 构建）
-- runtime:printInfo()

-- 获取回溯信息
local has_backtrace = runtime:getBacktrace()

-- ===== Module =====
-- 链接 WASI 支持（需编译时启用）
module:linkWASI()

-- 链接 LibC 支持
module:linkLibC()

-- 模块名称
local name = module:getName()
module:setName("my_module")

-- ===== Function =====
-- 调用函数（参数自动转换：number/string/boolean）
local result = func:call(arg1, arg2, ...)

-- 多返回值
local results = {func:call(42)}
```

### 2.5 函数调用机制

`function_call` 实现了自动类型转换：

1. **参数转换**：Lua 的 number → 字符串格式化（整数用 `%lld`，浮点用 `%f`），string → 直接传递，boolean → `"0"`/`"1"`
2. **调用**：通过 `m3_CallArgv` 以字符串数组形式传递参数
3. **返回值转换**：根据返回类型（`m3_GetRetType`）将结果解析为对应的 Lua 类型：
   - `c_m3Type_i32` → `lua_pushinteger`（int32_t）
   - `c_m3Type_i64` → `lua_pushinteger`（int64_t）
   - `c_m3Type_f32` → `lua_pushnumber`（float）
   - `c_m3Type_f64` → `lua_pushnumber`（double）

最大支持 128 个参数和 128 个返回值。

### 2.6 GC 管理

- `env_gc`：释放 `m3_FreeEnvironment`
- `runtime_gc`：释放 `m3_FreeRuntime`，同时 `luaL_unref` 对 Environment 的引用
- `module_gc`：仅对未加载的模块调用 `m3_FreeModule`（已加载的模块所有权已转移给运行时）
- `function_gc`：仅 `luaL_unref` 对 Runtime 的引用

---

## 3. wasmtime 集成

### 3.1 概述

wasmtime 是 Bytecode Alliance 开发的高性能 WASM JIT 运行时。LXCLUA-NCore 集成 wasmtime v45.0.1，**完整支持 WASM GC 提案**（包括 `anyref`、`structref`、`arrayref`、`externref`、`exnref`、`eqref`），这是运行 lua2wasm 编译产物的关键依赖。

### 3.2 预编译库

项目使用预编译的 wasmtime C API 库，位于 `wasmtime/` 目录：

| 平台 | 路径 | 库文件 |
|------|------|--------|
| Windows (x86_64, MinGW) | `wasmtime/wasmtime-v45.0.1-x86_64-mingw-c-api/` | `lib/libwasmtime.a` |
| Android (aarch64) | `wasmtime/wasmtime-v45.0.1-aarch64-android-c-api/` | `lib/libwasmtime.a`, `lib/libwasmtime.so` |

每个预编译目录包含：
- `include/` — C API 头文件（`wasmtime.h`, `wasm.h` 等）
- `lib/` — 静态库/动态库
- `min/` — 精简版（最小导出符号集）

### 3.3 核心源文件

| 文件 | 说明 |
|------|------|
| `src/wasm/lwasmtime.c` | wasmtime 的 Lua C 绑定，~3000+ 行，实现完整的 wasmtime C API 封装 |

### 3.4 模块架构

`lwasmtime.c` 封装了以下核心对象：

| Lua 对象 | C 类型 | 说明 |
|----------|--------|------|
| Engine | `wasm_engine_t*` | 编译引擎，线程安全，可跨 Store 共享 |
| Store | `wasmtime_store_t*` | 执行上下文，持有所有 WASM 对象 |
| Module | `wasmtime_module_t*` | 编译后的 WASM 模块 |
| Instance | `wasmtime_instance_t*` | 实例化后的模块 |
| Linker | `wasmtime_linker_t*` | 用于链接导入函数 |
| Func | `wasmtime_func_t` | 函数引用 |
| Memory | `wasmtime_memory_t` | 线性内存 |
| Global | `wasmtime_global_t` | 全局变量 |
| Table | `wasmtime_table_t` | 函数表 |
| SharedMemory | `wasmtime_sharedmemory_t` | 线程安全共享内存 |

### 3.5 lua2wasm Host 回调环境

`lwasmtime.c` 定义了一个完整的 `l2w_host_t` 宿主编译环境，包含 28 个 host 回调函数，用于桥接 WASM 模块与宿主系统：

| 回调分类 | 函数 | 说明 |
|----------|------|------|
| 输出 | `l2w_print_cb`, `l2w_write_raw_cb` | 捕获 print/write 输出到缓冲区 |
| 格式化 | `l2w_fmt_cb`, `l2w_fmt_spec_cb` | 字符串格式化（模拟 Lua 的 `string.format`） |
| 数学 | `l2w_math_cb`, `l2w_math2_cb` | 数学函数（floor, ceil, sqrt 等） |
| 输入 | `l2w_read_cb`, `l2w_read_num_cb` | io.read 支持 |
| 解析 | `l2w_parse_num_cb` | 字符串转数字 |
| 对象 | `l2w_obj_id_cb` | 对象 ID 生成 |
| 文件系统 | `l2w_fs_open_cb` 等 7 个 | 文件 I/O（open/read/write/seek/flush/close） |
| 系统 | `l2w_os_time_cb` 等 7 个 | os.time/date/clock/getenv/exit/remove/rename/tmpname |
| 警告 | `l2w_warn_cb`, `l2w_write_err_cb` | warn 和 stderr 输出 |

`l2w_host_t` 结构体提供：
- **输出捕获**：`output_buf` 动态增长缓冲区，捕获所有 print/write 输出
- **格式化缓冲区**：`fmt_buf`（16KB）线程本地格式化缓冲
- **文件表**：`files[64]` + `file_paths[64]`，支持最多 64 个打开文件
- **stdin 支持**：`stdin_data` / `stdin_pos` 提供可编程的 stdin 输入
- **冻结时间**：`frozen_time` 用于测试确定性
- **对象 ID 计数器**：`next_obj_id` 用于对象标识

**anyref 操作辅助函数**：通过回调 WASM 导出函数（`lua_tag`, `lua_get_int`, `lua_get_float`, `lua_get_bool`, `lua_str_len`, `lua_str_word`, `lua_make_int`, `lua_make_float` 等）实现 anyref 值的类型检查和转换。

### 3.6 Lua API 用法

```lua
local wasmtime = require("wasmtime")

-- ===== Engine =====
-- 默认引擎
local engine = wasmtime.newEngine()

-- 高级配置引擎
local engine = wasmtime.newEngine{
    optLevel             = "speed",       -- "none"/"speed"/"speedAndSize"
    parallelCompilation  = true,          -- 并行编译
    profiler             = "none",        -- "none"/"jitdump"/"vtune"/"perfmap"
    nanCanonicalization  = false,         -- NaN 规范化（确定性执行）
    nativeUnwind         = true,          -- 原生栈展开
    sharedMemory         = false,         -- 共享内存
    memoryMayMove        = false,         -- 内存可重定位
    memoryGuardSize      = 0,             -- 内存保护区（字节）
    maxWasmStack         = 0,             -- 最大 WASM 栈（字节）
    tailCall             = false,         -- 尾调用
}
engine:incrementEpoch()  -- 递增 epoch 计数器

-- ===== Store =====
local store = wasmtime.newStore(engine)

-- 燃料计量（用于限制执行）
store:setFuel(1000000)
local remaining = store:getFuel()

-- GC 控制
store:gc()

-- Epoch 截止（用于协作式中断）
store:setEpochDeadline(1)

-- 创建独立内存
local mem = store:newMemory(min_pages, max_pages)

-- ===== Module =====
-- 编译（验证 + 编译）
local module = wasmtime.newModule(engine, wasm_bytes)

-- 仅验证
local ok, err = wasmtime.validate(wasm_bytes)

-- 序列化（预编译缓存）
local cached = module:serialize()
local module2 = wasmtime.deserializeModule(engine, cached)

-- 查看导入/导出
local exports = module:getExports()
local imports = module:getImports()

-- ===== Instance =====
-- 直接实例化（无导入）
local instance = wasmtime.newInstance(store, module, {})

-- 通过 Linker 实例化（有导入）
local linker = wasmtime.newLinker(engine)
linker:defineFunc("env", "host_func", {"i32", "i32"}, {"i32"},
    function(caller, a, b)
        return a + b
    end)
local instance = linker:instantiate(store, module)

-- ===== 导出操作 =====
local func   = instance:getExport("main")
local mem    = instance:getMemory("memory")
local global = instance:getGlobal("counter")
local table  = instance:getTable("indirect")
local item, kind = instance:getExportEx("name")  -- kind: "func"/"memory"/"global"/"table"
local exports = instance:getExports()

-- ===== 函数操作 =====
local results = func:call(arg1, arg2, ...)
local params, rets = func:getType()  -- 返回参数类型列表和返回值类型列表

-- ===== 内存操作 =====
local data    = mem:read(offset, len)
local n       = mem:write(offset, str)
local pages   = mem:size()
local bytes   = mem:dataSize()
local old, ok = mem:grow(delta)
local min, max = mem:getType()

-- ===== 全局变量操作 =====
local val    = global:get()
local ok, err = global:set(val)

-- ===== 表操作 =====
local val    = table:get(idx)
local ok, err = table:set(idx, val)
local n      = table:size()
local old, ok = table:grow(delta, init_val)

-- ===== externref =====
local eref = wasmtime.newExternref(store, some_lua_data)

-- ===== 共享内存 =====
local shmem = wasmtime.newSharedMemory(engine, min_pages, max_pages)
local sz    = shmem:size()
local ptr   = shmem:data()  -- lightuserdata 指针

-- ===== lua2wasm 一键端到端 =====
local output = wasmtime.runLua2wasm(wasm_bytes)
```

### 3.7 lua2wasm 端到端流程

```lua
local lua2wasm = require("lua2wasm")
local wasmtime = require("wasmtime")

-- 编译 Lua 源码为 WASM 二进制
local wasm = lua2wasm.wcompile("print('hello from lua2wasm!')")

-- 一键运行
local output = wasmtime.runLua2wasm(wasm)
print(output)  --> "hello from lua2wasm!"
```

---

## 4. lua2wasm 编译器

### 4.1 概述

lua2wasm 是一个将 Lua 源码编译为 WebAssembly 的编译器，生成的 WASM 模块依赖 WASM GC 提案，因此需要 wasmtime 运行时执行。编译器以 Lua C 模块 (`require("lua2wasm")`) 的形式嵌入 LXCLUA-NCore，同时也提供独立的 CLI 工具。

### 4.2 编译管线

```
Lua 源码
    │
    ▼
┌──────────────┐
│  lexer.c     │  词法分析：将源码字符串转换为 Token 流
│  TokenList   │
└──────────────┘
    │
    ▼
┌──────────────┐
│  parser.c    │  语法分析：构建 AST（抽象语法树）
│  ast.c       │  NodePool 管理节点内存
│  ParseResult │  输出 Function 表、Global 表、主 Block
└──────────────┘
    │
    ▼
┌──────────────┐
│  codegen.c   │  代码生成：遍历 AST 生成 WAT 指令
│  builtins.c  │  内置函数（print, type, tonumber 等）
│  prelude.wat │  预置模块（类型定义、运行时结构体）
│  WatBuilder  │  WAT 文本缓冲区
└──────────────┘
    │
    ▼
┌──────────────┐
│ wat2wasm.c   │  WAT→WASM 汇编：将 WAT 文本转为 WASM 二进制
│  DCE 优化    │  死代码消除（默认开启）
└──────────────┘
    │
    ▼
  WASM 二进制
```

### 4.3 核心源文件

| 文件 | 说明 |
|------|------|
| `lexer.c` / `lexer.h` | 词法分析器，生成 TokenList |
| `parser.c` / `parser.h` | 语法分析器，生成 ParseResult（含 AST 和函数表） |
| `ast.c` / `ast.h` | AST 节点定义和 NodePool 内存管理 |
| `codegen.c` / `codegen.h` | 代码生成器，遍历 AST 输出 WAT |
| `builtins.c` / `builtins.h` | 内置函数定义（print, type, tonumber, tostring 等） |
| `wat_builder.c` / `wat_builder.h` | WAT 文本构建器（动态增长缓冲区） |
| `wat2wasm.c` / `wat2wasm.h` | WAT→WASM 二进制汇编器（自包含，无外部依赖） |
| `xalloc.c` / `xalloc.h` | 内存分配包装（带 OOM 检查） |
| `prelude.wat` | 预置 WAT 模块（运行时类型定义、表结构、闭包等） |
| `lua2wasmlib.c` | Lua 模块入口（`require("lua2wasm")`） |
| `main.c` | lua2wasm CLI 工具入口 |
| `wat2wasm_cli.c` | wat2wasm CLI 工具入口 |
| `emscripten_entry.c` | Emscripten/浏览器端编译入口 |

### 4.4 prelude.wat — 预置运行时

`prelude.wat` 定义了 WASM GC 类型系统，是 lua2wasm 编译产物的运行时基础：

| 类型 | 定义 | 说明 |
|------|------|------|
| `$LuaArr` | `(array (mut i8))` | 字节数组（字符串存储） |
| `$LuaString` | `(struct (field $bytes (ref $LuaArr)))` | 字符串类型 |
| `$LuaFloat` | `(struct (field $v f64))` | 浮点数类型 |
| `$LuaInt` | `(struct (field $v i64))` | 整数类型 |
| `$LuaBool` | `(struct (field $b i32))` | 布尔类型 |
| `$LuaClosure` | `(struct (field $code ...) (field $upvals ...))` | 闭包（函数 + 上值） |
| `$LuaTable` | `(struct (field $keys ...) (field $vals ...) (field $idx ...))` | Lua 表（键值对数组 + 哈希索引） |
| `$Box` | `(struct (field $v (mut anyref)))` | 可变引用盒（用于上值） |
| `$ArgArr` | `(array (mut anyref))` | 参数数组 |
| `$UpvalArr` | `(array (mut (ref $Box)))` | 上值数组 |
| `$Tbc` | `(struct (field $items ...) (field $len ...))` | to-be-closed 变量栈 |
| `$CapArr` | `(array (mut i32))` | 模式匹配捕获缓冲区 |
| `$LineArr` | `(array (mut i32))` | 调用帧行号栈 |
| `$Builder` | `(struct (field $arr ...) (field $len ...))` | 可变字节缓冲区（string.gsub） |

### 4.5 内置函数（builtins.c）

编译器内置了 Lua 标准库的核心函数实现，直接编译为 WASM 指令，不依赖宿主环境：

- **类型操作**：`type`, `tonumber`, `tostring`, `rawequal`, `rawget`, `rawset`, `rawlen`
- **输出**：`print`
- **表操作**：`next`, `pairs`, `ipairs`, `setmetatable`, `getmetatable`
- **字符串**：`string.format`, `string.sub`, `string.len`, `string.byte`, `string.char` 等
- **数学**：`math.abs`, `math.floor`, `math.ceil`, `math.sqrt`, `math.max`, `math.min` 等
- **错误处理**：`error`, `pcall`, `xpcall`, `assert`
- **其他**：`select`, `tostring`, `tonumber`

### 4.6 wat2wasm 汇编器

`wat2wasm.c` 是一个**自包含的 WAT→WASM 二进制汇编器**，仅依赖 C 标准库。它专门针对 lua2wasm 输出的 WAT 子集（WasmGC + 类型化函数引用 + 异常处理），不支持通用 WAT 的所有构造。

**关键特性：**
- **死代码消除（DCE）**：当 `dce=1` 时，从模块导出和全局初始化器出发，沿调用/ref.func 边追踪可达函数，删除不可达函数体，减小输出体积
- 零外部依赖，可独立链接为 CLI 工具或嵌入编译器

### 4.7 Emscripten 支持

`emscripten_entry.c` 提供浏览器端编译入口，仅在 `__EMSCRIPTEN__` 宏定义时编译：

| 导出函数 | 说明 |
|----------|------|
| `lua2wasm_compile(source)` | 编译 Lua 源码为 WAT 文本 |
| `lua2wasm_compile_ex(source, tree_shake)` | 编译（可选 tree-shaking） |
| `lua2wasm_assemble(wat, out_len, err, errcap)` | WAT 汇编为 WASM 二进制 |
| `lua2wasm_free(p)` | 释放编译结果内存 |

`tree_shake` 参数：开启后仅输出 AST 实际引用的内置函数和 `_G` 条目，让 wasm-opt 可以 DCE 未使用的函数体。默认关闭，因为 `_G.foo` 自省需要所有内置函数。

### 4.8 Lua API 用法

```lua
local lua2wasm = require("lua2wasm")

-- 编译 Lua 源码为 WAT 文本
local wat = lua2wasm.compile([[
    local function add(a, b)
        return a + b
    end
    return add(1, 2)
]])

-- 直接编译为 WASM 二进制（compile + assemble 一步完成）
local wasm = lua2wasm.wcompile("return 1 + 2")

-- 将 WAT 文本汇编为 WASM 二进制
local wasm = lua2wasm.assemble(wat)

-- 关闭 DCE 优化
local wasm = lua2wasm.assemble(wat, true)  -- 第二个参数 true = 不执行 DCE
```

### 4.9 CLI 工具

```bash
# lua2wasm CLI：将 .lua 编译为 .wat / .wasm
make lua2wasm
./lua2wasm input.lua            # 输出 WAT 到 stdout
./lua2wasm input.lua -o out.wat # 输出到文件
./lua2wasm input.lua --wasm     # 输出 WASM 二进制

# wat2wasm CLI：WAT 文本转 WASM 二进制
make wat2wasm
./wat2wasm input.wat -o output.wasm
```

---

## 5. lxclua_wasm.c — Lua C API 的 WASM 导出

### 5.1 概述

`lxclua_wasm.c` 将 Lua C API 封装为可导出的 WASM 函数，使 Lua 引擎能够被编译为 WASM 模块，供 wasm3 等其他 WASM 运行时加载和调用。这在 "Lua in WASM" 场景中非常有用——例如在浏览器中运行 Lua。

### 5.2 导出策略

```c
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define EXPORT
#endif
```

在 Emscripten 编译时使用 `EMSCRIPTEN_KEEPALIVE` 防止函数被优化删除，在原生编译时无额外标记。

### 5.3 API 分类

| 分类 | 函数数量 | 说明 |
|------|----------|------|
| **State 管理** | 3 | `newstate`, `close`, `openlibs` |
| **执行** | 4 | `dostring`, `loadstring`, `dofile`, `loadfile` |
| **栈操作** | 8 | `gettop`, `settop`, `pop`, `pushvalue`, `remove`, `insert`, `replace`, `checkstack` |
| **类型检查** | 10 | `type`, `typename`, `isnil`, `isboolean`, `isnumber`, `isstring`, `istable`, `isfunction`, `isuserdata`, `isthread`, `islightuserdata` |
| **获取值** | 8 | `tonumber`, `tointeger`, `toboolean`, `tostring`, `tolstring`, `rawlen`, `touserdata`, `tothread`, `topointer` |
| **压入值** | 7 | `pushnil`, `pushnumber`, `pushinteger`, `pushboolean`, `pushstring`, `pushlstring`, `pushlightuserdata` |
| **表操作** | 14 | `createtable`, `newtable`, `getglobal`, `setglobal`, `getfield`, `setfield`, `gettable`, `settable`, `rawget`, `rawgeti`, `rawset`, `rawseti`, `setmetatable`, `getmetatable`, `next`, `len` |
| **调用函数** | 3 | `pcall`, `call`, `pcallk` |
| **错误处理** | 2 | `error`, `errorstring` |
| **GC 控制** | 3 | `gc`, `collectgarbage`, `memusage` |
| **引用系统** | 2 | `ref`, `unref` |
| **比较** | 3 | `compare`, `equal`, `lessthan`, `rawequal` |
| **高级 API** | 8 | `eval`, `eval_number`, `eval_integer`, `call_global_number`, `call_global_string`, `setglobal_number`, `setglobal_integer`, `setglobal_string`, `getglobal_number`, `getglobal_integer`, `getglobal_string` |
| **内存** | 3 | `malloc`, `free`, `realloc` |

### 5.4 高级 API 示例

```c
// 执行代码并返回字符串结果
const char* result = lua_wasm_eval(L, "return 'hello' .. ' world'");

// 执行代码并返回数字
lua_Number n = lua_wasm_eval_number(L, "return 1 + 2");

// 调用全局函数
lua_Number v = lua_wasm_call_global_number(L, "my_func");

// 设置/获取全局变量
lua_wasm_setglobal_string(L, "name", "world");
const char* name = lua_wasm_getglobal_string(L, "name");
```

---

## 6. 编译配置

### 6.1 Makefile 中的 WASM 相关变量

```makefile
# wasmtime 预编译库路径
WASMTIME_DIR = wasmtime/wasmtime-v45.0.1-x86_64-mingw-c-api
WASMTIME_INC = -I$(WASMTIME_DIR)/include
WASMTIME_LIB = $(WASMTIME_DIR)/lib/libwasmtime.a -lbcrypt -luserenv -lole32 -lntdll

# wasm3 核心源文件
WASM3_O = m3_api_libc.o m3_api_meta_wasi.o m3_api_tracer.o m3_api_uvwasi.o \
          m3_api_wasi.o m3_bind.o m3_code.o m3_compile.o m3_core.o m3_env.o \
          m3_exec.o m3_function.o m3_info.o m3_module.o m3_parse.o

# lua2wasm 编译器核心
LUA2WASM_CORE_O = ast.o lexer_l2w.o parser_l2w.o wat_builder.o codegen_l2w.o \
                  builtins_l2w.o xalloc_l2w.o

# WAT→WASM 汇编器
WAT2WASM_CORE_O = wat2wasm_core.o

# Lua 模块入口
LUA2WASM_LIB_O = lua2wasmlib.o

# WASM 运行时绑定（wasm3 + wasmtime）
LIB_O_WASM = lwasm3.o lwasmtime.o $(WASM3_O)

# 基础对象（桌面/原生构建，含 wasmtime）
BASE_O = $(CORE_O) $(LIB_O) $(LIB_O_WASM) $(QJS_O) $(MYOBJS) \
         $(LUA2WASM_CORE_O) $(WAT2WASM_CORE_O) $(LUA2WASM_LIB_O) $(PCRE2_O)
```

### 6.2 编译目标

#### `make wasm` — Emscripten/WASM 构建

将整个 LXCLUA-NCore 编译为 WebAssembly 模块（`.js` + `.wasm`），在浏览器中运行。

```makefile
wasm:
    # 使用 Emscripten 编译器
    CC = emcc -std=c23
    # 编译选项
    CFLAGS = -O3 -DNDEBUG -fno-exceptions -DLUA_32BITS=0
    SYSCFLAGS = -DLUA_USE_LONGJMP -DLUA_COMPAT_MATHLIB -DLUA_COMPAT_MAXN -DLUA_NOJIT
    # 链接选项
    LDFLAGS = -sWASM=1 -sSINGLE_FILE=1 \
              -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,callMain,FS \
              -sMODULARIZE=1 -sALLOW_MEMORY_GROWTH=1 \
              -sFILESYSTEM=1 -sINVOKE_RUN=0 \
              -sSTACK_SIZE=5MB -sINITIAL_MEMORY=32MB
```

**关键注意事项：**
- 不含 wasmtime（`WASMTIME_INC=`、`WASMTIME_LIB=` 置空），仅含 wasm3
- 禁用 JIT（`LUA_NOJIT`）
- 生成三个模块：`lxclua.js`（LuaModule）、`luac.js`（LuacModule）、`luaccheck.js`（LuaccheckModule）
- 使用 `SINGLE_FILE=1` 将 WASM 内嵌到 JS 中
- 使用 `MODULARIZE=1` 生成 ES 模块

#### `make wasmlsp` — LSP 服务器的 WASM 构建

将 LSP 服务器编译为 WASM 模块，不含 wasmtime 运行时，仅链接基础数学库。

```makefile
wasmlsp:
    # 输出 lxclua-lsp.js
    LDFLAGS = -sWASM=1 -sSINGLE_FILE=1 \
              -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,callMain,FS \
              -sMODULARIZE=1 -sEXPORT_NAME=LuaLSPModule \
              -sALLOW_MEMORY_GROWTH=1 -sFILESYSTEM=1 \
              -sINVOKE_RUN=0 -sSTACK_SIZE=5MB -sINITIAL_MEMORY=32MB
```

#### `make wasm-c` / `make wasm-c-all` — 将任意 C 代码编译为 WASM

将用户 C 源文件编译为独立的 WASM 模块，供 wasm3 加载。

```bash
# 编译指定 C 文件为 WASM 模块
make wasm-c SRC=test.c

# 指定输出文件名和导出函数
make wasm-c SRC=test.c OUT=mylib.wasm EXPORTS="_add,_mul"

# 导出所有函数
make wasm-c-all SRC=test.c
```

编译选项：
```makefile
WASM_CFLAGS  = -O3 -DNDEBUG
WASM_LDFLAGS = -sWASM=1 -sSTANDALONE_WASM=1 -sALLOW_MEMORY_GROWTH=1 --no-entry
```

#### `make lua2wasm` — lua2wasm CLI 工具

```bash
make lua2wasm
# 生成 lua2wasm.exe（独立命令行工具）
```

#### `make wat2wasm` — wat2wasm CLI 工具

```bash
make wat2wasm
# 生成 wat2wasm.exe（WAT→WASM 汇编器）
```

### 6.3 平台特定配置

```makefile
# Linux x86_64
WASMTIME_DIR = wasmtime/wasmtime-v45.0.1-x86_64-linux-c-api
WASMTIME_LIB = wasmtime/wasmtime-v45.0.1-x86_64-linux-c-api/lib/libwasmtime.a

# Android aarch64
WASMTIME_DIR = wasmtime/wasmtime-v45.0.1-aarch64-android-c-api
WASMTIME_LIB = wasmtime/wasmtime-v45.0.1-aarch64-android-c-api/lib/libwasmtime.a
```

### 6.4 对象文件命名策略

为避免 lua2wasm 与 lxclua 核心的同名文件冲突，编译器模块使用显式后缀命名：

| 源文件 | 对象文件 |
|--------|----------|
| `src/lua2wasm/lexer.c` | `build/obj/lexer_l2w.o` |
| `src/lua2wasm/parser.c` | `build/obj/parser_l2w.o` |
| `src/lua2wasm/codegen.c` | `build/obj/codegen_l2w.o` |
| `src/lua2wasm/builtins.c` | `build/obj/builtins_l2w.o` |
| `src/lua2wasm/xalloc.c` | `build/obj/xalloc_l2w.o` |
| `src/lua2wasm/wat2wasm.c` | `build/obj/wat2wasm_core.o` |

---

## 7. 典型使用场景

### 7.1 在 Lua 中加载和执行 WASM 模块（wasm3）

```lua
local wasm3 = require("wasm3")

-- 创建环境
local env = wasm3.newEnvironment()
local runtime = env:newRuntime(256 * 1024)

-- 解析并加载 WASM 模块
local wasm_bytes = io.open("my_module.wasm", "rb"):read("*a")
local module = env:parseModule(wasm_bytes)
runtime:loadModule(module)

-- 调用函数
local add = runtime:findFunction("add")
local result = add:call(3, 4)
print(result)  --> 7
```

### 7.2 将 Lua 编译为 WASM 并用 wasmtime 执行

```lua
local lua2wasm = require("lua2wasm")
local wasmtime = require("wasmtime")

-- 编译 Lua 源码
local wasm = lua2wasm.wcompile([[
    local t = {}
    for i = 1, 10 do
        t[i] = i * i
    end
    return table.concat(t, ", ")
]])

-- 一键运行
local result = wasmtime.runLua2wasm(wasm)
print(result)  --> "1, 4, 9, 16, 25, 36, 49, 64, 81, 100"
```

### 7.3 在浏览器中运行 Lua（Emscripten）

```html
<script src="lxclua.js"></script>
<script>
LuaModule().then(function(Module) {
    // 通过 ccall 调用 Lua
    var result = Module.ccall('lua_wasm_eval', 'string', ['string'],
        ['return "hello from wasm!"']);
    console.log(result);
});
</script>
```

### 7.4 将 C 函数编译为 WASM 供 Lua 调用

```bash
# 编译 C 文件为 WASM
make wasm-c SRC=my_lib.c OUT=my_lib.wasm EXPORTS="_my_func"
```

```lua
local wasm3 = require("wasm3")
local env = wasm3.newEnvironment()
local runtime = env:newRuntime()
local module = env:parseModule(io.open("my_lib.wasm", "rb"):read("*a"))
runtime:loadModule(module)
local fn = runtime:findFunction("my_func")
print(fn:call(42))
```

---

## 8. 文件清单

```
src/wasm/
├── lwasm3.c                    # wasm3 Lua 绑定
├── lwasmtime.c                 # wasmtime Lua 绑定
├── lxclua_wasm.c               # Lua C API→WASM 导出
├── wasm3.h                     # wasm3 公共 API
├── wasm3_defs.h                # wasm3 类型定义
├── m3_*.c (15 files)           # wasm3 核心引擎
├── m3_*.h (16 files)           # wasm3 内部头文件
├── m3_api_wasi.c/.h            # WASI 实现
├── m3_api_libc.c/.h            # LibC 桥接
├── m3_api_uvwasi.c             # uvwasi 实现
├── m3_api_meta_wasi.c          # WASI 元接口
└── m3_api_tracer.c/.h          # API 追踪

src/lua2wasm/
├── lexer.c / lexer.h           # 词法分析
├── parser.c / parser.h         # 语法分析
├── ast.c / ast.h               # AST 节点
├── codegen.c / codegen.h       # 代码生成
├── builtins.c / builtins.h     # 内置函数
├── wat_builder.c / wat_builder.h  # WAT 构建器
├── wat2wasm.c / wat2wasm.h     # WAT→WASM 汇编器
├── xalloc.c / xalloc.h         # 内存分配
├── prelude.wat                 # 预置运行时类型
├── lua2wasmlib.c               # Lua 模块入口
├── main.c                      # CLI 工具
├── wat2wasm_cli.c              # wat2wasm CLI
└── emscripten_entry.c          # Emscripten 入口

wasmtime/
├── wasmtime-v45.0.1-x86_64-mingw-c-api/   # Windows MinGW 预编译
├── wasmtime-v45.0.1-x86_64-windows-c-api/ # Windows MSVC 预编译
├── wasmtime-v45.0.1-aarch64-android-c-api/# Android aarch64 预编译
└── Android.mk                              # Android NDK 构建
```