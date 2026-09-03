# LXCLUA-NCore

> Enterprise-grade high-performance embedded scripting engine, deeply customized from Lua 5.5  
> Version: 505.8 | Source: 216 files / 173,231 lines of C | Rating: **A (Production)**

---

## Feature Highlights

- **15 primitive types** — nil, boolean, number, string, table, function, thread, struct, pointer, concept, namespace, map, and more
- **64-bit instruction set** — 400+ opcodes, 32768 registers, 6 instruction formats
- **Full OOP system** — class/interface/trait/abstract/singleton, C3 linearization multiple inheritance, public/protected/private access control
- **Modern syntax** — pipe operator `|>`, null coalescing `??`, optional chaining `?.`, spaceship `<=>`, string interpolation `${}`, lambda `||`, arrow functions `=>`, compound assignment, slicing
- **Async programming** — event loop (IOCP/epoll/kqueue), Promise/A+, async/await, thread pool
- **BigInt** — arbitrary precision integer arithmetic
- **PCRE2 regex** — regex literals `/pattern/`, global matching, substitution
- **Cryptography** — AES/RSA/ECC/SHA/MD5/CRC/Base64/CSPRNG
- **WASM runtime** — full Wasmtime bindings with fuel, epoch, serialization
- **Bytecode protection** — control flow flattening, basic block shuffling, VM instruction encryption, anti-debug
- **Native VM** — NLang 2.0 compiler, 37x performance gain (fib(30))
- **Lua to WASM** — experimental Lua-to-WebAssembly compilation pipeline
- **LBCTC** — bytecode-to-C compilation via TinyCC
- **LSP server** — code completion, diagnostics, go-to-definition

---

## Quick Start

```bash
# Run a script
lxclua.exe hello.lua

# Execute inline code
lxclua.exe -e "io.write('Hello, World!\n')"

# Interactive mode
lxclua.exe
```

```lua
-- hello.lua
io.write("Hello, LXCLUA-NCore!\n")

-- Modern syntax
local double = |x| -> x * 2
io.write(double(21), "\n")  -- 42

local msg = $"Hello, {name}!"
io.write(msg, "\n")

-- OOP
class Animal {
  function init(self, name) self.name = name end
  function speak(self) return $"Animal {self.name} speaks" end
}
local a = Animal("Dog")
io.write(a:speak(), "\n")
```

---

## Documentation Index

| Document | Description |
|----------|-------------|
| [docs/SYNTAX_REFERENCE.md](docs/SYNTAX_REFERENCE.md) | Complete syntax reference |
| [docs/TYPES_AND_STRUCTS.md](docs/TYPES_AND_STRUCTS.md) | Type system details |
| [docs/BUILTIN_LIBRARIES.md](docs/BUILTIN_LIBRARIES.md) | 30+ built-in library reference |
| [docs/OOP_SYSTEM.md](docs/OOP_SYSTEM.md) | Object-oriented system |
| [docs/MODULES.md](docs/MODULES.md) | Special modules (WASM, TCC, NativeVM, etc.) |
| [docs/API_REFERENCE.md](docs/API_REFERENCE.md) | C API reference |
| [docs/COMPILER_ARCHITECTURE.md](docs/COMPILER_ARCHITECTURE.md) | Compiler architecture |
| [docs/VM_ARCHITECTURE.md](docs/VM_ARCHITECTURE.md) | VM architecture |
| [docs/examples/](docs/examples/) | Runnable verified examples |
| [PROJECT_STATUS.md](PROJECT_STATUS.md) | Project maturity report |
| [DEEP_DIVE.md](DEEP_DIVE.md) | Technical deep dive |
| [EMBEDDING_GUIDE.md](EMBEDDING_GUIDE.md) | C/C++ embedding guide |
| [CONTRIBUTING.md](CONTRIBUTING.md) | Contributing guide |

---

## Building

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

### Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `BUILD_LUA` | ON | Build lxclua executable |
| `BUILD_LUA_LIB` | OFF | Build static library |
| `BUILD_LUA_DLL` | OFF | Build shared library |
| `BUILD_TESTS` | ON | Build tests |
| `ENABLE_LTO` | OFF | Link-time optimization |
| `ENABLE_WASM` | ON | WASM runtime support |
| `ENABLE_CRYPTO` | ON | Cryptography library |

---

## Maturity

- Overall rating: **A (Production)**
- Core engine: Stable
- OOP system: Stable
- Standard library: Stable
- Advanced modules: In development (WASM, LBCTC, NativeVM, LSP)
- See [PROJECT_STATUS.md](PROJECT_STATUS.md) for detailed module ratings

---

## License

LXCLUA-NCore is released under the MIT License. See [LICENSE](LICENSE) for details.