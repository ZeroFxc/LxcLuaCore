# LXCLUA-NCore

[![License](https://img.shields.io/badge/License-MIT-blue.svg)](../LICENSE)
[![C Standard](https://img.shields.io/badge/C-C23-blue.svg)]()
[![Platform](https://img.shields.io/badge/Platform-Cross--Platform-green.svg)]()

A high-performance embedded scripting engine based on **Lua 5.5 (Custom)** with enhanced security features, extended libraries, and optimized bytecode compilation.

[中文文档 (Chinese Documentation)](README_CN.md)

---

## Features

### Core Enhancements

- **Secure Compilation**: Dynamic OPcode mapping, timestamp encryption, SHA-256 integrity verification.
- **Custom VM**: Implements XCLUA instruction set with 64-bit instruction format and optimized dispatch.
- **Syntax Extensions**: Modern language features including Classes, Switch, Try-Catch, Arrow Functions, Pipe Operators, and more.
- **Shell-like Conditions**: Built-in support for shell-style test expressions (e.g., `[ -f "file.txt" ]`).
- **Code Obfuscation**: Control flow flattening, block shuffling, bogus blocks, VM protection, and string encryption.
- **Bytecode-to-C Code Generation (tcc)**: Converts Lua bytecode to C source code, enabling embedding in C projects or external compilation optimization.

### Extension Modules

| Module | require Name | Description |
|--------|-------------|-------------|
| `bit` / `bit32` | `require("bit")` | Bitwise operations |
| `bool` | `require("bool")` | Boolean enhancements |
| `userdata` | `require("userdata")` | Binary data serialization |
| `smgr` | `require("smgr")` | Memory management utilities |
| `process` | `require("process")` | Process management (Linux/Windows) |
| `http` | `require("http")` | HTTP client/server and Socket |
| `thread` | `require("thread")` | Multithreading with mutex, condition variables, and read-write locks |
| `fs` | `require("fs")` | File system operations |
| `struct` | `require("struct")` | C-style structs and arrays |
| `ptr` | `require("ptr")` | Pointer operations library |
| `vm` | `require("vm")` | VM introspection and bytecode manipulation |
| `jit` | `require("jit")` | Just-in-time compilation (sljit-based JIT) |
| `tcc` | `require("tcc")` | Bytecode-to-C code generation |
| `ByteCode` | `require("ByteCode")` | Bytecode manipulation and analysis |
| `vmprotect` | `require("vmprotect")` | VM-based code protection |
| `translator` | `require("translator")` | Code translation utilities |
| `crypto` | `require("crypto")` | Cryptographic library (SHA-256, AES, HMAC, CRC32, CSPRNG) |
| `uuid` | `require("uuid")` | UUID generation |
| `rsa` | `require("rsa")` | RSA asymmetric encryption |
| `ecc` | `require("ecc")` | ECC elliptic curve cryptography |
| `lexer` | `require("lexer")` | Lexer and AST manipulation |
| `asyncio` | `require("asyncio")` | Async I/O and Promises |
| `wasm3` | `require("wasm3")` | WebAssembly runtime (wasm3) |
| `wasmtime` | `require("wasmtime")` | WebAssembly runtime (wasmtime) |
| `lua2wasm` | `require("lua2wasm")` | Lua to WASM compiler |
| `quickjs` | `require("quickjs")` | QuickJS JavaScript engine integration |
| `vmcustom` | `require("vmcustom")` | Custom opcode extension system |
| `nativevm` | `require("nativevm")` | Native VM interface |
| `nativeparser` | `require("nativeparser")` | Native parser interface |
| `logtable` | `require("logtable")` | Log table support |
| `libc` | `require("libc")` | C standard library interface (MinGW) |

---

## Syntax Extensions

LXCLUA-NCore introduces modern language features to extend Lua 5.5.

> Implementation status legend: [Full] [Partial] [Not implemented]
>
> Statuses below are verified against actual `lparser.c`, `lcode.c`, `llex.c`, and `lvm.c` source code.

### 1. Extended Operators

Supports compound assignments, increment, spaceship operator, null coalescing, optional chaining, pipe operators, and walrus operator.

```lua
-- Compound Assignment [Full] (13 types: += -= *= /= //= %= &= |= ^= >>= <<= ..= ??=)
local a = 10
a += 5          -- a = 15

-- Increment [Partial: postfix var++ only, statement-level]
a++             -- a = 16

-- Decrement (--) not implemented: no TK_MINUSMINUS in lexer
-- Prefix ++var / --var not implemented
-- a--           -- syntax error
-- ++a           -- syntax error

-- Spaceship Operator [Full] (-1, 0, 1)
local cmp = 10 <=> 20  -- -1

-- Null Coalescing [Full] (includes ??= compound assignment)
local val = nil
local res = val ?? "default"  -- "default"

-- Optional Chaining [Full] (includes ?.() optional call)
local config = { server = { port = 8080 } }
local port = config?.server?.port  -- 8080
local timeout = config?.client?.timeout  -- nil

-- Pipe Operator [Full] (includes reverse pipe <|)
local function double(x) return x * 2 end
local result = 10 |> double  -- 20

-- Safe Pipe [Full] (skips if nil)
local maybe_nil = nil
local _ = maybe_nil |?> print  -- (does nothing)

-- Walrus Operator [Full] (Assignment Expression)
local x
if (x := 100) > 50 then
    print(x) -- 100
end
```

### 2. Enhanced Strings

- **Interpolation** [Full]: `${var}` or `${[expr]}` inside strings.
- **Raw Strings** [Full]: Prefixed with `_raw`, ignores escape sequences.

```lua
local name = "World"
print("Hello, ${name}!")  -- Hello, World!

local calc = "1 + 1 = ${[1+1]}"  -- 1 + 1 = 2

local path = _raw"C:\Windows\System32"
```

### 3. Function Features

Supports arrow functions, lambdas, C-style definitions, generics, and async/await.

```lua
-- Arrow Function [Full]
local add = (a, b) => a + b
local log = ->(msg) { print("[LOG]: " .. msg) }

-- Lambda Expression [Full]
local sq = lambda(x): x * x

-- C-style Function [Full]
int sum(int a, int b) {
    return a + b;
}

-- Generic Function [Full]
local function Factory(T)(val)
    return { type = T, value = val }
end
local obj = Factory("int")(99)

-- Async/Await [Full]
async function fetchData(url)
    local data = await(http.get(url))
    return data
end
```

### 4. Object-Oriented Programming (OOP)

Complete class and interface system with modifiers (`private`, `public`, `protected`, `static`, `final`, `abstract`, `sealed`) and properties (`get`/`set`).

```lua
interface Drawable  -- [Full]
    function draw(self)
end

class Shape implements Drawable  -- [Full]
    function draw(self)
        -- abstract-like behavior
    end
end

-- Sealed Class [Full] (cannot be extended)
sealed class Circle extends Shape
    private _radius = 0    -- [Full]
    protected _id = 0      -- [Full]

    function __init__(self, r)
        self._radius = r
    end

    -- Property with Getter/Setter [Full]
    get radius(self)
        return self._radius
    end

    set radius(self, v)
        if v >= 0 then self._radius = v end
    end

    function draw(self)
        super.draw(self)   -- [Full] super expression
        return "Drawing circle: " .. self._radius
    end

    static function create(r)  -- [Full]
        return new Circle(r)   -- [Full] new expression
    end
end

local c = Circle.create(10)
c.radius = 20
print(c.radius)  -- 20

-- instanceof check [Full] (equivalent to is operator)
if c instanceof Circle then
    print("c is a Circle")
end
```

### 5. Structs and Types

```lua
-- Struct [Full]
struct Point {
    int x;
    int y;
}
local p = Point()
p.x = 10

-- Concept (Type Predicate) [Full]
concept IsPositive(x)
    return x > 0
end

-- SuperStruct (Enhanced Table Definition) [Full]
superstruct MetaPoint [
    x: 0,
    y: 0,
    ["move"]: function(self, dx, dy)
        self.x = self.x + dx
        self.y = self.y + dy
    end
]

-- Enum [Full]
enum Color {
    Red,
    Green,
    Blue = 10
}

-- Destructuring [Full]
local data = { x = 1, y = 2 }
local take { x, y } = data

-- Array Destructuring [Full]
local arr = {10, 20, 30}
local take [first, , third] = arr

-- Spread Operator [Full]
local arr1 = {1, 2}
local arr2 = {3, 4}
local combined = { 0, ...arr1, ...arr2 }
```

### 6. Control Flow

```lua
-- Switch Statement [Full]
switch (val) do
    case 1:
        print("One")
        break
    default:
        print("Other")
end

-- When Statement [Full]
do
    when x == 1
        print("x is 1")
    case x == 10
        print("x is 10")
    else
        print("other")
end

-- Try-Catch-Finally [Full]
try
    error("Error")
catch(e)
    print("Caught: " .. e)
finally
    print("Cleanup")
end

-- Defer [Full]
defer do print("Executes at scope exit") end

-- With Statement [Full]
local ctx = { val = 10 }
with (ctx) {
    print(val) -- 10
}

-- Namespace and Using [Full]
namespace MyLib {
    function test() return "test" end
}
using namespace MyLib;
-- using MyLib::test;

-- Ternary Conditional Expression [Full]
local is_debug = true
local level = is_debug ? 10 : 0

-- List Comprehension [Full]
local src = {1, 2, 3, 4, 5}
local evens = [for _, v in ipairs(src) do v * 2 if v % 2 == 0]

-- Dict Comprehension [Full]
local dict = {a = 1, b = 2}
local inverted = {for k, v in pairs(dict) do v, k}

-- Continue Statement [Full]
for i = 1, 10 do
    if i % 2 == 0 then
        continue
    end
    print(i)
end
```

### 7. Shell-like Tests [Full]

Built-in conditional tests using `[ ... ]` syntax.

```lua
if [ -f "config.lua" ] then
    print("Config file exists")
end

if [ "a" == "a" ] then
    print("Strings match")
end

if [ 10 -gt 5 ] then
    print("10 > 5")
end
```

### 8. Metaprogramming and Macros

```lua
-- Custom Command [Full]
command echo(msg)
    print(msg)
end
echo "Hello World"

-- Custom Operator [Full]
operator ++ (x)
    return x + 1
end
local res = $$++(10)

-- Preprocessor Directives [Full]
$define DEBUG 1
$alias CONST_VAL = 100
$type MyInt = int

$if DEBUG
    print("Debug mode")
$else
    print("Release mode")
$end

$declare g_var: MyInt
```

### 9. Inline ASM [Full]

Write VM instructions directly. Use `newreg` to allocate registers safely.

```lua
asm(
    newreg r0
    LOADI r0 100

    rep 5 {
        ADDI r0 r0 1
    }

    _if 1
       _print "Compiling this block"
    _endif

    RETURN1 r0
)
```

### 10. Slice Operations [Full]

Python-style slice syntax for tables and strings.

```lua
local arr = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10}
local slice1 = arr[1:5]      -- {1, 2, 3, 4, 5}
local slice2 = arr[1:10:2]   -- {1, 3, 5, 7, 9}
local slice3 = arr[5:]       -- {5, 6, 7, 8, 9, 10}
local slice5 = arr[::-1]     -- {10, 9, 8, 7, 6, 5, 4, 3, 2, 1}
```

### 11. `in` Operator [Full]

Check if a value exists in a container.

```lua
local arr = {1, 2, 3, 4, 5}
if 3 in arr then
    print("3 is in arr")
end
```

### 12. Type Hints [Full]

Support for type annotations and type checking.

```lua
local function greet(name: string): string
    return "Hello, " .. name
end

local x: int = 10
local y: float = 3.14
local flag: bool = true
```

---

## Security Features

### Code Obfuscation

LXCLUA-NCore provides multiple obfuscation techniques:

| Flag | Description |
|------|-------------|
| `OBFUSCATE_CFF` | Control Flow Flattening |
| `OBFUSCATE_BLOCK_SHUFFLE` | Randomize basic block order |
| `OBFUSCATE_BOGUS_BLOCKS` | Insert bogus basic blocks |
| `OBFUSCATE_STATE_ENCODE` | Obfuscate state variable values |
| `OBFUSCATE_NESTED_DISPATCHER` | Multi-layered dispatcher |
| `OBFUSCATE_OPAQUE_PREDICATES` | Opaque predicates |
| `OBFUSCATE_FUNC_INTERLEAVE` | Function interleaving |
| `OBFUSCATE_VM_PROTECT` | VM protection (custom instruction set) |
| `OBFUSCATE_BINARY_DISPATCHER` | Binary search dispatcher |
| `OBFUSCATE_RANDOM_NOP` | Insert random NOP instructions |
| `OBFUSCATE_STR_ENCRYPT` | String constant encryption |

```lua
-- Obfuscate bytecode
local obfuscated = string.dump(func, false, OBFUSCATE_CFF | OBFUSCATE_STR_ENCRYPT)
```

### Cryptographic Library

Built-in unified cryptographic library `crypto` with a sub-table structure organizing all algorithms.

```lua
local crypto = require("crypto")

-- SHA-256 Hash
local hash = crypto.sha256("Hello World")         -- returns hex string
local raw = crypto.sha256.raw("Hello World")      -- returns raw bytes

-- HMAC-SHA256
local mac = crypto.hmac.sha256("key", "data")     -- returns hex string
local raw_mac = crypto.hmac.sha256_raw("key", "data")  -- returns raw bytes

-- AES Encryption (ECB/CBC/CTR modes)
local key = "16-byte-key-1234"
local iv = "initial-vector-16"                    -- required for CBC/CTR
local encrypted = crypto.aes.encrypt(key, plaintext, "CBC", iv)
local decrypted = crypto.aes.decrypt(key, encrypted, "CBC", iv)

-- CRC32 Checksum
local checksum = crypto.crc32(data)

-- Secure Random Numbers
local rand_hex = crypto.random.hex(16)            -- 16 bytes random hex
local rand_bytes = crypto.random.bytes(32)        -- 32 bytes random raw
local rand_int = crypto.random.int(1, 100)        -- [1, 100] random integer
crypto.random.seed(12345)                         -- manually set seed
```

### UUID Generation

```lua
local uuid = require("uuid")
local id = uuid.v4()          -- Random UUID (version 4)
local id2 = uuid.v7()         -- Time-ordered UUID (version 7)
```

---

## Threading Support

Full multithreading support with synchronization primitives.

```lua
local thread = require("thread")

-- Mutex
local m = thread.mutex()
m:lock()
-- critical section
m:unlock()

-- Condition Variable
local cond = thread.cond()
cond:wait(m)
cond:signal()
cond:broadcast()

-- Read-Write Lock
local rwlock = thread.rwlock()
rwlock:rdlock()  -- read lock
rwlock:wrlock()  -- write lock
rwlock:unlock()

-- Thread Creation
local t = thread.create(function()
    print("Running in thread")
end)
t:join()
```

---

## Bytecode-to-C Code Generation (tcc)

The `tcc` module compiles Lua source code to bytecode, then converts bytecode to C source code. The generated C code can be compiled separately with an external C compiler into native executables or shared libraries. The module name is a historical artifact and is unrelated to Tiny C Compiler.

```lua
local tcc = require("tcc")

-- Convert Lua code to C source code
local c_code = tcc.compile([[
    local function add(a, b)
        return a + b
    end
    return add
]])

-- c_code is a C source code string, can be written to file and compiled with GCC
```

### Real JIT Compilation (`jit`)

The project includes a separate `jit` module, based on sljit, which provides actual just-in-time compilation (JIT) at runtime -- compiling hot bytecode directly into native machine code:

```lua
local jit = require("jit")
jit.on()   -- enable JIT
jit.off()  -- disable JIT
print(jit.status())  -- check JIT status
```

---

## Extended Types

LXCLUA-NCore extends Lua with additional types:

| Type | Description |
|------|-------------|
| `LUA_TSTRUCT` | C-style struct |
| `LUA_TPOINTER` | Raw pointer type |
| `LUA_TCONCEPT` | Type predicate concept |
| `LUA_TNAMESPACE` | Namespace type |
| `LUA_TSUPERSTRUCT` | Enhanced table definition |

---

## Big Integer Support

Arbitrary precision integer arithmetic.

```lua
local bigint = require("bigint")

local a = bigint.new("12345678901234567890")
local b = bigint.new("98765432109876543210")
local c = a + b
print(c:tostring())  -- 111111111011111111100
```

---

## HTTP and Networking

```lua
local http = require("http")

-- HTTP GET
local response = http.get("https://api.example.com/data")
print(response.body)

-- HTTP POST
local result = http.post("https://api.example.com/submit", {
    headers = { ["Content-Type"] = "application/json" },
    body = '{"key": "value"}'
})

-- Socket operations
local sock = http.socket()
sock:connect("example.com", 80)
sock:send("GET / HTTP/1.0\r\n\r\n")
local data = sock:recv(1024)
sock:close()
```

---

## File System Operations

```lua
local fs = require("fs")

-- File operations
local content = fs.read("file.txt")
fs.write("output.txt", "Hello World")
fs.append("log.txt", "New entry\n")

-- Directory operations
local files = fs.listdir("/path/to/dir")
fs.mkdir("/path/to/new/dir")
fs.rmdir("/path/to/dir")

-- Path utilities
local exists = fs.exists("file.txt")
local is_dir = fs.isdir("path")
local is_file = fs.isfile("file.txt")
local size = fs.size("file.txt")
```

---

## Bytecode Manipulation

```lua
local bytecode = require("ByteCode")

-- Dump function to bytecode
local bc = bytecode.dump(function() print("Hello") end)

-- Load bytecode back
local func = bytecode.load(bc)

-- Analyze bytecode
local info = bytecode.analyze(func)
print("Instructions:", info.num_instructions)
print("Constants:", info.num_constants)
```

---

## Build and Test

### Build

```bash
# Linux
make linux

# Windows (MinGW)
make mingw

# Windows static (MinGW)
make mingw-static

# Android (Termux)
make termux

# WebAssembly (main)
make wasm

# WebAssembly (LSP server)
make wasmlsp

# WebAssembly (minimal)
make wasm-minimal

# WebAssembly (C library)
make wasm-c
```

### Build Outputs

| File | Description |
|------|-------------|
| `lxclua` / `lxclua.exe` | LXCLUA interpreter |
| `luac` / `luac.exe` | Lua bytecode compiler |
| `lbcdump` / `lbcdump.exe` | Bytecode analysis tool |
| `lxclua-lsp.exe` | LXCLUA LSP language server |
| `liblua.a` / `lua55.dll` | Lua static library / dynamic library |
| `lxclua.js` / `lxclua.wasm` | WebAssembly build outputs |
| `lxclua-lsp.js` / `lxclua-lsp.wasm` | LSP WebAssembly build outputs |

### Verification

Run the test suite to verify all features:

```bash
./lxclua tests/verify_docs_full.lua
./lxclua tests/test_parser_features.lua
./lxclua tests/test_advanced_parser.lua
```

### Clean

```bash
make clean
```

---

## API Reference

### Enhanced Memory API

```c
size_t lua_getmemoryusage(lua_State *L);
void   lua_gc_force(lua_State *L);
void   lua_table_iextend(lua_State *L, int idx, int n);
```

### Obfuscation API

```c
int lua_dump_obfuscated(lua_State *L, lua_Writer writer, void *data,
                        int strip, int obfuscate_flags, unsigned int seed,
                        const char *log_path);
```

---

## License

[MIT License](../LICENSE).
Lua original code Copyright (c) PUC-Rio.