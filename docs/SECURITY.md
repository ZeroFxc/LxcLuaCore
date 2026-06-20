# Security Policy / 安全策略

[English](#english) | [中文](#中文)

---

## English

### Supported Versions

| Version | Supported          | Security Patches    |
| ------- | ------------------ | ------------------- |
| 2.x     | :white_check_mark: | Active              |
| 1.x     | :x:                | End of Life         |

### Reporting a Vulnerability

If you discover a security vulnerability in **LXCLUA-NCore**, please **do NOT** open a public issue.

| Method | Details |
| ------ | ------- |
| GitHub Private Report | [Report a vulnerability](https://github.com/ZeroFxc/LxcLuaCore/security/advisories/new) (if enabled) |
| Email | **difierline@yeah.net** |
| PGP | Not available at this time |

Please include in your report:

1. **Affected module(s)** — which source files / components are impacted
2. **Description** — clear description of the vulnerability
3. **Reproduction steps** — minimal code or build steps
4. **Impact assessment** — what an attacker could achieve (RCE, DoS, information disclosure, privilege escalation, bytecode extraction)
5. **Affected versions** — version(s) and platform(s)
6. **Suggested fix** — (optional) proposed patch

### Scope of Concern

The following components are within the security scope of this project:

| Component | Source Files | Risk Profile |
| --------- | ------------ | ------------ |
| Core VM | `lvm.c`, `lparser.c`, `llex.c`, `lgc.c` | Memory corruption, sandbox escape |
| Bytecode Obfuscation | `lobfuscate.c` | Integrity bypass, deobfuscation |
| Cryptography | `aes.c`, `sha256.c`, `lrsa.c`, `lecc.c`, `csprng.c`, `lcrypto.c` | Weak crypto, side-channel |
| JIT Compiler | `src/vm/jit/` | Code injection, memory safety |
| WASM Runtime | `lwasm3.c`, `lwasmtime.c`, `m3_*.c` | Sandbox escape |
| QuickJS | `lquickjs.c`, `quickjs/` | JS engine vulnerabilities |
| PCRE2 | `pcre2/` | ReDoS, buffer overflow |
| HTTP / Networking | `libhttp.c` | SSRF, injection |
| Multi-threading | `lthread.c`, `lthreadlib.c` | Race conditions, deadlocks |
| LSP Server | `src/lspsrv/` | DoS via crafted input |
| Lua-to-WASM | `src/lua2wasm/` | Code generation bugs |

### Response Timeline

| Severity | Examples | Initial Response | Resolution Target |
| -------- | -------- | ---------------- | ----------------- |
| Critical | RCE via crafted bytecode, VM sandbox escape | 24 hours | 72 hours |
| High | Information leak, crypto weakness, auth bypass | 48 hours | 7 days |
| Medium | DoS via malformed input, memory leak | 72 hours | 14 days |
| Low | Minor issues, defense-in-depth | 5 days | 30 days |

### Disclosure Policy

- **Coordinated disclosure**: fixes released before public disclosure
- Reporter credited in release notes (unless anonymity requested)
- [GitHub Security Advisory](https://github.com/ZeroFxc/LxcLuaCore/security/advisories) published post-fix

### Security Considerations

#### Bytecode Protection

The obfuscation engine (`OBFUSCATE_CFF`, `OBFUSCATE_STR_ENCRYPT`, `OBFUSCATE_VM_PROTECT`, etc.) increases reverse engineering difficulty but **does not guarantee absolute security** against a determined attacker with full runtime binary access.

#### Memory Safety

LXCLUA-NCore is written in C (C23). Modules `require("ptr")` and `require("struct")` provide raw pointer access that **bypasses Lua's memory safety**. Misuse can cause crashes, memory corruption, or undefined behavior.

#### WASM Sandbox

`wasm3` and `wasmtime` execute WebAssembly in-process. Bugs in the WASM runtime or host bindings may allow sandbox escape. Report any WASM-related crashes.

#### Cryptography

Cryptographic modules — `crypto` (SHA-256, AES, HMAC, CSPRNG, CRC32), `rsa`, `ecc`, `uuid` — should be used for all security-sensitive operations. **Always use `crypto.random.*` (CSPRNG)** instead of `math.random()`.

---

## 中文

### 支持的版本

| 版本 | 支持状态           | 安全补丁            |
| ---- | ------------------ | ------------------- |
| 2.x  | :white_check_mark: | 活跃维护            |
| 1.x  | :x:                | 已停止支持          |

### 报告漏洞

如在 **LXCLUA-NCore** 中发现安全漏洞，请**勿**创建公开 Issue。

| 方式 | 详情 |
| ---- | ---- |
| GitHub 私人报告 | [报告漏洞](https://github.com/ZeroFxc/LxcLuaCore/security/advisories/new)（如已启用） |
| 邮件 | **difierline@yeah.net** |
| PGP 密钥 | 暂未提供 |

请包含以下信息：

1. **受影响模块** — 涉及哪些源文件/组件
2. **漏洞描述** — 清晰描述漏洞详情
3. **复现步骤** — 最小代码或构建步骤
4. **影响评估** — 攻击者可实现的目标（RCE、DoS、信息泄露、权限提升、字节码提取）
5. **受影响版本** — 版本和平台
6. **修复建议** — （可选）建议的补丁

### 安全范围

以下组件在本项目的安全审核范围内：

| 组件 | 源文件 | 风险类型 |
| ---- | ------ | -------- |
| 核心 VM | `lvm.c`、`lparser.c`、`llex.c`、`lgc.c` | 内存损坏、沙箱逃逸 |
| 字节码混淆 | `lobfuscate.c` | 完整性绕过、去混淆 |
| 加密 | `aes.c`、`sha256.c`、`lrsa.c`、`lecc.c`、`csprng.c`、`lcrypto.c` | 弱加密、侧信道 |
| JIT 编译器 | `src/vm/jit/` | 代码注入、内存安全 |
| WASM 运行时 | `lwasm3.c`、`lwasmtime.c`、`m3_*.c` | 沙箱逃逸 |
| QuickJS | `lquickjs.c`、`quickjs/` | JS 引擎漏洞 |
| PCRE2 | `pcre2/` | ReDoS、缓冲区溢出 |
| HTTP / 网络 | `libhttp.c` | SSRF、注入 |
| 多线程 | `lthread.c`、`lthreadlib.c` | 竞态条件、死锁 |
| LSP 服务器 | `src/lspsrv/` | 构造输入导致 DoS |
| Lua-to-WASM | `src/lua2wasm/` | 代码生成缺陷 |

### 响应时间线

| 严重程度 | 示例 | 首次响应 | 解决目标 |
| -------- | ---- | -------- | -------- |
| 严重 | 通过构造字节码实现 RCE，VM 沙箱逃逸 | 24 小时 | 72 小时 |
| 高危 | 信息泄露、加密弱点、认证绕过 | 48 小时 | 7 天 |
| 中危 | 畸形输入导致 DoS、内存泄漏 | 72 小时 | 14 天 |
| 低危 | 次要问题、纵深防御 | 5 天 | 30 天 |

### 披露政策

- **协调披露**：修复先于公开披露
- 报告者将在发布说明中致谢（除非要求匿名）
- 修复后发布 [GitHub 安全公告](https://github.com/ZeroFxc/LxcLuaCore/security/advisories)

### 安全注意事项

#### 字节码保护

混淆引擎（`OBFUSCATE_CFF`、`OBFUSCATE_STR_ENCRYPT`、`OBFUSCATE_VM_PROTECT` 等）增加了逆向工程难度，但**不能保证**对拥有运行时二进制完全访问权限的攻击者提供绝对安全。

#### 内存安全

LXCLUA-NCore 使用 C 语言（C23）编写。`require("ptr")` 和 `require("struct")` 提供原始指针访问，**绕过 Lua 的内存安全保证**。误用可能导致崩溃、内存损坏或未定义行为。

#### WASM 沙箱

`wasm3` 和 `wasmtime` 在进程内执行 WebAssembly。WASM 运行时或宿主绑定中的 bug 可能导致沙箱逃逸。请报告任何 WASM 相关异常。

#### 加密

使用加密模块 — `crypto`（SHA-256、AES、HMAC、CSPRNG、CRC32）、`rsa`、`ecc`、`uuid` — 处理所有安全敏感操作。**始终使用 `crypto.random.*`（CSPRNG）**，而非 `math.random()`。