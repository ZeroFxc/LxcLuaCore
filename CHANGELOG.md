# LXCLUA-NCore 版本变更日志

> 本文档记录 LXCLUA-NCore 项目的版本历史和变更内容。格式遵循 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.0.0/) 规范。

---

## [Unreleased] - 开发中

### 文档完善（本次更新）

#### 新增
- **PROJECT_STATUS.md** — 项目状态与成熟度报告，基于对全部 160,000+ 行源码的逐文件客观评估：
  - 6 大模块层、30+ 子模块的五星制成熟度评级
  - 完整的指令格式、类型系统、软关键字系统、OOP 元数据键、混淆模式、NativeVM 指令集等技术规格
  - 已知限制清单（基于代码验证）
  - 综合评级 **A**（生产级别 Lua 引擎）
- **DEEP_DIVE.md** — 技术深潜文档：
  - 64 位自定义指令格式（含扰乱层设计动机和编解码宏详解）
  - OOP 系统实现（C3 线性化算法、Trait 混入、访问控制、Super 编译）
  - 混淆引擎架构（CFF 变换流程、不透明谓词数学原理、VM 保护模式）
  - 密码学库实现（ChaCha20 CSPRNG、RSA 大整数、ECC secp256k1、AES）
  - NativeVM 原生虚拟机（两阶段汇编、寄存器系统、NLang 2.0 编译器）
  - Lua-to-WASM 编译管线（28 个 host 回调详解、端到端流程）
- **EMBEDDING_GUIDE.md** — C/C++ 嵌入指南：
  - 最小嵌入示例和编译命令
  - 自定义分配器（内存限制沙箱）
  - C 函数暴露、C++ 类包装
  - lua_pcall 错误处理和 xpcall 用法
  - 完整的游戏脚本引擎示例
- **CODE_OF_CONDUCT.md** — 社区行为准则（Contributor Covenant 2.1 中文版）
- **CONTRIBUTING.md（重写）** — 全面重写贡献指南：
  - 项目架构导览
  - 代码风格规范（命名、注释、头文件模板）
  - 内存管理 5 条规则
  - 平台移植性要求（6 个目标平台、字节序、整数大小）
  - 构建系统变量表、新文件接入流程
  - 性能基准测试要求、测试文件编写规范
  - 发布流程（语义化版本）

#### 改进
- **README_CN.md** — 添加文档导航索引，链接到新增文档
- **README_EN.md** — 添加成熟度徽章和完整文档索引
- **README.md** — 添加成熟度徽章和项目成熟度声明

---

## [2.0.0] - 2026-08 (计划/近期)

### 新增
- 完整 OOP 系统（class/interface/trait/sealed/singleton，C3 线性化 MRO）
- 原生 NativeVM 虚拟机（独立指令集、40+ 操作码、两遍汇编器）
- NLang 2.0 编译器（Lua-like 语言 → NativeVM 字节码）
- Lua2WASM 编译器（Lua 源码 → WebAssembly 模块）
- wasmtime GC + externref 绑定（28 个 host 回调）
- QuickJS 引擎集成
- SHA-256 / AES / HMAC / CRC32 密码算法库
- RSA 自定义大整数实现（Barrett 约简，Miller-Rabin）
- ECC secp256k1 椭圆曲线（ECDSA、ECDH、公钥恢复）
- ChaCha20 CSPRNG 安全随机数
- 异步 I/O 系统（跨平台事件循环：epoll/kqueue/IOCP/select）
- Promise 实现
- 多线程支持（mutex/cond/rwlock/channel）
- HTTP 客户端/服务端、WebSocket、URL/Base64
- LSP 语言服务器

---

## [1.5.0] - 年内更早（计划/历史重构）

### 新增
- 软关键字系统（80+ 关键字，哈希 + 上下文位掩码快速查找）
- 复合赋值运算符扩展（??=）
- Shell 风格测试表达式（`[ -f file ]`）
- 控制流扁平化混淆（CFF，11 种混淆模式）
- VM 保护模式（bytecode → Lua function 转换）
- 自定义操作码注册系统（vmcustom 模块）
- 字节码转 C 源码生成器（tcc 模块）

---

## [1.0.0] - 项目早期

### 新增
- Lua 5.5 基础运行时 fork
- 64 位自定义指令格式（扰乱布局）
- 15 种 Lua 类型扩展
- MAXVARS=512 寄存器扩展
- 复合赋值运算符（+=, -=, *=, /=, //=, %=, &=, |=, ^=, >>=, <<=, ..=）
- 太空船操作符（<=>）
- 空值合并（??）
- 管道操作符（|>, <|, |?>）
- 可选链（?.）
- 海象操作符（:=）
- 字符串插值（${var}）
- 原生字符串（_raw 前缀）
- 箭头函数（=>）
- Lambda 表达式
- 三元条件表达式（?:）
- Switch 语句
- When 语句
- Try-Catch-Finally
- Defer 语句
- With 环境切换
- Namespace 和 Using
- 泛型函数
- Async/Await 语法糖
- 列表/字典推导式
- 解构赋值（take）
- 类型提示
- C 风格函数定义
- 切片操作（Python 风格）
- In 操作符
- Is/instanceof 操作符
- 自定义命令（command）
- 自定义操作符（operator，$$ 前缀调用）
- 预处理器指令（$define, $alias, $type, $if, $include 等）
- 内联汇编（asm 块，newreg 安全寄存器分配）
- 字节码签名（SHA-256 + 时间戳加密 + 动态 opcode remapping）

---

## 附录：版本号规范

本项目采用 [语义化版本](https://semver.org/lang/zh-CN/) 规范：

```
MAJOR.MINOR.PATCH

MAJOR — 不兼容的 API 变更
MINOR — 向后兼容的功能新增
PATCH — 向后兼容的 Bug 修复
```

### 兼容性说明

- **字节码兼容性**：`.luac` 文件跨 MAJOR 版本不兼容（动态 opcode 映射表变化）
- **源码兼容性**：Lua 5.5 语法完全兼容；扩展语法在 MINOR 版本内向后兼容
- **C API 兼容性**：标准 Lua C API 完全兼容；扩展 API 在 MINOR 版本内向后兼容

### 平台支持状态

| 平台 | 构建状态 | 运行状态 | 备注 |
|------|----------|----------|------|
| Linux x86_64 | ✅ | ✅ | 主力开发平台 |
| Windows x86_64 (MinGW) | ✅ | ✅ | CI 验证 |
| macOS (Apple Silicon) | ✅ | ✅ | 社区验证 |
| Android ARM64 (Termux) | ✅ | ✅ | JNI 嵌入支持 |
| Android ARM64 (LXCLUA) | ✅ | ✅ | 蓝牙调试 |
| WebAssembly (Emscripten) | ✅ | ✅ | wasm3 运行时 |
