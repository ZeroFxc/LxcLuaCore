# LXCLUA-NCore

> 基于 Lua 5.5 深度定制的企业级高性能嵌入式脚本引擎  
> 版本: 505.8 | 源码: 216 文件 / 173,231 行 C | 综合评级: **A (生产级)**

## 快速开始

```bash
lxclua.exe -e "io.write('Hello, LXCLUA-NCore!\n')"
lxclua.exe hello.lua
```

## 文档

| 文档 | 说明 |
|------|------|
| [docs/README.md](docs/README.md) | 完整文档索引与快速入门 |
| [docs/SYNTAX_REFERENCE.md](docs/SYNTAX_REFERENCE.md) | 语法参考 |
| [docs/BUILTIN_LIBRARIES.md](docs/BUILTIN_LIBRARIES.md) | 内置库参考 |
| [docs/OOP_SYSTEM.md](docs/OOP_SYSTEM.md) | 面向对象系统 |
| [docs/API_REFERENCE.md](docs/API_REFERENCE.md) | C API 参考 |
| [docs/COMPILER_ARCHITECTURE.md](docs/COMPILER_ARCHITECTURE.md) | 编译器架构 |
| [docs/VM_ARCHITECTURE.md](docs/VM_ARCHITECTURE.md) | 虚拟机架构 |
| [docs/MODULES.md](docs/MODULES.md) | 特殊模块 |
| [PROJECT_STATUS.md](PROJECT_STATUS.md) | 项目成熟度报告 |
| [DEEP_DIVE.md](DEEP_DIVE.md) | 技术深潜 |
| [EMBEDDING_GUIDE.md](EMBEDDING_GUIDE.md) | C/C++ 嵌入指南 |
| [CONTRIBUTING.md](CONTRIBUTING.md) | 贡献指南 |
| [README_CN.md](README_CN.md) | 中文 README |
| [README_EN.md](README_EN.md) | English README |

## 构建

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

## 许可证

MIT License — 详见 [LICENSE](LICENSE)