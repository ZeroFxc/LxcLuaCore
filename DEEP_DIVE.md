# LXCLUA-NCore 技术深潜

> 核心子系统实现详解，适合编译器工程师、安全研究员和高级 Lua 开发者阅读。

---

## 目录

1. [64 位自定义指令集](#1-64-位自定义指令集)
2. [OOP 系统实现](#2-oop-系统实现)
3. [混淆引擎架构](#3-混淆引擎架构)
4. [密码学库实现](#4-密码学库实现)
5. [NativeVM 原生虚拟机](#5-nativevm-原生虚拟机)
6. [Lua-to-WASM 编译管线](#6-lua-to-wasm-编译管线)

---

## 1. 64 位自定义指令集

### 1.1 设计动机

标准 Lua 5.5 使用 32 位指令，限制了寄存器数量（最多 256 个）和常量索引范围。LXCLUA 将指令扩展为 64 位，使寄存器数量提升到 32768 个，常量索引范围提升到 2^31。

### 1.2 位域布局

LXCLUA 采用扰乱位域布局，而非简单的连续位域。这种设计有两个目的：

1. **性能**: 关键字段（如 OP 码）放在高 10 位，便于快速解码
2. **安全**: 非标准布局使反汇编工具难以直接解析

```
从 LSB 到 MSB 的布局:
   0-14:  A   (15 位) — 目标寄存器
  15-29:  B   (15 位) — 源寄存器/常量索引
     30:  k   (1 位)  — 常量标志 (0=R[x], 1=K[x])
  31-45:  C   (15 位) — 源寄存器/常量索引
  46-53:  (gap)       — 保留
  54-63:  OP  (10 位) — 操作码
```

### 1.3 指令格式编解码

核心宏定义在 `lopcodes.h` 中:

```c
// 创建 ABCk 格式指令
#define CREATE_ABCk(o,a,b,c,k)  ((cast(Instruction, o)<<POS_OP) \
            | (cast(Instruction, a)<<POS_A) \
            | (cast(Instruction, b)<<POS_B) \
            | (cast(Instruction, c)<<POS_C) \
            | (cast(Instruction, k)<<POS_k))

// 提取操作码
#define GET_OPCODE(i)  (cast(OpCode, ((i)>>POS_OP) & MASK1(SIZE_OP,0)))

// 提取 A 字段
#define GETARG_A(i)    getarg(i, POS_A, SIZE_A)
```

### 1.4 六种指令格式

| 格式 | 公式 | 用途 |
|------|------|------|
| `iABC` | `C(15) \| B(15) \| k(1) \| A(15) \| OP(10)` | 三寄存器操作 |
| `ivABC` | `vC(16) \| vB(14) \| k(1) \| A(15) \| OP(10)` | 大参数的三寄存器操作 |
| `iABx` | `Bx(31) \| A(15) \| OP(10)` | 寄存器 + 宽常量 |
| `iAsBx` | `sBx(31 signed) \| A(15) \| OP(10)` | 寄存器 + 有符号跳转 |
| `iAx` | `Ax(46) \| OP(10)` | 扩展地址 (EXTRAARG) |
| `isJ` | `sJ(46 signed) \| OP(10)` | 大偏移跳转 |

### 1.5 常量优化

常量表支持去重和分类:

```c
// 常量类型
#define LUA_VNUMINT    ((4 << 4) | 1)  // 整数常量
#define LUA_VNUMFLT    ((4 << 4) | 2)  // 浮点常量
#define LUA_VSHRSTR    ((4 << 4) | 3)  // 短字符串
#define LUA_VLNGSTR    ((4 << 4) | 4)  // 长字符串
```

---

## 2. OOP 系统实现

### 2.1 类结构

每个类在 Lua 中就是一个表，通过特定键存储元信息:

```c
// 类元数据键 (lclass.h)
#define CLASS_KEY_NAME      "__classname"   // 类名
#define CLASS_KEY_PARENT    "__parent"      // 父类
#define CLASS_KEY_PARENTS   "__parents"     // 多父类数组
#define CLASS_KEY_METHODS   "__methods"     // 方法表
#define CLASS_KEY_STATICS   "__statics"     // 静态成员
#define CLASS_KEY_PRIVATES  "__privates"    // 私有成员
#define CLASS_KEY_PROTECTED "__protected"   // 保护成员
#define CLASS_KEY_INIT      "init"          // 构造函数
#define CLASS_KEY_MRO       "__mro"         // C3 线性化方法解析顺序
#define CLASS_KEY_FLAGS     "__flags"       // 类修饰符标志
```

### 2.2 C3 线性化算法

多继承使用 C3 线性化（与 Python 3 相同算法）计算 MRO:

```
function c3_linearize(C):
    if C has no parents:
        return [C]
    else:
        return [C] + merge(c3_linearize(P1), c3_linearize(P2), ..., [P1,P2,...])

function merge(lists):
    result = []
    while true:
        if all lists empty: return result
        for head of first non-empty list:
            if head not in tail of any list:
                result.append(head)
                remove head from all lists
                break
        if no head found: error (linearization fails)
```

**实现位置**: `lclass.c` 中的 `luaC_compute_mro()`

```c
void luaC_compute_mro(lua_State *L, int class_idx) {
    // 1. 获取父类列表
    // 2. 递归计算每个父类的 MRO
    // 3. merge 算法合并
    // 4. 结果存储在 CLASS_KEY_MRO 中
    // 5. 方法查找时按 MRO 顺序遍历
}
```

### 2.3 方法查找链

属性访问 `obj.prop` 的查找顺序:

1. 对象的 `__obj_privates` 表（实例私有数据）
2. 对象的表自身（实例字段）
3. 类的 `__methods` 表（实例方法）
4. 按 MRO 顺序遍历父类 `__methods` 表
5. 触发 `__index` 元方法（如果有）

### 2.4 Trait 混入机制

Trait 是轻量级的代码复用单元:

```c
void luaC_usetrait(lua_State *L, int class_idx, int trait_idx) {
    // 1. 遍历 trait 的 __methods 表
    // 2. 将每个方法复制到类的 __methods 表
    // 3. 记录 trait 的 __trait_requires 表
    // 4. 验证 require 方法是否已实现
    // 5. 将 trait 添加到 __traits 表
}
```

### 2.5 访问控制实现

```c
int luaC_checkaccess(lua_State *L, int obj_idx, TString *key,
                     int caller_class_idx) {
    // 1. 检查 key 是否在 __privates 中 → 私有
    // 2. 检查 key 是否在 __protected 中 → 保护
    // 3. 检查 caller 是否在继承链中（保护成员）
    // 4. 否则 → 公开
}
```

### 2.6 Super 编译

`super.method(self)` 在编译时转换为:

1. 查找当前类 mro 中下一个定义了 `method` 的父类
2. 生成 `OP_GETSUPER` 指令
3. 运行时按 MRO 顺序调用父类方法

---

## 3. 混淆引擎架构

### 3.1 概述

混淆引擎位于 `lobfuscate.c`（4,312 行），提供 10 种混淆模式，可组合使用。

### 3.2 控制流扁平化 (CFF)

**变换流程**:

```
输入: 函数 Proto 的指令序列
    
Step 1: 识别基本块
    ┌─────────────────────────────────────────┐
    │ 遍历指令序列，在以下位置分割基本块:       │
    │ - JMP 指令后                             │
    │ - 条件跳转指令后 (EQ/LT/LE/TEST/TESTSET) │
    │ - RETURN 指令后                          │
    │ - CALL 指令后 (可能触发异常)              │
    │ - FORLOOP 指令后                         │
    └─────────────────────────────────────────┘
    
Step 2: 分配状态 ID
    ┌─────────────────────────────────────────┐
    │ 每个基本块分配一个唯一状态 ID            │
    │ 入口块状态 = 0                          │
    │ 出口块状态 = num_blocks - 1             │
    └─────────────────────────────────────────┘
    
Step 3: 生成 dispatcher
    ┌─────────────────────────────────────────┐
    │ 生成 dispatcher 循环:                   │
    │   state = 0 (入口状态)                  │
    │   loop:                                 │
    │     switch(state):                      │
    │       case 0: ... state = next; break   │
    │       case 1: ... state = next; break   │
    │       ...                               │
    └─────────────────────────────────────────┘
    
Step 4: 替换跳转
    ┌─────────────────────────────────────────┐
    │ 原始: JMP target → state = target_state │
    │ 原始: EQ cond → state = cond ? t : f   │
    └─────────────────────────────────────────┘

输出: 扁平化后的 Proto
```

### 3.3 状态编码

状态编码使用种子随机化:

```c
int luaO_encodeState(int state, unsigned int seed) {
    // 使用种子和状态的线性变换
    return (state * 0x9E3779B9 + seed) ^ (seed >> 16);
}

int luaO_decodeState(int encoded, unsigned int seed) {
    // 逆变换
    return ((encoded ^ (seed >> 16)) - seed) * 0x9E3779B9_inv;
}
```

### 3.4 不透明谓词

生成永远为真或永远为假的表达式，用于插入虚假分支:

```c
int luaO_emitOpaquePredicate(CFFContext *ctx, OpaquePredicateType type,
                              unsigned int *seed) {
    // 生成形如: (x * (x+1)) % 2 == 0 (恒真)
    // 或: (x^2 + x) % 2 == 1 (恒假)
    // 使用代数恒等式，静态分析难以判定
}
```

### 3.5 VM 保护

将标准 Lua 指令翻译为自定义 VM 指令集:

```c
int luaO_convertToVM(VMProtectContext *ctx) {
    // 1. 创建 opcode 映射表 (随机置换)
    // 2. 将每条 Lua 指令翻译为 VM 指令
    // 3. 使用 XOR 加密指令（密钥基于 PC）
    // 4. 注册到全局 VM 代码表
    // 5. 运行时通过 luaO_executeVM 解密执行
}

VMInstruction luaO_decryptVMInst(VMInstruction inst, uint64_t key, int pc) {
    // 解密: (inst ^ key) ROTR (pc & 0x3F)
    return ((inst ^ key) >> (pc & 0x3F)) | 
           ((inst ^ key) << (64 - (pc & 0x3F)));
}
```

---

## 4. 密码学库实现

### 4.1 ChaCha20 CSPRNG

文件: `csprng.c` / `csprng.h`

使用 ChaCha20 流密码作为密码学安全随机数生成器:

```c
// 初始化: 使用系统熵源（Windows BCrypt / Linux getrandom / macOS CCRandomCopyBytes）
// 混合: 线程 ID + 时间戳 + 计数器
// 输出: ChaCha20 块生成 64 字节随机数
// 重新播种: 每生成 1MB 数据后自动重新播种
```

### 4.2 RSA 大整数

RSA 实现基于大整数库，支持:

- 密钥生成（512/1024/2048/4096 位）
- PKCS#1 v1.5 填充
- OAEP 填充
- 签名/验签
- 中国剩余定理 (CRT) 优化解密

### 4.3 ECC secp256k1

椭圆曲线密码学实现，使用 secp256k1 曲线（与比特币相同）:

- 点加/点乘（Jacobian 投影坐标）
- ECDH 密钥交换
- ECDSA 签名/验签
- ECIES 加密

### 4.4 AES

AES 实现支持:

- 128/192/256 位密钥
- ECB/CBC/CFB/OFB/CTR/GCM 模式
- 硬件加速（AES-NI 指令集）
- PKCS#7 填充

---

## 5. NativeVM 原生虚拟机

### 5.1 两阶段汇编

NativeVM 的编译过程分为两个阶段:

**阶段 1: 解析 (NLang → 中间代码)**

```
NLang 源码
    │
    ▼
词法分析 (lnativeparser.c)
    │
    ▼
语法分析 (生成指令列表)
    │
    ▼
中间代码 (TCode 数组)
    - 每条指令包含: opcode, dst, src1, src2, imm
    - 伪指令: label, comment, nop
```

**阶段 2: 汇编 (中间代码 → 原生字节码)**

```
中间代码 (TCode 数组)
    │
    ▼
第一遍: 符号解析
    - 收集所有 label 位置
    - 解析 forward references
    │
    ▼
第二遍: 指令编码
    - 将 TCode 编码为 64 位指令
    - 跳转偏移计算
    - 常量表生成
    │
    ▼
原生字节码 (可直接被 lnativevm.c 执行)
```

### 5.2 寄存器系统

NativeVM 使用虚拟寄存器:

- 寄存器数量: 可配置（默认 256）
- 寄存器 0: 保留（返回值）
- 寄存器 1-15: 临时寄存器
- 寄存器 16+: 局部变量
- 栈: 函数调用时压栈/出栈

### 5.3 NLang 2.0 编译器

NLang 是 NativeVM 的源语言，语法类似 Lua 但使用原生指令编译:

```
支持的特性:
- 基础表达式: 算术/比较/逻辑/位运算
- 变量: 局部变量声明和赋值
- 控制流: if/while/repeat/for/break/continue
- 函数: 定义/调用/递归/闭包
- 字符串: 字面量/连接/长度
- 表: 字面量创建/索引访问

性能 (fib(30) 测试):
- NLang: ~0.004 秒
- Lua:  ~0.150 秒
- 加速比: ~37.5x
```

---

## 6. Lua-to-WASM 编译管线

### 6.1 架构概览

```
Lua 源码
    │
    ▼
┌──────────────────┐
│  Lua 解析器       │ 复用标准解析器 (lparser.c)
│  (Parser)        │
└──────┬───────────┘
       │
       ▼
┌──────────────────┐
│  Lua AST         │ 转换为 lua2wasm 内部 AST
│  (Adaptation)    │
└──────┬───────────┘
       │
       ▼
┌──────────────────┐
│  WASM 代码生成器  │ codegen.c (4,527 行)
│  (Codegen)       │
└──────┬───────────┘
       │
       ▼
┌──────────────────┐
│  WAT 生成器       │ wat2wasm.c (2,380 行)
│  (WAT Writer)    │
└──────┬───────────┘
       │
       ▼
┌──────────────────┐
│  WASM 二进制编码  │ 使用 wabt 库将 WAT 编译为 WASM
│  (Binary)        │
└──────┬───────────┘
       │
       ▼
    .wasm 文件
```

### 6.2 28 个 Host 回调

Lua 标准库函数通过 WASM 的 host 导入机制暴露:

| 类别 | 函数 |
|------|------|
| 基础 | `print`, `type`, `tostring`, `tonumber`, `pcall` |
| 数学 | `math_abs`, `math_floor`, `math_sqrt`, `math_sin`, `math_cos` |
| 字符串 | `string_len`, `string_sub`, `string_find`, `string_gsub` |
| 表 | `table_insert`, `table_remove`, `table_sort`, `table_concat` |
| IO | `io_write`, `io_read`, `io_open`, `io_close` |
| 内存 | `malloc`, `free`, `memcpy`, `memset` |

### 6.3 端到端流程

```
1. 解析 Lua 源码为 AST
2. 遍历 AST，生成 WASM 函数体
3. 为每个 Lua 函数创建一个 WASM 函数
4. 使用 WASM 局部变量模拟 Lua 寄存器
5. 使用 WASM 全局变量模拟 Lua 全局表
6. 生成 WAT 文本格式
7. 调用 wabt 编译为 WASM 二进制
8. 输出 .wasm 文件
```

**已知限制**:
- 不支持协程 (coroutine)
- 不支持 debug 库
- 不支持动态加载 (load/loadstring)
- 元表支持有限
- 闭包捕获需要额外处理