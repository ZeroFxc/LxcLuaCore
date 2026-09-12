# 贡献指南

> 欢迎参与 LXCLUA-NCore 的开发。本文档涵盖项目架构、代码风格、内存管理、平台移植性、测试和发布流程。

---

## 1. 项目架构导览

```
src/
├── core/        — 核心运行时 (GC, 表, 字符串, API, 调试)
├── compiler/    — 编译器 (词法/语法/代码生成/汇编)
├── vm/          — 虚拟机 (执行引擎/保护/内省/原生VM)
├── stdlib/      — 标准库 (base/string/math/table/io/class 等)
├── utils/       — 工具库 (BigInt/加密/线程/Promise/事件循环/异步IO)
├── wasm/        — WASM 运行时绑定 (wasmtime/wasm3)
├── lspsrv/      — LSP 服务器
└── bin/         — 可执行文件入口
```

### 依赖关系

```
core ← compiler ← vm ← stdlib
  ↑                    ↑
utils ──────────────── utils
  ↑
wasm / lspsrv
```

- `core` 层无依赖（除了标准 C 库）
- `compiler` 依赖 `core`
- `vm` 依赖 `core` + `compiler`
- `stdlib` 依赖 `vm` + `core` + `utils`
- 上层模块不依赖同层或下层模块

---

## 2. 代码风格

### 2.1 命名约定

| 类别 | 约定 | 示例 |
|------|------|------|
| 函数 (公开) | `luaX_xxx` | `luaV_execute`, `luaC_newclass` |
| 函数 (静态) | `xxx_yyy` | `block_terminator`, `check_access` |
| 宏 | `LUA_X_XXX` 或 `XXX_YYY` | `LUA_TNUMBER`, `GETARG_A` |
| 类型 | `XxxYyy` | `TValue`, `CallInfo`, `BasicBlock` |
| 枚举 | `XXX_YYY` | `OP_MOVE`, `CLASS_FLAG_FINAL` |
| 全局变量 | `luaX_xxx` | `luaP_opmodes` |

### 2.2 头文件模板

```c
/**
 * @file lxxx.h
 * @brief Brief description of the module.
 */

#ifndef lxxx_h
#define lxxx_h

#include "lua.h"

/* 公开宏/常量/类型定义 */

/* 公开函数声明 */
LUAI_FUNC int luaX_xxx (lua_State *L, int arg);

#endif
```

### 2.3 实现文件模板

```c
/*
** $Id: lxxx.c $
** Brief description
** See Copyright Notice in lua.h
*/

#define lxxx_c
#define LUA_CORE

#include "lprefix.h"

#include <string.h>
#include <stdlib.h>

#include "lua.h"
#include "lobject.h"
#include "lstate.h"

/* 内部定义 */

/* 外部接口实现 */
```

### 2.4 注释规范

- 公开函数: 使用 Doxygen 风格注释
- 静态函数: 简短行注释
- 复杂逻辑: 添加注释说明意图，而非描述做了什么
- 混淆与安全代码: 必须添加详细注释说明变换逻辑

---

## 3. 内存管理规则

### 规则 1: 使用 GC 分配器

所有 Lua 堆上分配必须通过 GC 分配器:

```c
// 正确
GCObject *o = luaC_newobj(L, LUA_VNUMBIG, size);

// 错误
GCObject *o = malloc(size);
```

### 规则 2: 栈平衡

函数返回前必须保证栈平衡:

```c
// 正确
int luaX_xxx(lua_State *L) {
    lua_pushinteger(L, 42);
    return 1;  // 栈上只留一个返回值
}

// 错误
int luaX_xxx(lua_State *L) {
    lua_pushinteger(L, 42);
    lua_pushstring(L, "extra");  // 多余的值
    return 1;
}
```

### 规则 3: 保护调用处理

在 `lua_pcall` 后检查返回值:

```c
if (lua_pcall(L, nargs, nresults, 0) != LUA_OK) {
    // 错误已在栈顶
    const char *msg = lua_tostring(L, -1);
    // 处理错误
    lua_pop(L, 1);
}
```

### 规则 4: 引用管理

使用注册表引用管理长期存活的对象:

```c
int ref = luaL_ref(L, LUA_REGISTRYINDEX);  // 创建引用
// ...
lua_rawgeti(L, LUA_REGISTRYINDEX, ref);   // 获取引用对象
// ...
luaL_unref(L, LUA_REGISTRYINDEX, ref);    // 释放引用
```

### 规则 5: 避免内存泄漏

- 所有 `lua_newuserdata` 分配的内存必须设置 `__gc` 元方法
- 所有 `malloc`/`free` 对必须匹配
- 所有 `lua_ref` 必须有对应的 `lua_unref`

---

## 4. 平台移植性

### 4.1 目标平台

| 平台 | 架构 | 支持级别 |
|------|------|----------|
| Windows | x86_64, x86 | 完整 |
| Linux | x86_64, aarch64 | 完整 |
| macOS | aarch64, x86_64 | 完整 |
| Android | aarch64, armv7 | 基础 (NDK) |
| Emscripten | wasm32 | 基础 |
| FreeBSD | x86_64 | 基础 (kqueue) |

### 4.2 平台检测宏

```c
#if defined(_WIN32)
    // Windows 代码
#elif defined(__linux__)
    // Linux 代码
#elif defined(__APPLE__)
    // macOS 代码
#elif defined(__ANDROID__)
    // Android 代码
#elif defined(__EMSCRIPTEN__)
    // Emscripten 代码
#elif defined(__FreeBSD__) || defined(__OpenBSD__)
    // BSD 代码
#endif
```

### 4.3 字节序处理

```c
// 使用 lua_unsigned 和宏处理字节序
#if defined(LUA_USE_WINDOWS)
    #include <windows.h>
    #define lua_swap16(x) _byteswap_ushort(x)
    #define lua_swap32(x) _byteswap_ulong(x)
    #define lua_swap64(x) _byteswap_uint64(x)
#elif defined(__GNUC__) || defined(__clang__)
    #define lua_swap16(x) __builtin_bswap16(x)
    #define lua_swap32(x) __builtin_bswap32(x)
    #define lua_swap64(x) __builtin_bswap64(x)
#endif
```

### 4.4 整数大小保证

```c
// 跨平台整数类型
typedef int64_t lua_Integer;      // 总是 64 位
typedef double lua_Number;        // 总是双精度
typedef uint64_t lua_Unsigned;    // 总是 64 位无符号

// 检查
#define LUA_MAXINTEGER  INT64_MAX
#define LUA_MININTEGER  INT64_MIN
```

---

## 5. 构建系统

### 5.1 CMake 变量

| 变量 | 类型 | 默认 | 说明 |
|------|------|------|------|
| `BUILD_LUA` | BOOL | ON | 构建 lxclua 可执行文件 |
| `BUILD_LUA_LIB` | BOOL | OFF | 构建静态库 |
| `BUILD_LUA_DLL` | BOOL | OFF | 构建动态库 |
| `BUILD_LSP` | BOOL | ON | 构建 LSP 服务器 |
| `BUILD_TESTS` | BOOL | ON | 构建测试 |
| `ENABLE_LTO` | BOOL | OFF | 链接时优化 |
| `ENABLE_OBFUSCATION` | BOOL | ON | 混淆支持 |
| `ENABLE_CRYPTO` | BOOL | ON | 密码学库 |
| `ENABLE_PCRE2` | BOOL | ON | PCRE2 正则 |
| `ENABLE_WASM` | BOOL | ON | WASM 运行时 |

### 5.2 添加新文件

1. 在对应模块目录创建 `.c` 和 `.h` 文件
2. 更新 `CMakeLists.txt` 中的源文件列表
3. 如果创建新模块，添加 `luaopen_xxx` 函数
4. 在 `linit.c` 中注册新模块

---

## 6. 测试

### 6.1 测试文件位置

```
test/
├── *.lua          — Lua 测试脚本
├── test_xxx.lua   — 功能测试
├── benchmark.lua  — 性能基准测试
└── *.wasm         — WASM 测试模块
```

### 6.2 测试规范

- 每个测试文件独立可运行
- 使用 `assert` 验证结果
- 提供 PASS/FAIL 输出
- 测试覆盖: 正常路径、边界条件、错误路径

### 6.3 运行测试

```bash
# 运行所有测试
lxclua.exe test/all.lua

# 运行单个测试
lxclua.exe test/test_xxx.lua
```

### 6.4 性能基准测试

```bash
# 基准测试
lxclua.exe test/benchmark.lua
```

基准测试应包含:
- 函数调用开销
- 循环性能
- 表操作性能
- 字符串操作性能
- OOP 方法调用性能
- 与其他实现的对比

---

## 7. 发布流程

### 7.1 版本号

遵循语义化版本: `MAJOR.MINOR.PATCH`

- `MAJOR`: 不兼容的 API 变更
- `MINOR`: 向后兼容的功能新增
- `PATCH`: 向后兼容的 bug 修复

### 7.2 发布步骤

1. **代码冻结**: 所有功能完成，冻结 master 分支
2. **测试**: 运行完整测试套件，修复所有失败
3. **文档**: 更新 CHANGELOG、README、API 文档
4. **标签**: 创建 git tag (`v2.0.0`)
5. **构建**: 在目标平台构建发布包
6. **发布**: 发布 GitHub Release

### 7.3 CHANGELOG 格式

遵循 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.0.0/) 格式:

```markdown
## [2.0.0] - 2026-08-26

### 新增
- 功能 A

### 变更
- 功能 B 行为变更

### 修复
- Bug C 修复
```