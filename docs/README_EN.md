# LXCLUA-NCore

[![License](https://img.shields.io/badge/License-MIT-blue.svg)](../LICENSE)
[![C Standard](https://img.shields.io/badge/C-C23-blue.svg)]()
[![Platform](https://img.shields.io/badge/Platform-Cross--Platform-green.svg)]()

English | [中文](README.md)

A high-performance embedded scripting engine based on **Lua 5.5 (Custom)** with enhanced security features, extended libraries, and optimized bytecode compilation.

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
| `liblua.a` / `lua55.dll` | Lua static library / dynamic library |
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

Issues and Pull Requests are welcome. Please refer to the [Contributing Guidelines](CONTRIBUTING.md).

## Contact

- **Email**: difierline@yeah.net

## Acknowledgments

- [Lua](https://www.lua.org/) - Original Lua language
- [wasm3](https://github.com/wasm3/wasm3) - WebAssembly interpreter