# Build Guide / 编译指南

[English](#english) | [中文](#中文)

---

## English

### Prerequisites

| Platform | Compiler | Required Libraries |
| -------- | -------- | ------------------ |
| Linux x64 | GCC (gnu11) | `libssl-dev`, `libcrypto` |
| Windows (MinGW) | MSYS2 MinGW64 GCC | `mingw-w64-x86_64-gcc`, `mingw-w64-x86_64-ccache` |
| macOS | Clang | System libraries |
| Android (Termux) | Clang (c23) | `openssl`, `lld` |
| WebAssembly | Emscripten 3.0+ | Emscripten SDK |

### Step 1: Download wasmtime Prebuilt Library

**LXCLUA-NCore requires wasmtime v45.0.1 C API** for WASM runtime support. The prebuilt binaries are NOT included in the repository and must be downloaded manually.

#### Linux x64

```bash
mkdir -p wasmtime
curl -L -o wasmtime-linux.tar.xz \
  https://github.com/bytecodealliance/wasmtime/releases/download/v45.0.1/wasmtime-v45.0.1-x86_64-linux-c-api.tar.xz
tar -xf wasmtime-linux.tar.xz -C wasmtime/
rm wasmtime-linux.tar.xz
```

Expected directory structure after extraction:

```
wasmtime/
└── wasmtime-v45.0.1-x86_64-linux-c-api/
    ├── include/
    │   └── wasmtime.h (and other headers)
    └── lib/
        ├── libwasmtime.a
        └── libwasmtime.so
```

#### Windows (MinGW64)

```bash
mkdir -p wasmtime
curl -L -o wasmtime-mingw.tar.xz \
  https://github.com/bytecodealliance/wasmtime/releases/download/v45.0.1/wasmtime-v45.0.1-x86_64-mingw-c-api.tar.xz
tar -xf wasmtime-mingw.tar.xz -C wasmtime/
rm wasmtime-mingw.tar.xz
```

Expected directory structure:

```
wasmtime/
└── wasmtime-v45.0.1-x86_64-mingw-c-api/
    ├── include/
    │   └── wasmtime.h
    └── lib/
        ├── libwasmtime.a
        └── wasmtime.dll
```

#### Android (Termux / aarch64)

```bash
mkdir -p wasmtime
curl -L -o wasmtime-android.tar.xz \
  https://github.com/bytecodealliance/wasmtime/releases/download/v45.0.1/wasmtime-v45.0.1-aarch64-android-c-api.tar.xz
tar -xf wasmtime-android.tar.xz -C wasmtime/
rm wasmtime-android.tar.xz
```

> **Note**: If you don't need WASM runtime support, you can skip wasmtime download. The build will fail without it on desktop platforms. For WebAssembly target (`make wasm`), wasmtime is NOT required.

### Step 2: Build

#### Linux

```bash
# Install dependencies (Debian/Ubuntu)
sudo apt-get install -y gcc make libssl-dev

# Build
make linux
```

#### Windows (MSYS2 MinGW64)

```bash
# Start MSYS2 MinGW64 terminal, then:
make mingw
```

If you need a static build (no DLL dependency):

```bash
make mingw-static
```

#### macOS

```bash
make macosx
```

#### Android (Termux)

```bash
pkg install clang make openssl lld
make termux
```

#### WebAssembly (Emscripten)

```bash
# Set EMSDK_PATH in Makefile or export EMCC/EMAR/EMRANLIB
make wasm

# Minimal version (no filesystem)
make wasm-minimal

# LSP server for WASM
make wasmlsp
```

### Step 3: Build Outputs

| File | Description |
| ---- | ----------- |
| `lxclua` / `lxclua.exe` | LXCLUA interpreter |
| `luac` / `luac.exe` | Lua bytecode compiler |
| `lbcdump` / `lbcdump.exe` | Bytecode analysis tool |
| `lxclua-lsp` / `lxclua-lsp.exe` | LSP language server |
| `liblua.a` | Static library |
| `lua55.dll` | Windows dynamic library |
| `lxclua.js` + `lxclua.wasm` | WebAssembly build (Emscripten) |

### Step 4: Verify

```bash
# Check version
./lxclua -v

# Run a quick test
./lxclua -e "print('Hello, LXCLUA-NCore!')"

# Verify WASM support
./lxclua -e "local wasmtime = require('wasmtime'); print('wasmtime loaded')"

# Run test suite (if available)
./lxclua test/quick_test.lua
```

### Platform-Specific Notes

| Platform | Notes |
| -------- | ----- |
| Linux | Links OpenSSL (`-lssl -lcrypto`). Binary stripped with `strip --strip-unneeded`. |
| Windows | Links WinCrypt (`-lcrypt32`). Stack size set to 16MB (`--stack,16777216`). |
| macOS | Links system readline and crypto libraries. |
| Termux | Uses Clang + LLD linker. Links OpenSSL. |
| WASM | JIT disabled (`LUA_NOJIT`). No wasmtime linkage. Uses wasm3 for WASM runtime. |

### Release Packaging

```bash
# Platform-specific release packages
make linux-release    # → lxclua-linux-x64-YYYYMMDD_HHMMSS.tar.gz
make mingw-release    # → lxclua-windows-x64-YYYYMMDD_HHMMSS.zip
make macos-release    # → lxclua-macos-YYYYMMDD_HHMMSS.tar.gz
make termux-release   # → lxclua-termux-YYYYMMDD_HHMMSS.tar.gz
make wasm-release     # → lxclua-wasm-YYYYMMDD_HHMMSS.zip

# Generic release (current platform)
make release          # → lxclua-YYYYMMDD_HHMMSS.tar.gz
```

### Clean

```bash
make clean
```

---

## 中文

### 前置条件

| 平台 | 编译器 | 所需库 |
| ---- | ------ | ------ |
| Linux x64 | GCC (gnu11) | `libssl-dev`、`libcrypto` |
| Windows (MinGW) | MSYS2 MinGW64 GCC | `mingw-w64-x86_64-gcc`、`mingw-w64-x86_64-ccache` |
| macOS | Clang | 系统库 |
| Android (Termux) | Clang (c23) | `openssl`、`lld` |
| WebAssembly | Emscripten 3.0+ | Emscripten SDK |

### 第一步：下载 wasmtime 预编译库

**LXCLUA-NCore 需要 wasmtime v45.0.1 C API** 来支持 WASM 运行时。预编译库**不包含在仓库中**，必须手动下载。

#### Linux x64

```bash
mkdir -p wasmtime
curl -L -o wasmtime-linux.tar.xz \
  https://github.com/bytecodealliance/wasmtime/releases/download/v45.0.1/wasmtime-v45.0.1-x86_64-linux-c-api.tar.xz
tar -xf wasmtime-linux.tar.xz -C wasmtime/
rm wasmtime-linux.tar.xz
```

解压后的目录结构：

```
wasmtime/
└── wasmtime-v45.0.1-x86_64-linux-c-api/
    ├── include/
    │   └── wasmtime.h（及其他头文件）
    └── lib/
        ├── libwasmtime.a
        └── libwasmtime.so
```

#### Windows (MinGW64)

```bash
mkdir -p wasmtime
curl -L -o wasmtime-mingw.tar.xz \
  https://github.com/bytecodealliance/wasmtime/releases/download/v45.0.1/wasmtime-v45.0.1-x86_64-mingw-c-api.tar.xz
tar -xf wasmtime-mingw.tar.xz -C wasmtime/
rm wasmtime-mingw.tar.xz
```

解压后的目录结构：

```
wasmtime/
└── wasmtime-v45.0.1-x86_64-mingw-c-api/
    ├── include/
    │   └── wasmtime.h
    └── lib/
        ├── libwasmtime.a
        └── wasmtime.dll
```

#### Android (Termux / aarch64)

```bash
mkdir -p wasmtime
curl -L -o wasmtime-android.tar.xz \
  https://github.com/bytecodealliance/wasmtime/releases/download/v45.0.1/wasmtime-v45.0.1-aarch64-android-c-api.tar.xz
tar -xf wasmtime-android.tar.xz -C wasmtime/
rm wasmtime-android.tar.xz
```

> **注意**：如果不需要 WASM 运行时支持，可以跳过 wasmtime 下载。桌面平台缺少 wasmtime 会导致构建失败。WebAssembly 目标（`make wasm`）不需要 wasmtime。

### 第二步：编译

#### Linux

```bash
# 安装依赖（Debian/Ubuntu）
sudo apt-get install -y gcc make libssl-dev

# 编译
make linux
```

#### Windows (MSYS2 MinGW64)

```bash
# 启动 MSYS2 MinGW64 终端，然后执行：
make mingw
```

如需静态编译（无 DLL 依赖）：

```bash
make mingw-static
```

#### macOS

```bash
make macosx
```

#### Android (Termux)

```bash
pkg install clang make openssl lld
make termux
```

#### WebAssembly (Emscripten)

```bash
# 在 Makefile 中设置 EMSDK_PATH，或导出 EMCC/EMAR/EMRANLIB
make wasm

# 最小版本（无文件系统）
make wasm-minimal

# WASM 版 LSP 服务器
make wasmlsp
```

### 第三步：编译产物

| 文件 | 说明 |
| ---- | ---- |
| `lxclua` / `lxclua.exe` | LXCLUA 解释器 |
| `luac` / `luac.exe` | Lua 字节码编译器 |
| `lbcdump` / `lbcdump.exe` | 字节码分析工具 |
| `lxclua-lsp` / `lxclua-lsp.exe` | LSP 语言服务器 |
| `liblua.a` | 静态库 |
| `lua55.dll` | Windows 动态库 |
| `lxclua.js` + `lxclua.wasm` | WebAssembly 构建产物（Emscripten） |

### 第四步：验证

```bash
# 查看版本
./lxclua -v

# 快速测试
./lxclua -e "print('Hello, LXCLUA-NCore!')"

# 验证 WASM 支持
./lxclua -e "local wasmtime = require('wasmtime'); print('wasmtime loaded')"

# 运行测试套件（如有）
./lxclua test/quick_test.lua
```

### 平台特定说明

| 平台 | 说明 |
| ---- | ---- |
| Linux | 链接 OpenSSL（`-lssl -lcrypto`），使用 `strip --strip-unneeded` 裁剪 |
| Windows | 链接 WinCrypt（`-lcrypt32`），栈大小 16MB（`--stack,16777216`） |
| macOS | 链接系统 readline 和加密库 |
| Termux | 使用 Clang + LLD 链接器，链接 OpenSSL |
| WASM | JIT 禁用（`LUA_NOJIT`），不链接 wasmtime，使用 wasm3 作为 WASM 运行时 |

### 发布打包

```bash
# 各平台发布包
make linux-release    # → lxclua-linux-x64-YYYYMMDD_HHMMSS.tar.gz
make mingw-release    # → lxclua-windows-x64-YYYYMMDD_HHMMSS.zip
make macos-release    # → lxclua-macos-YYYYMMDD_HHMMSS.tar.gz
make termux-release   # → lxclua-termux-YYYYMMDD_HHMMSS.tar.gz
make wasm-release     # → lxclua-wasm-YYYYMMDD_HHMMSS.zip

# 通用发布包（当前平台）
make release          # → lxclua-YYYYMMDD_HHMMSS.tar.gz
```

### 清理

```bash
make clean
```