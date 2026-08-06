# LXCLUA-NCore 贡献指南

[English](#english) | [中文](#中文)

---

## English

Thank you for your interest in contributing to LXCLUA-NCore! This document provides comprehensive guidelines for contributing to the project.

### Project Architecture Overview

Before contributing, please familiarize yourself with the project's five-layer architecture:

```
┌─────────────────────────────────────────────────────────────┐
│                    Application Layer                         │
│  src/bin/ (lua, luac, luaccheck) ─ Entry points             │
├─────────────────────────────────────────────────────────────┤
│                   Extension Layer                            │
│  src/wasm/ src/lua2wasm/ src/lspsrv/ ─ External integrations│
├─────────────────────────────────────────────────────────────┤
│                  Standard Library Layer                      │
│  src/stdlib/ (lclass, laio, lthread, lstruct) ─ Libraries   │
├─────────────────────────────────────────────────────────────┤
│                  Compiler Layer                              │
│  src/compiler/ (llex, lparser, last, lcodegen) ─ Frontend   │
├─────────────────────────────────────────────────────────────┤
│                  Core Runtime Layer                          │
│  src/core/ (lapi, lvm, lgc, lobject, lstate) ─ Foundation    │
└─────────────────────────────────────────────────────────────┘
```

See `docs/ARCHITECTURE.md` and `docs/DEEP_DIVE.md` for detailed technical information.

### How to Contribute

#### Reporting Bugs

1. Search existing Issues to avoid duplicates
2. Create a new issue with:
   - **Clear title** describing the problem
   - **Minimal reproduction code** that triggers the bug
   - **Expected vs actual behavior**
   - **Environment details**:
     - OS: Windows / Linux / macOS / Android / Other
     - Compiler: GCC version, Clang version, or MSVC version
     - Build flags used (especially optimization level)
   - **Stack trace** if applicable (use `lua_gdb.py` or `lua_lldb.py` helpers)
3. Tag the issue appropriately: `bug`, `regression`, `crash`, etc.

#### Suggesting Features

1. Open an issue with the `enhancement` label
2. Describe the feature and its use case with concrete examples
3. For language syntax changes, provide:
   - Proposed syntax with examples
   - Backward compatibility analysis
   - Implementation approach (which compiler stages need modification)
4. For library additions, specify:
   - API surface (function signatures)
   - Platform requirements
   - Performance characteristics

#### Submitting Code

1. Fork the repository
2. Create a feature branch: `git checkout -b feature/your-feature` or `fix/bug-description`
3. Make your changes following the code style guidelines
4. Compile with warnings enabled:
   ```bash
   make clean
   make CFLAGS="-Wall -Wextra -Werror -std=c23 -O2"
   ```
5. Run existing tests to verify no regressions
6. Add test cases for new functionality
7. Commit with clear, descriptive messages following the convention:
   ```
   <module>: <verb> <description>
   
   <optional detailed explanation>
   
   Examples:
   - lparser: add support for `unless` keyword
   - lvm: fix off-by-one in FORPREP instruction
   - lclass: implement trait conflict detection
   - docs: update API reference for crypto module
   ```
8. Push and create a Pull Request with:
   - Link to related Issue (if applicable)
   - Summary of changes
   - Testing performed
   - Performance impact notes (if relevant)

### Code Style Guidelines

#### C Code Style

The project follows a consistent style derived from Lua's own codebase conventions:

**Indentation:**
- Use 2 spaces (no tabs)
- Maximum line length: 120 characters
- Align continuation lines with the opening delimiter

**Naming:**
```c
/* Types: PascalCase with _t suffix */
typedef struct { ... } MyStruct;
typedef enum { ... } MyEnum;

/* Variables: snake_case */
int local_variable;
lua_State *L;  /* Conventional name for Lua state */

/* Functions: module prefix + snake_case */
int luaVM_execute(lua_State *L);
void obfuscate_apply(Proto *f, int flags);

/* Constants: UPPER_SNAKECASE */
#define MAX_BUFFER_SIZE 4096
#define IS_LIKELY(x) __builtin_expect(!!(x), 1)

/* Macros that act like functions: lowercase */
#define checknelems(L, n) ...
```

**Header file structure:**
```c
/**
 * @file filename.h
 * @brief One-line description of the file's purpose.
 * 
 * Longer description if needed (multiple paragraphs allowed).
 */

#ifndef filename_h
#define filename_h

/* Standard headers */
#include <stdlib.h>
#include "lua.h"       /* Project headers after standard */

/* Public types */
typedef struct { ... } PublicType;

/* Public constants */
#define PUBLIC_CONSTANT 42

/* Public API function declarations */
int public_function(lua_State *L);

#endif /* filename_h */
```

**Function comments (Doxygen-style):**
```c
/**
 * @brief Executes the VM main loop for a given number of instructions.
 * 
 * @param L      The Lua state.
 * @param nexecls  Number of instructions to execute (0 = until return/yield).
 * @return       None (longjmps on error).
 * 
 * @note This function uses computed goto for dispatch and must not be called
 *       recursively. For nested execution, use lua_pcall instead.
 */
static void execute(lua_State *L, int nexecls);
```

**Complex algorithm comments:**
```c
/* === C3 Linearization Algorithm ===
 * Goal: Compute Method Resolution Order (MRO) for multiple inheritance.
 * 
 * Input: A class C with parents [C1, C2, ..., Cn]
 * Output: A linear list [C, ...] satisfying:
 *   1. C appears first
 *   2. Parents preserve declaration order
 *   3. Local precedence order is consistent
 * 
 * Algorithm: merge(L[C1], ..., L[Cn], [C1, ..., Cn]) recursively
 */
```

#### Memory Management Rules

1. **Ownership is explicit**: Every `malloc`/`calloc` must have exactly one `free` path
2. **Use `luaM_*` for GC-managed allocations**: Never raw `malloc` for Lua objects
3. **Match alloc/free at the same abstraction level**: If a function allocates, the caller frees (or it's documented as "transfer of ownership")
4. **No memory leaks on error paths**: Use `luaD_rawrunprotected` for protected allocations
5. **Stack balance**: Every push has a corresponding pop or documented permanent storage

#### Platform Portability

1. **No platform-specific code without `#ifdef`**:
   ```c
   #ifdef _WIN32
   #include <windows.h>
   /* Windows-specific code */
   #else
   #include <unistd.h>
   /* POSIX code */
   #endif
   ```

2. **Supported platform detection macros**:
   - `_WIN32` — Windows (32-bit and 64-bit)
   - `__linux__` — Linux
   - `__APPLE__` — macOS / iOS
   - `__ANDROID__` — Android NDK
   - `__EMSCRIPTEN__` — WebAssembly via Emscripten
   - `__TERMUX__` — Android Termux environment

3. **Byte order and alignment**: Do not assume little-endian or specific struct packing. Use `memcpy` for cross-platform serialization.

4. **Integer sizes**: Use `int32_t`, `int64_t`, etc. from `<stdint.h>`. Never assume `int` size.

#### Documentation Requirements

For new features, the following documentation MUST be updated:

1. **`docs/SYNTAX_REFERENCE.md`** — For new syntax features
2. **`docs/API_REFERENCE.md`** — For new public C API or Lua library functions
3. **`docs/DEEP_DIVE.md`** — For significant architectural changes
4. **`docs/TUTORIAL.md`** — User-facing examples for major features
5. **Code comments** — Doxygen-style for all public APIs

### Build System

#### Build Targets

| Target | Description |
|--------|-------------|
| `make` or `make all` | Build lua, luac, and all bundled libraries |
| `make lua` | Build only the standalone interpreter |
| `make luac` | Build only the bytecode compiler |
| `make lib` | Build static library `liblxclua.a` |
| `make lspsrv` | Build the Language Server |
| `make check` / `make test` | Run the test suite |

#### Build Configuration

Key `Makefile` variables (override on command line):

```bash
# Compiler selection
make CC=clang
make CC=gcc-13

# Build type
make BUILD=debug      # -O0 -g -DDEBUG
make BUILD=release    # -O2 -DNDEBUG
make BUILD=sanitize   # -O1 -fsanitize=address,undefined

# Platform
make PLATFORM=android   # Android cross-compile
make PLATFORM=wasm      # Emscripten build

# Feature selection
make WITH_WASM=0        # Disable wasmtime module
make WITH_WASM3=1       # Enable wasm3 module (optional)
```

#### Adding New Source Files

When adding a new `.c` file:

1. Add it to the appropriate `_SRC` variable in `Makefile`:
   ```makefile
   # For src/utils/newfile.c:
   UTILS_SRC += src/utils/newfile.c
   ```

2. If it's a new Lua library module, also add it to `linit.c`:
   ```c
   static const luaL_Reg loadedlibs[] = {
       /* ... existing libs ... */
       {NEWLIBNAME, luaopen_newlib},
   };
   
   LUAMOD_API int luaopen_newlib(lua_State *L);
   ```

3. Add corresponding `.h` file to the appropriate include path in `Makefile`

### Testing

#### Running Tests

```bash
# Full test suite
make test

# Specific module test
make test VM=1        # VM-specific tests
make test COMPILER=1  # Compiler tests
make test OBFUSCATE=1 # Obfuscation tests

# Manual testing with the interpreter
./lua -e "print('Hello from LXCLUA!')"
./lua test.lua
```

#### Writing Tests

Test files go in `tests/` directory with naming convention:

```
tests/
├── vm/           # VM instruction tests
├── compiler/     # Parser and code generation tests
├── stdlib/       # Standard library tests
├── utils/       # Utility function tests
└── integration/ # End-to-end integration tests
```

Test file format:
```lua
-- tests/vm/test_for_loop.lua
-- Tests numeric and generic for loop execution

local function test_numeric_for()
  local sum = 0
  for i = 1, 10 do
    sum = sum + i
  end
  assert(sum == 55, "Numeric for failed: " .. sum)
end

local function test_generic_for()
  local t = {1, 2, 3, 4, 5}
  local sum = 0
  for _, v in ipairs(t) do
    sum = sum + v
  end
  assert(sum == 15, "Generic for failed: " .. sum)
end

test_numeric_for()
test_generic_for()
print("All for-loop tests passed")
```

#### Performance Benchmarks

When making changes to performance-critical code (VM loop, GC, compiler):

1. Run benchmarks before and after the change:
   ```bash
   make benchmark > before.txt
   # ... make changes ...
   make benchmark > after.txt
   diff before.txt after.txt
   ```

2. Key benchmark areas:
   - VM instruction throughput (instructions/second)
   - GC pause times
   - Compilation speed (lines/second)
   - Function call overhead

3. Document any performance regression (> 5%) and justify

### Release Process

1. Version format: `MAJOR.MINOR.PATCH` (Semantic Versioning)
2. Backward-incompatible changes require major version bump
3. New features require minor version bump
4. Bug fixes require patch version bump
5. All releases must:
   - Pass full test suite
   - Build on all supported platforms
   - Update `CHANGELOG.md`
   - Update version string in `lua.h`

### Getting Help

- Technical discussions: Open a Discussion on GitHub
- Quick questions: Issue with `question` label
- Code review: Ping `@DifierLine` in PRs

---

## 中文

感谢您对 LXCLUA-NCore 项目的关注！本文档提供详细的贡献指南。

### 项目架构概览

贡献前请先了解项目的五层架构：

```
┌─────────────────────────────────────────────────────────────┐
│                      应用层                                  │
│  src/bin/ (lua, luac, luaccheck) ─ 入口程序                  │
├─────────────────────────────────────────────────────────────┤
│                     扩展层                                   │
│  src/wasm/ src/lua2wasm/ src/lspsrv/ ─ 外部集成              │
├─────────────────────────────────────────────────────────────┤
│                    标准库层                                  │
│  src/stdlib/ (lclass, laio, lthread, lstruct) ─ 库实现       │
├─────────────────────────────────────────────────────────────┤
│                     编译器层                                 │
│  src/compiler/ (llex, lparser, last, lcodegen) ─ 前端        │
├─────────────────────────────────────────────────────────────┤
│                    核心运行时层                               │
│  src/core/ (lapi, lvm, lgc, lobject, lstate) ─ 基础设施      │
└─────────────────────────────────────────────────────────────┘
```

详细技术信息参见 `docs/ARCHITECTURE.md` 和 `docs/DEEP_DIVE.md`。

### 如何贡献

#### 报告 Bug

1. 先搜索已有 Issue，避免重复提交
2. 创建新 Issue，包含以下内容：
   - **清晰的标题**描述问题
   - **最小可重现代码**
   - **预期行为与实际行为**
   - **环境信息**：
     - 操作系统：Windows / Linux / macOS / Android / 其他
     - 编译器：GCC 版本、Clang 版本 或 MSVC 版本
     - 使用的构建标志（尤其是优化级别）
   - **堆栈跟踪**（如适用，使用 `lua_gdb.py` 或 `lua_lldb.py` 辅助）
3. 使用合适的标签：`bug`、`regression`、`crash` 等

#### 功能建议

1. 创建带 `enhancement` 标签的 Issue
2. 用具体示例描述功能和使用场景
3. 对于语言语法变更，提供：
   - 建议语法及示例
   - 向后兼容性分析
   - 实现方案（需要修改哪些编译器阶段）
4. 对于库新增，说明：
   - API 接口（函数签名）
   - 平台需求
   - 性能特征

#### 提交代码

1. Fork 本仓库
2. 创建功能分支：`git checkout -b feature/功能名` 或 `fix/问题描述`
3. 按照代码风格指南进行修改
4. 启用警告进行编译：
   ```bash
   make clean
   make CFLAGS="-Wall -Wextra -Werror -std=c23 -O2"
   ```
5. 运行已有测试，确认无回归
6. 为新增功能添加测试用例
7. 使用清晰、描述性的提交信息，遵循以下约定：
   ```
   <模块>: <动词> <描述>
   
   <可选的详细解释>
   
   示例：
   - lparser: 添加 `unless` 关键字支持
   - lvm: 修复 FORPREP 指令的差一错误
   - lclass: 实现 trait 冲突检测
   - docs: 更新 crypto 模块 API 文档
   ```
8. 推送并创建 Pull Request，包含：
   - 关联 Issue（如有）
   - 变更摘要
   - 已执行的测试
   - 性能影响说明（如相关）

### 代码风格指南

#### C 代码风格

项目遵循源自 Lua 代码库的统一样式约定：

**缩进：**
- 使用 2 个空格（不使用 Tab）
- 最大行宽：120 字符
- 续行与 opening 分隔符对齐

**命名：**
```c
/* 类型: PascalCase + _t 后缀 */
typedef struct { ... } MyStruct;
typedef enum { ... } MyEnum;

/* 变量: snake_case */
int local_variable;
lua_State *L;  /* Lua 状态的约定名称 */

/* 函数: 模块前缀 + snake_case */
int luaVM_execute(lua_State *L);
void obfuscate_apply(Proto *f, int flags);

/* 常量: 大写下划线分隔 */
#define MAX_BUFFER_SIZE 4096
#define IS_LIKELY(x) __builtin_expect(!!(x), 1)

/* 类函数宏: 小写 */
#define checknelems(L, n) ...
```

**头文件结构：**
```c
/**
 * @file filename.h
 * @brief 文件用途的单行描述。
 * 
 * 需要时可添加更长描述（允许多段落）。
 */

#ifndef filename_h
#define filename_h

/* 标准头文件 */
#include <stdlib.h>
#include "lua.h"       /* 项目头文件放在标准头之后 */

/* 公开类型 */
typedef struct { ... } PublicType;

/* 公开常量 */
#define PUBLIC_CONSTANT 42

/* 公开 API 函数声明 */
int public_function(lua_State *L);

#endif /* filename_h */
```

**函数注释（Doxygen 风格）：**
```c
/**
 * @brief 执行指定数量指令的 VM 主循环。
 * 
 * @param L      Lua 状态。
 * @param nexecls  要执行的指令数量（0 = 直到返回/yield）。
 * @return       无返回值（错误时 longjmp）。
 * 
 * @note 此函数使用 computed goto 进行调度，不可递归调用。
 *       嵌套执行请使用 lua_pcall。
 */
static void execute(lua_State *L, int nexecls);
```

**复杂算法注释：**
```c
/* === C3 线性化算法 ===
 * 目标：计算多继承的方法解析顺序 (MRO)。
 * 
 * 输入：类 C，父类 [C1, C2, ..., Cn]
 * 输出：线性列表 [C, ...]，满足：
 *   1. C 出现第一位
 *   2. 父类保持声明顺序
 *   3. 局部优先序一致
 * 
 * 算法：递归 merge(L[C1], ..., L[Cn], [C1, ..., Cn])
 */
```

#### 内存管理规则

1. **所有权明确**：每个 `malloc`/`calloc` 必须有且仅有一条 `free` 路径
2. **GC 托管分配使用 `luaM_*`**：Lua 对象不要直接使用 raw `malloc`
3. **分配/释放在同一抽象级别匹配**：如果函数分配内存，由调用者释放（或文档说明为"所有权转移"）
4. **错误路径无内存泄漏**：使用 `luaD_rawrunprotected` 进行保护分配
5. **栈平衡**：每次 push 都有对应的 pop 或文档说明的永久存储

#### 平台移植性

1. **平台相关代码必须使用 `#ifdef`**：
   ```c
   #ifdef _WIN32
   #include <windows.h>
   /* Windows 专用代码 */
   #else
   #include <unistd.h>
   /* POSIX 代码 */
   #endif
   ```

2. **支持的平台检测宏**：
   - `_WIN32` — Windows（32 位和 64 位）
   - `__linux__` — Linux
   - `__APPLE__` — macOS / iOS
   - `__ANDROID__` — Android NDK
   - `__EMSCRIPTEN__` — 通过 Emscripten 的 WebAssembly
   - `__TERMUX__` — Android Termux 环境

3. **字节序和对齐**：不要假设小端序或特定结构体填充。跨平台序列化使用 `memcpy`。

4. **整数大小**：使用 `<stdint.h>` 中的 `int32_t`、`int64_t` 等。永远不要假设 `int` 大小。

#### 文档要求

新增功能必须更新以下文档：

1. **`docs/SYNTAX_REFERENCE.md`** — 新语法特性
2. **`docs/API_REFERENCE.md`** — 新的公开 C API 或 Lua 库函数
3. **`docs/DEEP_DIVE.md`** — 重大架构变更
4. **`docs/TUTORIAL.md`** — 主要功能的用户示例
5. **代码注释** — 所有公开 API 使用 Doxygen 风格

### 构建系统

#### 构建目标

| 目标 | 描述 |
|------|------|
| `make` 或 `make all` | 构建 lua、luac 和所有内置库 |
| `make lua` | 仅构建独立解释器 |
| `make luac` | 仅构建字节码编译器 |
| `make lib` | 构建静态库 `liblxclua.a` |
| `make lspsrv` | 构建 LSP 服务器 |
| `make check` / `make test` | 运行测试套件 |

#### 构建配置

可在命令行覆盖的关键 `Makefile` 变量：

```bash
# 编译器选择
make CC=clang
make CC=gcc-13

# 构建类型
make BUILD=debug      # -O0 -g -DDEBUG
make BUILD=release    # -O2 -DNDEBUG
make BUILD=sanitize   # -O1 -fsanitize=address,undefined

# 平台
make PLATFORM=android   # Android 交叉编译
make PLATFORM=wasm      # Emscripten 构建

# 功能选择
make WITH_WASM=0        # 禁用 wasmtime 模块
make WITH_WASM3=1       # 启用 wasm3 模块（可选）
```

#### 添加新源文件

添加新 `.c` 文件时：

1. 在 `Makefile` 中添加到对应的 `_SRC` 变量：
   ```makefile
   # 对于 src/utils/newfile.c：
   UTILS_SRC += src/utils/newfile.c
   ```

2. 如果是新的 Lua 库模块，还需添加到 `linit.c`：
   ```c
   static const luaL_Reg loadedlibs[] = {
       /* ... 已有库 ... */
       {NEWLIBNAME, luaopen_newlib},
   };
   
   LUAMOD_API int luaopen_newlib(lua_State *L);
   ```

3. 添加对应的 `.h` 文件到 `Makefile` 中合适的 include 路径

### 测试

#### 运行测试

```bash
# 完整测试套件
make test

# 特定模块测试
make test VM=1        # VM 相关测试
make test COMPILER=1  # 编译器测试
make test OBFUSCATE=1 # 混淆测试

# 手动测试解释器
./lua -e "print('Hello from LXCLUA!')"
./lua test.lua
```

#### 编写测试

测试文件存放在 `tests/` 目录，命名约定如下：

```
tests/
├── vm/           # VM 指令测试
├── compiler/     # 解析器和代码生成测试
├── stdlib/       # 标准库测试
├── utils/        # 实用函数测试
└── integration/  # 端到端集成测试
```

测试文件格式：
```lua
-- tests/vm/test_for_loop.lua
-- 测试数值型 for 循环和泛型 for 循环执行

local function test_numeric_for()
  local sum = 0
  for i = 1, 10 do
    sum = sum + i
  end
  assert(sum == 55, "数值型 for 失败: " .. sum)
end

local function test_generic_for()
  local t = {1, 2, 3, 4, 5}
  local sum = 0
  for _, v in ipairs(t) do
    sum = sum + v
  end
  assert(sum == 15, "泛型 for 失败: " .. sum)
end

test_numeric_for()
test_generic_for()
print("所有 for 循环测试通过")
```

#### 性能基准

修改性能关键代码（VM 循环、GC、编译器）时：

1. 修改前后分别运行基准测试：
   ```bash
   make benchmark > before.txt
   # ... 修改 ...
   make benchmark > after.txt
   diff before.txt after.txt
   ```

2. 关键基准区域：
   - VM 指令吞吐量（指令/秒）
   - GC 暂停时间
   - 编译速度（行/秒）
   - 函数调用开销

3. 任何性能回归（> 5%）必须记录并说明合理性

### 发布流程

1. 版本格式：`MAJOR.MINOR.PATCH`（语义化版本）
2. 不兼容变更需要主版本号递增
3. 新功能需要次版本号递增
4. Bug 修复需要补丁版本号递增
5. 所有发布必须：
   - 通过完整测试套件
   - 在所有支持平台构建成功
   - 更新 `CHANGELOG.md`
   - 更新 `lua.h` 中的版本字符串

### 获取帮助

- 技术讨论：GitHub 上发起 Discussion
- 快速提问：Issue 标记 `question` 标签
- 代码审查：PR 中 @ `@DifierLine`
