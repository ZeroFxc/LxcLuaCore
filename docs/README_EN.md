# LXCLUA-NCore

[![License](https://img.shields.io/badge/License-MIT-blue.svg)](../LICENSE)
[![C Standard](https://img.shields.io/badge/C-C23-blue.svg)]()
[![Platform](https://img.shields.io/badge/Platform-Cross--Platform-green.svg)]()
[![Code Size](https://img.shields.io/badge/Code-~160K--lines-green)]()
[![Maturity](https://img.shields.io/badge/Maturity-Production--Grade-brightgreen)](PROJECT_STATUS.md)
[![Documentation](https://img.shields.io/badge/Docs-Complete-blue)](docs/)

English | [中文](README.md)

A high-performance embedded scripting engine based on **Lua 5.5 (Custom)** with enhanced security features, extended libraries, and optimized bytecode compilation.

> **Project Maturity: A Grade** — This is a production-level Lua engine fork with ~160K lines of C code across 85 source files, not a "toy project". See [Project Status Report](docs/PROJECT_STATUS.md) for a detailed assessment based on thorough code analysis.

## Tested Platforms

| Platform | Status | Bytecode Interop |
|----------|--------|------------------|
| Windows (MinGW) | Passed | Supported |
| Arch Linux | Passed | Supported |
| Ubuntu | Passed | Supported |
| Android (Termux) | Passed | Supported |
| Android (LXCLUA JNI) | Passed | Supported |
| WebAssembly (Emscripten) | Passed | Supported |

## Features

### Core Enhancements

- **Secure Compilation** - Bytecode uses dynamic OPcode mapping, timestamp encryption, and SHA-256 integrity verification
- **Anti-Reverse Protection** - Multi-layer encryption mechanisms effectively prevent decompilation and tampering
- **Syntax Extensions** - Modern language features: OOP (classes, interfaces), generics, async/await, pipe operators, optional chaining, null coalescing, and more
- **Code Obfuscation** - Control flow flattening, block shuffling, VM protection, and string encryption
- **Bytecode-to-C Code Generation (tcc)** - Converts Lua bytecode to C source code for external compilation
- **JIT Compilation (jit)** - Real JIT compilation via sljit, compiling hot bytecode to native machine code at runtime

### Extension Modules

| Module | Description |
|--------|-------------|
| `crypto` | Unified cryptographic library (SHA-256, AES, HMAC, CRC32, CSPRNG) |
| `uuid` | UUID generation (v4, v7) |
| `rsa` | RSA asymmetric encryption |
| `ecc` | ECC elliptic curve cryptography (ECDSA, ECDH) |
| `bit` / `bit32` | Bitwise operations |
| `struct` | C-style structs and arrays |
| `ptr` | Pointer operations library |
| `thread` | Multithreading with mutex, condition variables, and read-write locks |
| `http` | HTTP client/server and Socket |
| `fs` | File system operations |
| `process` | Process management |
| `vm` | VM introspection and bytecode manipulation |
| `tcc` | Bytecode-to-C code generation |
| `ByteCode` | Bytecode manipulation and analysis |
| `vmprotect` | VM-based code protection |
| `lexer` | Lexer and AST manipulation |
| `asyncio` | Async I/O and Promises |
| `wasm3` | WebAssembly runtime (wasm3) |
| `wasmtime` | WebAssembly runtime (wasmtime) |
| `lua2wasm` | Lua to WASM compiler |
| `quickjs` | QuickJS JavaScript engine integration |
| `vmcustom` | Custom opcode extension system |
| `translator` | Code translation utilities |

### Compilation Optimizations

- Compiled with C23 standard
- LTO (Link-Time Optimization)
- Loop unrolling and strict aliasing analysis
- Debug symbols stripped for minimal binary size

## System Requirements

- **Compiler**: GCC or Clang (with C11/C23 standard support)
- **Platform**: Windows / Linux / Android (Termux) / WebAssembly (Emscripten)

## Quick Start

### Build

```bash
# Windows (MinGW)
make mingw

# Linux
make linux

# Android (Termux)
make termux

# WebAssembly
make wasm
```

### Verify Installation

```bash
make test
```

### Clean Build

```bash
make clean
```

## Build Outputs

| File | Description |
|------|-------------|
| `lxclua` / `lxclua.exe` | LXCLUA interpreter |
| `luac` / `luac.exe` | Lua bytecode compiler |
| `lbcdump` / `lbcdump.exe` | Bytecode analysis tool |
| `lxclua-lsp.exe` | LXCLUA LSP language server |
| `liblxclua.a` / `lxclua.dll` | Lua static library / dynamic library |
| `lxclua.js` / `lxclua.wasm` | WebAssembly build outputs |

## Usage Examples

### Run Lua Script

```bash
./lxclua script.lua
```

### Compile to Bytecode

```bash
./luac -o output.luac script.lua
```

### Embed in C/C++ Project

```c
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

int main() {
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);

    luaL_dofile(L, "script.lua");

    lua_close(L);
    return 0;
}
```

## Project Structure

```
.
├── src/
│   ├── core/         # Lua core (lapi, lcode, ldebug, ldo, lgc, lobject, lparser, lstate, lstring, ltable, ltm, lvm, etc.)
│   ├── compiler/     # Compiler extensions (lbctc - bytecode-to-C, llex - lexer, lparser extensions)
│   ├── stdlib/       # Standard libraries (lbaselib, lcorolib, liolib, lmathlib, loslib, lstrlib, ltablib, etc.)
│   ├── utils/        # Utility modules (http, fs, crypto, thread, struct, uuid, rsa, ecc, bigint, etc.)
│   └── lspsrv/       # LSP server implementation
├── tests/            # Test suites
├── docs/             # Documentation
├── Makefile          # Build script
└── LICENSE           # MIT License
```

## Security Notes

The bytecode compilation in this project employs multiple security mechanisms:

1. **Dynamic OPcode Mapping** - Generates unique instruction mapping table for each compilation
2. **Timestamp Encryption** - Uses compilation time as encryption key
3. **SHA-256 Verification** - Ensures bytecode integrity

> Note: These protective measures are designed to increase reverse engineering difficulty but cannot guarantee absolute security.

## License

This project is open-sourced under the [MIT License](../LICENSE).

Original Lua code is copyrighted by PUC-Rio. See [Lua License](https://www.lua.org/license.html).

## Contributing

Issues and Pull Requests are welcome. Please refer to the [Contributing Guidelines](docs/CONTRIBUTING.md).

## Documentation Index

### Getting Started
- [Chinese README](docs/README_CN.md) — Comprehensive syntax reference and feature overview
- [Build Guide](docs/BUILD.md) — Compilation and build instructions
- [Tutorial](docs/TUTORIAL.md) — Quick start and basic usage

### Architecture & Design
- [Architecture Overview](docs/ARCHITECTURE.md) — System architecture and module relationships
- [Module Details](docs/MODULES.md) — Detailed module documentation
- [Project Status Report](docs/PROJECT_STATUS.md) — Objective assessment based on full source analysis

### Reference Manuals
- [Syntax Reference](docs/SYNTAX_REFERENCE.md) — Complete syntax feature documentation verified against source code
- [API Reference](docs/API_REFERENCE.md) — Complete function API listing for all standard and extension libraries
- [Lua API Reference](docs/LUA_API.md) — Full Lua C API reference

### Technical Deep Dives
- [Deep Dive](docs/DEEP_DIVE.md) — Technical deep dive into 6 core subsystems: 64-bit instruction format, OOP, obfuscation, crypto, NativeVM, lua2wasm
- [WASM Runtime](docs/WASM_RUNTIME.md) — WebAssembly runtime integration
- [LSP Server](docs/LSP_SERVER.md) — Language Server Protocol implementation
- [AST System](docs/AST_SYSTEM.md) — Abstract syntax tree structure and operations

### Integration & Embedding
- [Embedding Guide](docs/EMBEDDING_GUIDE.md) — Complete guide for embedding LXCLUA in C/C++ projects
- [Async Programming](docs/NATIVE_ASYNC_AWAIT.md) — async/await native support

### Community
- [Code of Conduct](docs/CODE_OF_CONDUCT.md) — Community participation guidelines
- [Security Notes](docs/SECURITY.md) — Security features and considerations
- [Inline Assembly (CN)](docs/ASM_TUTORIAL_CN.md) / [(EN)](docs/ASM_TUTORIAL.md)
- [GC Analysis](docs/REPORT_GC_ANALYSIS.md) — Garbage collector performance analysis
- [Development Plan](docs/PLAN.md) — Project status and future roadmap

## Contact

- **Email**: difierline@yeah.net

## Acknowledgments

- [Lua](https://www.lua.org/) - Original Lua language
- [wasm3](https://github.com/wasm3/wasm3) - WebAssembly interpreter