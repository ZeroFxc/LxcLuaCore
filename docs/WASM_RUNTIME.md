# LXCLUA-NCore WASM 运行时集成文档

LXCLUA-NCore 集成了完整的 WebAssembly 运行时支持，包括两个 WASM 运行时（wasm3 和 wasmtime），以及将 Lua C API 导出为 WASM 模块的包装层。

---

## 1. 架构概览

```
┌─────────────────────────────────────────────────────────────────┐
│                        Lua 代码层                                │
│  require("wasm3")    require("wasmtime")                        │
├─────────────────────────────────────────────────────────────────┤
│   lwasm3.c          lwasmtime.c                                  │
│   (Lua 绑定)        (Lua 绑定)                                   │
├─────────────────────────────────────────────────────────────────┤
│   wasm3 (v0.5.1)    wasmtime (v48.0.1)                          │
│   解释器引擎         JIT 运行时                                   │
│   m3_*.c 核心       支持 GC 提案                                  │
├─────────────────────────────────────────────────────────────────┤
│   lxclua_wasm.c                                                 │
│   Lua C API → WASM 导出（供 wasm3 嵌入 Lua 引擎）               │
└─────────────────────────────────────────────────────────────────┘
```

**两种运行时对比：**

| 特性 | wasm3 | wasmtime |
|------|-------|----------|
| 类型 | 解释器 | JIT 编译器 |
| WASM GC 提案 | 不支持 | 支持（v48.0.1） |
| 体积 | 极小（纯 C） | 较大（预编译库） |
| 适用场景 | 嵌入式、WASM 自身编译 | 高性能 JIT、WASM GC 模块 |
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

wasmtime 是 Bytecode Alliance 开发的高性能 WASM JIT 运行时。LXCLUA-NCore 集成 wasmtime v48.0.1，**完整支持 WASM GC 提案**（包括 `anyref`、`structref`、`arrayref`、`externref`、`exnref`、`eqref`），

### 3.2 预编译库

项目使用预编译的 wasmtime C API 库，位于 `wasmtime/` 目录：

| 平台 | 路径 | 库文件 |
|------|------|--------|
| Windows (x86_64, MinGW) | `wasmtime/wasmtime-v48.0.1-x86_64-mingw-c-api/` | `lib/libwasmtime.a` |
| Android (aarch64) | `wasmtime/wasmtime-v48.0.1-aarch64-android-c-api/` | `lib/libwasmtime.a`, `lib/libwasmtime.so` |

每个预编译目录包含：
- `include/` — C API 头文件（`wasmtime.h`, `wasm.h` 等）
- `lib/` — 静态库/动态库
- `min/` — 精简版（最小导出符号集）

### 3.3 核心源文件

| 文件 | 说明 |
|------|------|
| `src/wasm/lwasmtime.c` | wasmtime 的 Lua C 绑定，实现完整的 wasmtime C API 封装 |

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

```


 — Lua C API 的 WASM 导出

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
WASMTIME_DIR = wasmtime/wasmtime-v48.0.1-x86_64-mingw-c-api
WASMTIME_INC = -I$(WASMTIME_DIR)/include
WASMTIME_LIB = $(WASMTIME_DIR)/lib/libwasmtime.a -lbcrypt -luserenv -lole32 -lntdll

# wasm3 核心源文件
WASM3_O = m3_api_libc.o m3_api_meta_wasi.o m3_api_tracer.o m3_api_uvwasi.o \
          m3_api_wasi.o m3_bind.o m3_code.o m3_compile.o m3_core.o m3_env.o \
          m3_exec.o m3_function.o m3_info.o m3_module.o m3_parse.o


# WASM 运行时绑定（wasm3 + wasmtime）
LIB_O_WASM = lwasm3.o lwasmtime.o $(WASM3_O)

# 基础对象（桌面/原生构建，含 wasmtime）
BASE_O = $(CORE_O) $(LIB_O) $(LIB_O_WASM) $(QJS_O) $(MYOBJS) $(PCRE2_O)
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



```makefile
# Linux x86_64
WASMTIME_DIR = wasmtime/wasmtime-v48.0.1-x86_64-linux-c-api
WASMTIME_LIB = wasmtime/wasmtime-v48.0.1-x86_64-linux-c-api/lib/libwasmtime.a

# Android aarch64
WASMTIME_DIR = wasmtime/wasmtime-v48.0.1-aarch64-android-c-api
WASMTIME_LIB = wasmtime/wasmtime-v48.0.1-aarch64-android-c-api/lib/libwasmtime.a
```



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

（Emscripten）

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

├── wasmtime-v48.0.1-x86_64-mingw-c-api/   # Windows MinGW 预编译
├── wasmtime-v48.0.1-x86_64-windows-c-api/ # Windows MSVC 预编译
├── wasmtime-v48.0.1-aarch64-android-c-api/# Android aarch64 预编译
└── Android.mk                              # Android NDK 构建
```