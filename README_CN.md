# LXCLUA-NCore

> 基于 Lua 5.5 深度定制的企业级高性能嵌入式脚本引擎  
> 版本: 505.8 | 源码: 216 文件 / 173,231 行 C | 综合评级: **A (生产级)**

---

## 特性速览

- **15 种基础类型** — nil, boolean, number, string, table, function, thread, struct, pointer, concept, namespace, map 等
- **64 位指令集** — 400+ 操作码，32768 个寄存器，6 种指令格式
- **完整 OOP 系统** — class/interface/trait/abstract/singleton，C3 线性化多继承，public/protected/private 访问控制
- **现代语法** — 管道运算符 `|>`、空值合并 `??`、可选链 `?.`、三路比较 `<=>`、字符串插值 `${}`、lambda `||`、箭头函数 `=>`、复合赋值、切片
- **异步编程** — 事件循环 (IOCP/epoll/kqueue)、Promise/A+、async/await、线程池
- **大整数** — 任意精度整数运算
- **PCRE2 正则** — 正则字面量 `/pattern/`、全局匹配、替换
- **加密库** — AES/RSA/ECC/SHA/MD5/CRC/Base64/CSPRNG
- **WASM 运行时** — 完整 Wasmtime 绑定，支持燃料/纪元/序列化
- **字节码保护** — 控制流扁平化、基本块洗牌、VM 指令加密、反调试
- **原生 VM** — NLang 2.0 编译器，性能提升 37x (fib(30))
- **Lua→WASM** — 实验性 Lua 到 WebAssembly 编译管线
- **LBCTC** — 字节码到 C 编译，通过 TCC 生成本机代码
- **LSP 服务器** — 代码补全、诊断、跳转

---

## 快速开始

```bash
# 运行脚本
lxclua.exe hello.lua

# 直接执行
lxclua.exe -e "io.write('Hello, World!\n')"

# 交互模式
lxclua.exe
```

```lua
-- hello.lua
io.write("Hello, LXCLUA-NCore!\n")

-- 现代语法
local double = |x| -> x * 2
io.write(double(21), "\n")  -- 42

local msg = $"Hello, {name}!"
io.write(msg, "\n")

-- 面向对象
class Animal {
  function init(self, name) self.name = name end
  function speak(self) return $"Animal {self.name} speaks" end
}
local a = Animal("Dog")
io.write(a:speak(), "\n")
```

---

## 文档索引

| 文档 | 说明 |
|------|------|
| [docs/SYNTAX_REFERENCE.md](docs/SYNTAX_REFERENCE.md) | 完整语法参考 |
| [docs/TYPES_AND_STRUCTS.md](docs/TYPES_AND_STRUCTS.md) | 类型系统详解 |
| [docs/BUILTIN_LIBRARIES.md](docs/BUILTIN_LIBRARIES.md) | 30+ 内置库函数参考 |
| [docs/OOP_SYSTEM.md](docs/OOP_SYSTEM.md) | 面向对象系统 |
| [docs/MODULES.md](docs/MODULES.md) | 特殊模块 (WASM, TCC, NativeVM 等) |
| [docs/API_REFERENCE.md](docs/API_REFERENCE.md) | C API 参考 |
| [docs/COMPILER_ARCHITECTURE.md](docs/COMPILER_ARCHITECTURE.md) | 编译器架构 |
| [docs/VM_ARCHITECTURE.md](docs/VM_ARCHITECTURE.md) | 虚拟机架构 |
| [docs/examples/](docs/examples/) | 可运行示例 (已验证) |
| [PROJECT_STATUS.md](PROJECT_STATUS.md) | 项目成熟度报告 |
| [DEEP_DIVE.md](DEEP_DIVE.md) | 技术深潜文档 |
| [EMBEDDING_GUIDE.md](EMBEDDING_GUIDE.md) | C/C++ 嵌入指南 |
| [CONTRIBUTING.md](CONTRIBUTING.md) | 贡献指南 |

---

## 构建

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

### 构建选项

| 选项 | 默认 | 说明 |
|------|------|------|
| `BUILD_LUA` | ON | 构建 lxclua 可执行文件 |
| `BUILD_LUA_LIB` | OFF | 构建静态库 |
| `BUILD_LUA_DLL` | OFF | 构建动态库 |
| `BUILD_TESTS` | ON | 构建测试 |
| `ENABLE_LTO` | OFF | 链接时优化 |
| `ENABLE_WASM` | ON | WASM 运行时 |
| `ENABLE_CRYPTO` | ON | 密码学库 |

---

## 许可证

LXCLUA-NCore 基于 MIT 许可证发布。详见 [LICENSE](LICENSE) 文件。

---

## 项目状态

- 综合评级: **A (生产级)**
- 核心引擎: 稳定
- OOP 系统: 稳定
- 标准库: 稳定
- 高级模块: 开发中 (WASM, LBCTC, NativeVM, LSP)
- 各模块成熟度详见 [PROJECT_STATUS.md](PROJECT_STATUS.md)