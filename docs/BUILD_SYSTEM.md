# LXCLUA-NCore 编译系统文档

## 1. Makefile 结构概览

LXCLUA-NCore 使用 GNU Make 作为构建系统，主 Makefile 位于 `lua/Makefile`。此外还有 `lua/Make`（简化版 MinGW Makefile）和 `lua/Android.mk`（Android NDK 构建配置）。

### 1.1 变量系统

#### 编译器与工具链

| 变量 | 默认值 | 说明 |
| ---- | ------ | ---- |
| `CC` | `gcc -std=gnu11 -pipe` | C 编译器及基础选项，使用 GNU11 标准 |
| `AR` | `ar rcu` | 静态库归档工具 |
| `RANLIB` | `ranlib` | 静态库索引生成 |
| `RM` | `rm -f` | 文件删除命令 |
| `UNAME` | `uname` | 系统名称检测 |

#### 编译选项

| 变量 | 默认值 | 说明 |
| ---- | ------ | ---- |
| `CFLAGS` | 见下方详解 | 基础编译选项（优化级别、调试宏等） |
| `SYSCFLAGS` | 见下方详解 | 系统级宏定义，平台目标可覆盖 |
| `MYCFLAGS` | 包含路径集合 | 用户自定义编译选项，含所有源码头文件路径 |
| `CMCFLAGS` | 同 `MYCFLAGS` | 编译器模块专用编译选项 |

#### 链接选项

| 变量 | 默认值 | 说明 |
| ---- | ------ | ---- |
| `SYSLDFLAGS` | 空 | 系统级链接选项，平台目标可覆盖 |
| `MYLDFLAGS` | 空 | 用户自定义链接选项 |
| `LDFLAGS` | `$(SYSLDFLAGS) $(MYLDFLAGS)` | 合并后的链接选项 |
| `SYSLIBS` | 空 | 系统级链接库，平台目标可覆盖 |
| `MYLIBS` | 空 | 用户自定义链接库 |
| `LIBS` | `-lm $(SYSLIBS) $(MYLIBS) $(WASMTIME_LIB)` | 合并后的链接库 |

#### wasmtime 集成

| 变量 | 默认值 | 说明 |
| ---- | ------ | ---- |
| `WASMTIME_DIR` | `wasmtime/wasmtime-v45.0.1-x86_64-mingw-c-api` | wasmtime 预编译库目录（默认 MinGW） |
| `WASMTIME_INC` | `-I$(WASMTIME_DIR)/include` | wasmtime 头文件路径 |
| `WASMTIME_LIB` | `$(WASMTIME_DIR)/lib/libwasmtime.a -lbcrypt -luserenv -lole32 -lntdll` | wasmtime 静态库及 Windows 系统依赖 |
| `WASMTIME_DLL` | `$(WASMTIME_DIR)/lib/wasmtime.dll` | wasmtime 动态库路径 |

#### WASM 导出名称

| 变量 | 说明 |
| ---- | ---- |
| `WASM_EXPORT_NAME_LUA` | 解释器 WASM 模块导出名，wasm 构建时设为 `-sEXPORT_NAME=LuaModule` |
| `WASM_EXPORT_NAME_LUAC` | 编译器 WASM 模块导出名，wasm 构建时设为 `-sEXPORT_NAME=LuacModule` |
| `WASM_EXPORT_NAME_LUACCHECK` | 字节码检查器 WASM 模块导出名，wasm 构建时设为 `-sEXPORT_NAME=LuaccheckModule` |

#### 目录结构

| 变量 | 值 | 说明 |
| ---- | --- | ---- |
| `BUILDDIR` | `build/obj` | 所有 `.o` 目标文件的输出目录 |
| `VPATH` | `src/core:src/stdlib:src/vm:src/compiler:src/utils:src/wasm:src/bin:src/lua2wasm:pcre2/src` | Make 的源文件搜索路径 |

### 1.2 目标文件变量

项目中所有 `.o` 文件通过 `$(addprefix $(BUILDDIR)/, ...)` 前缀统一输出到 `build/obj/` 目录。

#### CORE_O — 核心运行时（含 JIT）

```
CORE_O = build/obj/sljitLir.o build/obj/ljit.o build/obj/ljit_ir.o ...
         build/obj/lapi.o build/obj/lcode.o build/obj/lctype.o ...
         build/obj/lobfuscate.o build/obj/lthread.o build/obj/lstruct.o ...
         build/obj/lnamespace.o build/obj/lbigint.o build/obj/lsuper.o
```

包含：
- **SLJIT 库**：`sljitLir.o`（底层 JIT 汇编基础设施）
- **LJIT 编译器**：`ljit.o`、`ljit_ir.o`、`ljit_codegen.o`、`ljit_regalloc.o`、`ljit_opt.o` 等约 20 个模块
- **Lua 核心**：`lapi.o`、`lcode.o`、`ldebug.o`、`ldo.o`、`lfunc.o`、`lgc.o`、`llex.o`、`lmem.o`、`lobject.o`、`lopcodes.o`、`lparser.o`、`lstate.o`、`lstring.o`、`ltable.o`、`ltm.o`、`lundump.o`、`lvm.o`、`lzio.o` 等
- **扩展核心**：`lasm.o`（汇编器）、`last.o`/`last_parse.o`/`last_visitor.o`/`last_serialize.o`（AST 处理）、`lcodegen.o`、`lobfuscate.o`（字节码混淆）、`lthread.o`、`lstruct.o`、`lnamespace.o`、`lbigint.o`、`lsuper.o`

#### CORE_O_NOJIT — 核心运行时（无 JIT）

```
CORE_O_NOJIT = build/obj/lapi.o build/obj/lcode.o ... build/obj/lvmustom.o
```

与 `CORE_O` 的区别：
- 去掉所有 `sljitLir.o` 和 `ljit_*.o` JIT 模块
- 增加 `lvmustom.o`（自定义 VM 桩实现，替代 JIT 功能）

#### LIB_O — 标准库与扩展模块

```
LIB_O = build/obj/lauxlib.o build/obj/lpatchlib.o build/obj/lbaselib.o ...
```

包含约 50 个模块，涵盖：
- **标准库**：`lbaselib.o`、`lcorolib.o`、`lmathlib.o`、`lstrlib.o`、`ltablib.o`、`lutf8lib.o`、`liolib.o`、`loslib.o`、`ldblib.o`、`loadlib.o`
- **扩展库**：`lboolib.o`、`lbitlib.o`、`lptrlib.o`、`ludatalib.o`、`lmaplib.o`、`lclass.o`、`lstruct.o`、`lthreadlib.o`
- **VM 模块**：`lvmlib.o`、`lvmustom.o`、`lnativevm.o`、`lnativeparser.o`、`lvmpro.o`
- **加密与安全**：`sha256.o`、`aes.o`、`crc.o`、`csprng.o`、`lcrypto.o`、`luuid.o`、`lrsa.o`、`lecc.o`
- **编译器**：`lbctc.o`、`lbytecode.o`、`llexerlib.o`、`llexer_compiler.o`、`ltranslator.o`、`lastlib.o`
- **网络与 IO**：`libhttp.o`、`lfs.o`、`laio.o`
- **其他**：`json_parser.o`、`logtable.o`、`lproclib.o`、`lquickjs.o`、`leventloop.o`、`lpromise.o`、`lpatchlib.o`

#### BASE_O — 完整静态库目标文件

```
BASE_O = $(CORE_O) $(LIB_O) $(LIB_O_WASM) $(QJS_O) $(MYOBJS) $(LUA2WASM_CORE_O) $(WAT2WASM_CORE_O) $(LUA2WASM_LIB_O) $(PCRE2_O)
```

包含所有需要链接进 `liblxclua.a` 的 `.o` 文件。

#### BASE_O_WASM — WASM 构建用目标文件（不含 lua2wasm）

```
BASE_O_WASM = $(CORE_O) $(LIB_O) $(LIB_O_WASM) $(MYOBJS) $(PCRE2_O)
```

与 `BASE_O` 的区别：不含 `QJS_O`、`LUA2WASM_CORE_O`、`WAT2WASM_CORE_O`、`LUA2WASM_LIB_O`。

#### LIB_O_WASM — WASM 运行时模块

```
LIB_O_WASM = build/obj/lwasm3.o build/obj/lwasmtime.o $(WASM3_O)
```

包含：
- `lwasm3.o`：wasm3 轻量级 WASM 解释器 Lua 绑定
- `lwasmtime.o`：wasmtime WASM 运行时 Lua 绑定
- `WASM3_O`：wasm3 引擎核心（约 15 个 `m3_*.o` 模块）

#### WASM3_O — wasm3 引擎

```
WASM3_O = build/obj/m3_api_libc.o build/obj/m3_api_meta_wasi.o build/obj/m3_api_tracer.o
          build/obj/m3_api_uvwasi.o build/obj/m3_api_wasi.o build/obj/m3_bind.o
          build/obj/m3_code.o build/obj/m3_compile.o build/obj/m3_core.o build/obj/m3_env.o
          build/obj/m3_exec.o build/obj/m3_function.o build/obj/m3_info.o build/obj/m3_module.o
          build/obj/m3_parse.o
```

#### QJS_O — QuickJS 引擎

```
QJS_O = quickjs/quickjs.o quickjs/libregexp.o quickjs/libunicode.o quickjs/cutils.o
        quickjs/quickjs-libc.o quickjs/dtoa.o
```

QuickJS 输出到 `quickjs/` 目录而非 `build/obj/`。

#### LUA2WASM_CORE_O — Lua-to-WASM 编译器

```
LUA2WASM_CORE_O = build/obj/ast.o build/obj/lexer_l2w.o build/obj/parser_l2w.o
                  build/obj/wat_builder.o build/obj/codegen_l2w.o build/obj/builtins_l2w.o
                  build/obj/xalloc_l2w.o
```

#### PCRE2_O — PCRE2 正则引擎（含 JIT）

```
PCRE2_O = build/obj/pcre2_auto_possess.o build/obj/pcre2_chartables.o ...
          build/obj/pcre2_jit_compile.o ... build/obj/pcre2_xclass.o  (共 30 个模块)
```

#### PCRE2_O_NOJIT — PCRE2 正则引擎（无 JIT）

```
PCRE2_O_NOJIT = ... build/obj/pcre2_jit_stubs.o ...  (pcre2_jit_compile.o 替换为 pcre2_jit_stubs.o)
```

### 1.3 产物变量

| 变量 | 值 | 说明 |
| ---- | --- | ---- |
| `LUA_A` | `liblxclua.a` | 静态库 |
| `LUA_T` | `lxclua` | 解释器可执行文件 |
| `LUA_O` | `build/obj/lua.o` | 解释器入口 |
| `LUAC_T` | `luac` | 字节码编译器 |
| `LUAC_O` | `build/obj/luac.o` | 编译器入口 |
| `LUACCHECK_T` | `luaccheck` | 字节码检查器 |
| `LUACCHECK_O` | `build/obj/luaccheck.o` | 检查器入口 |
| `LSP_SRV_T` | `lxclua-lsp` | LSP 语言服务器 |
| `LSP_SRV_O` | 10 个 `build/obj/lspsrv_*.o` | LSP 服务器模块 |
| `ALL_T` | `$(LUA_A) $(LUA_T) $(LUAC_T) $(LUACCHECK_T)` | 所有编译目标 |
| `ALL_A` | `$(LUA_A)` | 仅静态库 |

---

## 2. 所有编译目标

### 2.1 `make` / `make guess`

默认目标，自动检测操作系统并调用对应平台目标。

```
default: $(PLAT)   # PLAT 默认值为 guess
guess → $(MAKE) $(UNAME)   # 执行 uname 获取系统名并递归调用
```

### 2.2 `make mingw`

Windows MinGW 编译，生成以下产物：

| 产物 | 说明 |
| ---- | ---- |
| `lxclua.exe` | GUI 模式解释器（`-mwindows` 默认行为，含 `-DGUI_PLATFORM_WINDOWS`） |
| `luac.exe` | 控制台模式字节码编译器 |
| `luaccheck.exe` | 控制台模式字节码检查器（`-mconsole`） |
| `lxclua.dll` | 动态链接库（导出所有符号） |
| `lxclua-lsp.exe` | LSP 语言服务器 |
| `liblxclua.a` | 静态库 |

关键编译选项：
- `SYSCFLAGS` 包含 `-DGUI_PLATFORM_WINDOWS -D_UNICODE -DUNICODE`（仅 lxclua.exe）
- `SYSLIBS` 包含 `-lwininet -lws2_32 -lpsapi -lpthread -lcomctl32 -lshell32 -lcomdlg32 -lole32 -luuid -lgdi32 -lsecur32 -lcrypt32`
- lxclua.exe 栈大小设为 16MB：`-Wl,--stack,16777216`
- DLL 使用 `--export-all-symbols --allow-multiple-definition --whole-archive`

### 2.3 `make mingw-static`

与 `mingw` 相同，但**不生成 `lxclua.dll`**，lxclua.exe 栈大小不设 16MB（使用默认值）。

### 2.4 `make linux`

Linux x64 编译，使用 GCC gnu11。

| 产物 | 说明 |
| ---- | ---- |
| `lxclua` | 解释器 |
| `luac` | 字节码编译器 |
| `luaccheck` | 字节码检查器 |
| `liblxclua.a` | 静态库 |

关键编译选项：
- `CFLAGS` 包含 `-fPIC -D_DEFAULT_SOURCE`
- `SYSCFLAGS` 为 `-DLUA_USE_LINUX`
- `SYSLIBS` 为 `-Wl,-E -ldl -lm -lpthread -lssl -lcrypto`
- `SYSLDFLAGS` 为 `-s`（剥离符号）
- wasmtime 路径切换为 Linux 预编译库
- 编译后执行 `strip --strip-unneeded` 进一步裁剪

### 2.5 `make termux`

Android Termux 环境编译，使用 Clang C23 标准。

| 产物 | 说明 |
| ---- | ---- |
| `lxclua` | 解释器 |
| `luac` | 字节码编译器 |
| `luaccheck` | 字节码检查器 |
| `liblxclua.a` | 静态库 |

关键编译选项：
- `CC` 为 `clang -std=c23`
- `CFLAGS` 包含 `-fPIC`
- `SYSCFLAGS` 为 `-DLUA_USE_LINUX -DLUA_USE_DLOPEN`
- `SYSLIBS` 为 `-ldl -lm -lssl -lcrypto`
- `SYSLDFLAGS` 为 `-Wl,--build-id -fuse-ld=lld`（使用 LLD 链接器）
- wasmtime 路径切换为 Android aarch64 预编译库

### 2.6 `make android`

通过 Android NDK 的 `ndk-build` 命令使用 `Android.mk` 进行交叉编译，生成 `liblua.a`（静态库）。

与 `make termux` 的区别：
- 使用 NDK 工具链进行交叉编译（非本机构建）
- 编译标准为 C23，优化级别 `-O3`
- 包含 `-fasm` 支持内联汇编
- 针对不同 ABI 自动设置架构选项（`armv8-a`、`armv7-a`、`x86-64`、`i686`）
- 链接 `-llog -lz`（Android 日志和压缩库）

### 2.7 `make wasm`

使用 Emscripten 编译为 WebAssembly。

| 产物 | 说明 |
| ---- | ---- |
| `lxclua.js` | 解释器 WASM 模块（含 JS 胶水代码，单文件模式） |
| `luac.js` | 编译器 WASM 模块 |
| `luaccheck.js` | 字节码检查器 WASM 模块 |

关键特性：
- 编译前自动执行 `make clean`（全新构建）
- JIT **禁用**（`-DLUA_NOJIT`），使用 `CORE_O_NOJIT`
- PCRE2 使用 `PCRE2_O_NOJIT`（`pcre2_jit_stubs.o` 替代 `pcre2_jit_compile.o`）
- 不链接 wasmtime（`WASMTIME_INC` 和 `WASMTIME_LIB` 置空）
- 使用 `-DLUA_32BITS=0`（64 位模式）
- 使用 `-DLUA_USE_LONGJMP`（longjmp 异常处理替代 C++ 异常）
- Emscripten 链接选项：
  - `-sWASM=1`：生成 WASM 格式
  - `-sSINGLE_FILE=1`：JS 和 WASM 合并为单文件
  - `-sMODULARIZE=1`：ES 模块化封装
  - `-sALLOW_MEMORY_GROWTH=1`：允许内存动态增长
  - `-sSTACK_SIZE=5MB`：栈大小 5MB
  - `-sINITIAL_MEMORY=32MB`：初始内存 32MB
  - `-sINVOKE_RUN=0`：不自动运行 main

### 2.8 `make wasmlsp`

编译 LSP 服务器为 WASM。

| 产物 | 说明 |
| ---- | ---- |
| `lxclua-lsp.js` | LSP 语言服务器 WASM 模块 |

与 `make wasm` 的区别：
- 仅编译 LSP 服务器模块，不编译整个 Lua 运行时
- 导出名称为 `LuaLSPModule`
- 不依赖 `-DLUA_NOJIT` 等 Lua 相关宏

### 2.9 `make lsp`

编译桌面版 LSP 服务器。

| 产物 | 说明 |
| ---- | ---- |
| `lxclua-lsp.exe` | Windows LSP 可执行文件 |

关键特性：
- 不链接 wasmtime 运行时库
- 禁用 `_FORTIFY_SOURCE`（避免 GCC 15 的 `_chk` 符号链接失败）
- 仅链接 `-lm`（数学库）

### 2.10 `make lsp-linux`

编译 Linux 版 LSP 服务器。

```
make lsp-linux
```

### 2.11 `make lua2wasm`

编译独立的 `lua2wasm` CLI 工具（Lua 源码 → WAT/WASM 编译器）。

### 2.12 `make wat2wasm`

编译独立的 `wat2wasm` CLI 工具（WAT 文本 → WASM 二进制汇编器）。

### 2.13 `make all`

编译所有目标：`$(LUA_A) $(LUA_T) $(LUAC_T) $(LUACCHECK_T)`

### 2.14 `make clean`

清理所有编译产物，包括：
- `build/obj/` 目录
- 所有可执行文件（`.exe`、`.dll`）
- 所有中间文件（`.o`、`.a`）
- 所有 WASM 产物（`.js`、`.wasm`）
- 所有测试文件（`.lua`、`.luac`、`.out`、`.log`）

### 2.15 `make test`

运行解释器版本检查：`./lxclua -v`

### 2.16 `make head`

使用 Python 脚本 `tools/merge_headers.py` 生成合并头文件 `lxclua.h`（单头文件，供 C 扩展模块开发使用）。

### 2.17 发行版打包目标

| 目标 | 产物格式 | 包含文件 |
| ---- | -------- | -------- |
| `make mingw-release` | `.zip` | `lxclua.exe`、`luac.exe`、`luaccheck.exe`、`lxclua-lsp.exe`、`lxclua.dll`、`LICENSE` |
| `make linux-release` | `.tar.gz` | `lxclua`、`luac`、`luaccheck`、`liblxclua.a`、`LICENSE` |
| `make macos-release` | `.tar.gz` | `lxclua`、`luac`、`LICENSE`、`README.md` |
| `make termux-release` | `.tar.gz` | `lxclua`、`luac`、`luaccheck`、`liblxclua.a`、`LICENSE` |
| `make wasm-release` | `.zip` | `lxclua.js`、`luac.js`、`luaccheck.js`、`LICENSE` |
| `make release` | `.tar.gz` | 当前平台的 `$(LUA_T)`、`$(LUAC_T)`、`$(LUA_A)`、`LICENSE`、`README.md` |

所有发布包命名格式：`lxclua-<平台>-<YYYYMMDD_HHMMSS>.<扩展名>`

---

## 3. 编译选项说明

### 3.1 CFLAGS 默认选项

```
-O2 -funroll-loops -fomit-frame-pointer -ffunction-sections -fdata-sections
-fstrict-aliasing -g0 -DNDEBUG -fno-exceptions -Wimplicit-function-declaration -D_GNU_SOURCE
```

| 选项 | 含义 |
| ---- | ---- |
| `-O2` | 二级优化（平衡编译速度与运行速度） |
| `-funroll-loops` | 循环展开优化 |
| `-fomit-frame-pointer` | 省略帧指针，释放一个寄存器 |
| `-ffunction-sections` | 每个函数放入独立段（配合 `--gc-sections` 链接器优化） |
| `-fdata-sections` | 每个数据变量放入独立段 |
| `-fstrict-aliasing` | 严格别名规则优化 |
| `-g0` | 不生成调试信息 |
| `-DNDEBUG` | 禁用 assert 断言 |
| `-fno-exceptions` | 禁用 C++ 异常（减少代码体积） |
| `-Wimplicit-function-declaration` | 警告隐式函数声明 |
| `-D_GNU_SOURCE` | 启用 GNU 扩展（POSIX + GNU 特有功能） |

### 3.2 SYSCFLAGS 系统级宏定义

默认值：
```
-DLUA_DL_DLOPEN -DLUA_COMPAT_MATHLIB -DLUA_COMPAT_MAXN -DLUA_COMPAT_MODULE
```

| 宏 | 含义 |
| --- | ---- |
| `LUA_DL_DLOPEN` | 使用 dlopen 加载动态库 |
| `LUA_COMPAT_MATHLIB` | 兼容旧版 math 库 API |
| `LUA_COMPAT_MAXN` | 兼容 `table.maxn` 函数 |
| `LUA_COMPAT_MODULE` | 兼容 `module()` 函数 |

各平台额外宏：

| 平台 | 额外宏 |
| ---- | ------ |
| Linux | `-DLUA_USE_LINUX` |
| Termux | `-DLUA_USE_LINUX -DLUA_USE_DLOPEN` |
| MinGW | `-DGUI_PLATFORM_WINDOWS -D_UNICODE -DUNICODE`（仅 lxclua.exe） |
| macOS | `-DLUA_USE_MACOSX -DLUA_USE_READLINE` |
| WASM | `-DLUA_USE_LONGJMP -DLUA_NOJIT` |
| POSIX | `-DLUA_USE_POSIX` |
| iOS | `-DLUA_USE_IOS` |

### 3.3 MYCFLAGS 包含路径

```
-Isrc/core -Isrc/stdlib -Isrc/vm -Isrc/compiler -Isrc/utils -Isrc/wasm
-Isrc/bin -Iquickjs -Isrc/lua2wasm -Ipcre2 -DPCRE2_CODE_UNIT_WIDTH=8 -DHAVE_CONFIG_H
$(WASMTIME_INC)
```

| 包含路径 | 内容 |
| -------- | ---- |
| `src/core` | Lua 核心 API、GC、VM、状态机、字符串、表 |
| `src/stdlib` | 标准库及扩展库 |
| `src/vm` | 虚拟机实现、JIT 编译器、原生 VM |
| `src/compiler` | 词法/语法分析、字节码生成、AST |
| `src/utils` | 加密、IO、JSON、HTTP、大整数等工具 |
| `src/wasm` | wasm3/wasmtime 绑定 |
| `src/bin` | 解释器入口、QuickJS 绑定 |
| `src/lua2wasm` | Lua-to-WASM 编译器 |
| `quickjs` | QuickJS 引擎 |
| `pcre2` | PCRE2 正则引擎 |
| `wasmtime/...` | wasmtime C API 头文件 |

### 3.4 各平台特殊编译选项

#### Windows/MinGW
- `-mwindows`（默认，GUI 子系统）
- `-mconsole`（luaccheck.exe 强制控制台模式）
- `-Wl,--stack,16777216`（16MB 栈空间）
- `-Wl,--export-all-symbols`（DLL 导出所有符号）
- `-Wl,--allow-multiple-definition`（允许多重定义）
- `-Wl,--whole-archive ... --no-whole-archive`（全量静态链接）

#### Linux
- `-fPIC`（位置无关代码）
- `-D_DEFAULT_SOURCE`（替代 `_BSD_SOURCE` + `_SVID_SOURCE`）
- `-Wl,-E`（导出所有符号到动态符号表）
- `strip --strip-unneeded`（移除不需要的符号）

#### Termux
- `-std=c23`（C23 标准）
- `-fuse-ld=lld`（LLD 链接器）
- `-Wl,--build-id`（生成 Build ID 标记）

#### WASM (Emscripten)
- `-std=c23`（C23 标准）
- `-O3`（最高优化级别）
- `-DLUA_32BITS=0`（64 位 Lua）
- `-DLUA_USE_LONGJMP`（longjmp 错误处理）
- `-DLUA_NOJIT`（禁用 JIT）
- `PYTHONUTF8=1`（设置 Python UTF-8 模式，避免编码问题）
- `-sSINGLE_FILE=1`（单文件输出）
- `-sMODULARIZE=1`（ES 模块封装）
- `-sALLOW_MEMORY_GROWTH=1`（动态内存增长）

#### Android NDK
- `-std=c23 -O3`
- `-fasm`（内联汇编支持）
- `-fno-unwind-tables`（不生成栈展开表）
- 按 ABI 设置架构优化：`-march=armv8-a` / `-march=armv7-a` / `-march=x86-64` / `-march=i686`

---

## 4. 平台适配

### 4.1 Windows / MinGW

Windows 平台使用 MSYS2 MinGW64 工具链，适配要点：

- **GUI 子系统**：`lxclua.exe` 默认链接为 Windows GUI 应用（通过 `-DGUI_PLATFORM_WINDOWS` 和 `-mwindows`），不显示控制台窗口
- **控制台子系统**：`luac.exe` 和 `luaccheck.exe` 使用控制台模式（`luaccheck.exe` 显式使用 `-mconsole`）
- **DLL 生成**：`lxclua.dll` 使用 `--export-all-symbols` 导出所有符号，`--whole-archive` 确保静态库全部链接
- **系统库**：链接 Windows API 库（`ws2_32` 网络、`psapi` 进程信息、`crypt32` 加密、`comctl32` 控件、`shell32` Shell、`comdlg32` 对话框、`gdi32` 图形、`wininet` 网络、`secur32` 安全）
- **环境变量**：设置 `TMPDIR=. TMP=. TEMP=.` 避免临时文件路径问题

### 4.2 Linux

标准 POSIX 编译，适配要点：

- **链接方式**：使用 `-Wl,-E` 导出动态符号，支持 `dlopen` 加载 C 扩展模块
- **系统库**：链接 `libssl`、`libcrypto`（OpenSSL）、`libdl`、`libpthread`
- **符号裁剪**：编译后执行 `strip --strip-unneeded` 减小二进制体积
- **wasmtime**：使用 Linux 预编译的 `wasmtime-v45.0.1-x86_64-linux-c-api`

### 4.3 Termux (Android)

Android Termux 环境编译，适配要点：

- **编译器**：使用 `clang` 而非 GCC，支持 C23 标准
- **链接器**：使用 LLD（`-fuse-ld=lld`），性能优于 BFD/Gold
- **无 X11**：隐式不包含 X11 相关代码（Termux 无 X11 环境）
- **wasmtime**：使用 Android aarch64 预编译的 `wasmtime-v45.0.1-aarch64-android-c-api`

### 4.4 Emscripten / WASM

WebAssembly 编译，适配要点：

- **工具链**：使用 `emcc`（Emscripten 前端）、`emar`（归档）、`emranlib`（索引）
- **Python UTF-8**：`PYTHONUTF8=1` 确保 Python 在 Windows 下正确处理 UTF-8 路径
- **JIT 禁用**：WASM 不支持运行时 JIT 编译，使用 `CORE_O_NOJIT` 和 `PCRE2_O_NOJIT`
- **无 wasmtime**：WASM 目标不需要 wasmtime 运行时库
- **wasm3 运行时**：保留 wasm3 引擎用于 WASM 模块加载
- **QuickJS 和 lua2wasm 排除**：WASM 构建不包含 QuickJS 和 lua2wasm 编译器模块
- **内存管理**：初始内存 32MB，允许动态增长，栈大小 5MB
- **文件系统**：启用虚拟文件系统（`-sFILESYSTEM=1`）
- **模块化**：ES 模块封装（`-sMODULARIZE=1`），各产物独立模块名

### 4.5 Android NDK

通过 `Android.mk` 进行 NDK 交叉编译，适配要点：

- **构建系统**：使用 `ndk-build` 命令，而非 `make`
- **产物类型**：`BUILD_STATIC_LIBRARY`（静态库）
- **ABI 适配**：自动检测 `TARGET_ARCH_ABI` 并设置架构优化选项
- **Android 日志**：`LOCAL_EXPORT_LDLIBS := -llog` 导出日志库依赖
- **源文件**：在 `Android.mk` 中显式列出所有源文件路径（不依赖 VPATH）

---

## 5. 产物说明

### 5.1 静态库

| 产物 | 路径 | 说明 |
| ---- | ---- | ---- |
| `liblxclua.a` | 项目根目录 | 包含所有核心、标准库、扩展模块、JIT、wasm3、QuickJS、PCRE2 的静态库 |

`liblxclua.a` 是所有其他产物的基础，通过 `ar rcu` 归档，`ranlib` 生成索引。

### 5.2 解释器

| 平台 | 产物 |
| ---- | ---- |
| Windows | `lxclua.exe` |
| Linux | `lxclua` |
| macOS | `lxclua` |
| Termux | `lxclua` |
| WASM | `lxclua.js`（含内嵌 WASM） |

链接方式：`$(CC) -o $@ $(LDFLAGS) $(WASM_EXPORT_NAME_LUA) $(LUA_O) $(LUA_A) $(LIBS)`

### 5.3 字节码编译器

| 平台 | 产物 |
| ---- | ---- |
| Windows | `luac.exe` |
| Linux | `luac` |
| macOS | `luac` |
| Termux | `luac` |
| WASM | `luac.js` |

链接方式：`$(CC) -o $@ $(LDFLAGS) $(WASM_EXPORT_NAME_LUAC) $(LUAC_O) $(LUA_A) $(LIBS)`

### 5.4 字节码检查器

| 平台 | 产物 |
| ---- | ---- |
| Windows | `luaccheck.exe`（强制 `-mconsole`） |
| Linux | `luaccheck` |
| Termux | `luaccheck` |
| WASM | `luaccheck.js` |

用于验证、分析 `.luac` 字节码文件，依赖 `lobfuscate.h` 进行解密/反混淆。

### 5.5 LSP 语言服务器

| 平台 | 产物 |
| ---- | ---- |
| Windows | `lxclua-lsp.exe` |
| Linux | `lxclua-lsp` |
| WASM | `lxclua-lsp.js` |

LSP 服务器独立编译，不依赖 wasmtime 运行时，仅链接 `-lm`。由 10 个模块组成：

| 模块 | 功能 |
| ---- | ---- |
| `lspsrv_main` | 主入口 |
| `lspsrv_json` | JSON 解析 |
| `lspsrv_proto` | LSP 协议实现 |
| `lspsrv_doc` | 文档管理 |
| `lspsrv_lexer` | 词法分析 |
| `lspsrv_kwdb` | 关键字数据库 |
| `lspsrv_complete` | 代码补全 |
| `lspsrv_hover` | 悬停信息 |
| `lspsrv_features` | 特性支持 |
| `lspsrv_util` | 工具函数 |

### 5.6 动态链接库（仅 Windows）

| 产物 | 说明 |
| ---- | ---- |
| `lxclua.dll` | 导出所有符号，用于其他程序动态加载 |

构建命令：
```
$(CC) -shared -o lxclua.dll
  -Wl,--export-all-symbols
  -Wl,--allow-multiple-definition
  -Wl,--whole-archive liblxclua.a -Wl,--no-whole-archive
  $(WASMTIME_LIB) -lwininet -lws2_32 -lpsapi -lpthread -lcomctl32
  -lshell32 -lcomdlg32 -lole32 -luuid -lgdi32 -lsecur32 -lcrypt32 -lm
```

### 5.7 WASM 产物

| 产物 | 说明 |
| ---- | ---- |
| `lxclua.js` | 解释器（单文件，含 JS 胶水代码 + 内嵌 WASM） |
| `luac.js` | 字节码编译器 |
| `luaccheck.js` | 字节码检查器 |
| `lxclua-lsp.js` | LSP 语言服务器 |

所有 WASM 产物均为单文件（`-sSINGLE_FILE=1`），ES 模块化封装（`-sMODULARIZE=1`），不自动运行（`-sINVOKE_RUN=0`）。

### 5.8 独立工具

| 产物 | 说明 |
| ---- | ---- |
| `lua2wasm` / `lua2wasm.exe` | Lua-to-WASM 编译器 CLI（`make lua2wasm`） |
| `wat2wasm` / `wat2wasm.exe` | WAT-to-WASM 汇编器 CLI（`make wat2wasm`） |
| `qjs` / `qjs.exe` | QuickJS 解释器（随 `make all` 生成） |
| `qjsc` / `qjsc.exe` | QuickJS 编译器（随 `make all` 生成） |

---

## 6. 特殊编译流程

### 6.1 字节码密钥生成

LXCLUA-NCore 支持字节码加密/混淆功能。编译时通过 `lobfuscate` 模块内置的密钥机制对字节码进行保护。

- **密钥生成**：编译时自动生成 256 位（32 字节）密钥，嵌入到 `lobfuscate.o` 中
- **密钥作用**：用于 `.luac` 字节码文件的加密/解密，防止字节码被反编译或篡改
- **密钥文件**：密钥在编译时生成，编译完成后自动清理，不保留在源码目录中，确保每次编译产出的密钥不同
- **影响**：使用不同密钥编译的 `lxclua` 解释器无法加载其他版本编译的 `.luac` 文件

### 6.2 JIT 编译器编译流程

JIT（Just-In-Time）编译器是 LXCLUA-NCore 的核心性能特性，包含以下编译流程：

1. **SLJIT 基础设施**：`sljitLir.o`——底层与平台无关的 JIT 汇编抽象层
2. **LJIT 核心**：`ljit.o`——JIT 编译器主控制器
3. **IR（中间表示）**：`ljit_ir.o`、`ljit_ir_list.o`、`ljit_ir_label.o`、`ljit_ir_bb.o`
4. **前端（翻译）**：`ljit_translate.o`、`ljit_analyze.o`——将 Lua 字节码翻译为 IR
5. **优化器**：`ljit_opt.o`、`ljit_opt_const.o`、`ljit_opt_dce.o`、`ljit_opt_peep.o`、`ljit_opt_cse.o`、`ljit_opt_inline.o`——IR 优化遍
6. **寄存器分配**：`ljit_regalloc.o`、`ljit_reg_live.o`、`ljit_reg_graph.o`、`ljit_reg_color.o`、`ljit_reg_spill.o`、`ljit_reg_alloc.o`
7. **代码生成**：`ljit_codegen.o`、`ljit_cg_arith.o`、`ljit_cg_ctrl.o`、`ljit_cg_table.o`、`ljit_cg_conv.o`、`ljit_cg_closure.o`、`ljit_cg_oop.o`——生成目标机器码
8. **SLJIT 桥接**：`ljit_sljit.o`——将 LJIT 的代码生成调用转换为 SLJIT API

所有 JIT 源文件位于 `src/vm/jit/` 和 `src/jit/` 目录，编译时使用 `-I.` 包含根目录。

### 6.3 无 JIT 编译流程

当目标平台不支持 JIT（如 WASM）时，使用 `CORE_O_NOJIT` 和 `PCRE2_O_NOJIT`：

- 移除所有 `sljitLir.o` 和 `ljit_*.o` 模块
- 使用 `lvmustom.o` 提供自定义 VM 桩实现（替代 JIT 加速路径）
- 使用 `pcre2_jit_stubs.o` 替代 `pcre2_jit_compile.o`（PCRE2 JIT 桩）

### 6.4 lua2wasm 编译器编译流程

`lua2wasm` 将 Lua 源码编译为 WAT/WASM 格式，核心模块直接编译进 `liblxclua.a`，可在 Lua 中通过 `require("lua2wasm")` 使用。

编译管线：
1. **词法分析**：`lexer_l2w.o`（`src/lua2wasm/lexer.c`，注意与 `llex.o` 区分）
2. **语法分析**：`parser_l2w.o`（`src/lua2wasm/parser.c`）
3. **AST 构建**：`ast.o`（`src/lua2wasm/ast.c`）
4. **代码生成**：`codegen_l2w.o`（`src/lua2wasm/codegen.c`）
5. **WAT 输出**：`wat_builder.o`（`src/lua2wasm/wat_builder.c`）
6. **内建函数**：`builtins_l2w.o`（`src/lua2wasm/builtins.c`）
7. **WAT→WASM 汇编**：`wat2wasm_core.o`（`src/lua2wasm/wat2wasm.c`）
8. **Lua 模块入口**：`lua2wasmlib.o`（`src/lua2wasm/lua2wasmlib.c`）
9. **内存分配**：`xalloc_l2w.o`（`src/lua2wasm/xalloc.c`）

独立 CLI 工具额外编译：
- `lua2wasm_main.o`（`src/lua2wasm/main.c`）→ `lua2wasm` 可执行文件
- `wat2wasm_cli.o`（`src/lua2wasm/wat2wasm_cli.c`）→ `wat2wasm` 可执行文件

### 6.5 QuickJS 集成编译

QuickJS 引擎编译到 `quickjs/` 目录（非 `build/obj/`）：

| 模块 | 编译选项 |
| ---- | -------- |
| `quickjs/quickjs.o` | `-DCONFIG_VERSION="2024-01-13"` |
| `quickjs/libregexp.o` | 同 quickjs.o |
| `quickjs/libunicode.o` | 同 quickjs.o |
| `quickjs/cutils.o` | 同 quickjs.o |
| `quickjs/quickjs-libc.o` | 同 quickjs.o |
| `quickjs/dtoa.o` | 同 quickjs.o |
| `quickjs/qjs.o` | `-DCONFIG_VERSION="2024-01-13"` |
| `quickjs/qjsc.o` | `-DCONFIG_PREFIX="/usr/local" -DCONFIG_VERSION="2024-01-13"` |

Lua 绑定模块 `lquickjs.o` 编译到 `build/obj/`，通过 `require("quickjs")` 在 Lua 中使用。

### 6.6 PCRE2 正则引擎编译

PCRE2 编译使用独立规则：

```
$(BUILDDIR)/pcre2_%.o: pcre2/src/pcre2_%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(PCRE2_CFLAGS) -Ipcre2 -Ipcre2/src -c $< -o $@
```

编译选项：
- `-DPCRE2_CODE_UNIT_WIDTH=8`：8 位字符模式
- `-DHAVE_CONFIG_H`：使用配置文件
- 包含路径：`-Ipcre2 -Ipcre2/src`

### 6.7 合并头文件生成

`make head` 调用 `tools/merge_headers.py`，将多个 Lua 头文件合并为单个 `lxclua.h`，供 C 扩展模块开发使用，免除手动管理多个 `#include`。

---

## 7. 编译依赖关系图

```
src/*.c, src/vm/jit/*.c, src/lua2wasm/*.c, pcre2/src/*.c, quickjs/*.c
    │
    │  $(CC) $(CFLAGS) $(MYCFLAGS) -c
    ▼
build/obj/*.o, quickjs/*.o
    │
    │  $(AR) rcu + $(RANLIB)
    ▼
liblxclua.a
    │
    ├── $(CC) ... lua.o liblxclua.a $(LIBS) ──► lxclua / lxclua.exe
    ├── $(CC) ... luac.o liblxclua.a $(LIBS) ──► luac / luac.exe
    ├── $(CC) ... luaccheck.o liblxclua.a $(LIBS) ──► luaccheck / luaccheck.exe
    ├── $(CC) -shared ... liblxclua.a $(LIBS) ──► lxclua.dll (Windows)
    └── $(CC) ... qjs.o / qjsc.o liblxclua.a $(LIBS) ──► qjs / qjsc
```

---

## 8. 常用命令速查

```bash
# 编译（自动检测平台）
make

# 平台编译
make mingw          # Windows MinGW
make linux          # Linux x64
make termux         # Android Termux
make macosx         # macOS
make wasm           # WebAssembly (Emscripten)
make wasmlsp        # WASM LSP 服务器

# 独立工具
make lua2wasm       # Lua-to-WASM 编译器
make wat2wasm       # WAT-to-WASM 汇编器
make lsp            # Windows LSP 服务器
make lsp-linux      # Linux LSP 服务器
make head           # 生成合并头文件 lxclua.h

# 发行版打包
make mingw-release
make linux-release
make termux-release
make wasm-release
make release

# 其他
make clean          # 清理
make test           # 版本检查
make echo           # 打印编译变量
make help           # 显示帮助
```