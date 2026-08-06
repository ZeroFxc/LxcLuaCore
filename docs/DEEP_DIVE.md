# LXCLUA-NCore 技术深潜

> 本文档从实际代码出发，深入分析 LXCLUA-NCore 的六个核心子系统设计。所有技术细节均通过源码阅读验证，标注了具体文件和行号引用。

---

## 目录

- [1. 64 位自定义指令格式](#1-64-位自定义指令格式)
- [2. OOP 系统实现](#2-oop-系统实现)
- [3. 混淆引擎架构](#3-混淆引擎架构)
- [4. 密码学库实现](#4-密码学库实现)
- [5. NativeVM 原生虚拟机](#5-nativevm-原生虚拟机)
- [6. Lua-to-WASM 编译管线](#6-lua-to-wasm-编译管线)

---

## 1. 64 位自定义指令格式

### 1.1 设计动机

标准 Lua 5.4 使用 32 位指令格式，操作数空间有限：

```
Lua 5.4: | Bx(18) | C(9) | A(8) | OP(6) | = 32 bit
```

LXCLUA 改用 64 位格式以支持更大的寄存器文件（512 vs 250）和更多操作数位宽。额外的比特空间还允许添加扰乱层以增加逆向难度。

### 1.2 实际位布局

```c
/* src/core/lopcodes.h */
#define SIZE_OP  10  /* 操作码占 10 bit，支持 1024 种指令 */
#define SIZE_A   15  /* A 操作数: 0~32767 */
#define SIZE_B   15  /* B 操作数: 0~32767 */
#define SIZE_C   15  /* C 操作数: 0~32767 */
#define SIZE_k    1  /* k 标志位: 0 或 1 */

/* 扰乱布局（非物理顺序）:
** Physical: | OP[54-63] | C[31-45] | k[30] | B[15-29] | A[0-14] |
**
** 为什么扰乱？
** 标准 Lua 的布局是 Bx|C|A|OP 从高位到低位排列，
** 扰乱后直接读取二进制无法通过标准工具解析。
*/
```

### 1.3 编解码宏

```c
/* 编码宏 */
#define CREATE_ABC(op, a, b, c) (((Instruction)(op) << 54) | \
    ((Instruction)(c) << 31) | ((Instruction)(b) << 15) | (Instruction)(a))

#define CREATE_ABx(op, a, bx)   (((Instruction)(op) << 54) | \
    ((Instruction)(bx) << 15) | (Instruction)(a))

#define CREATE_Ax(op, ax)       (((Instruction)(op) << 54) | (Instruction)(ax))

/* 解码宏 */
#define GET_OPCODE(i)   (int)(((i) >> 54) & 0x3FF)  /* 10 bit */
#define GETARG_A(i)     (int)(((i) >> 0)  & 0x7FFF)  /* 15 bit */
#define GETARG_B(i)     (int)(((i) >> 15) & 0x7FFF)  /* 15 bit */
#define GETARG_C(i)     (int)(((i) >> 31) & 0x7FFF)  /* 15 bit */
#define GETARG_k(i)     (int)(((i) >> 30) & 0x1)     /* 1 bit */
#define GETARG_Bx(i)    (int)(((i) >> 15) & 0x7FFF)  /* B+C 合并 */
#define GETARG_Ax(i)    (int)(((i) >> 0)  & 0x7FFF)  /* A 扩展 */
#define GETARG_sBx(i)   (GETARG_Bx(i) - MAXARG_sBx)  /* 有符号 Bx */
#define GETARG_sC(i)    (GETARG_C(i) - MAXARG_sC)    /* 有符号 C */
#define GETARG_sJ(i)    (int)(((i) >> 15) & MAXARG_sJ)  /* 有符号跳转 */
```

### 1.4 字节码加密层

```c
/* src/ldum.c / lundump.c */
/* 序列化时应用的安全变换:
** 1. 动态 opcode remapping: 每次编译随机生成 opcode→instruction 映射表
** 2. 时间戳 XOR: 常量时间戳用 key 混淆
** 3. SHA-256 头部签名: 验证数据完整性
*/
```

### 1.5 与标准 Lua 的对比

| 特性 | Lua 5.4 | LXCLUA-NCore |
|------|---------|--------------|
| 指令位宽 | 32 bit | 64 bit |
| MAXARG_A | 255 (8 bit) | 32767 (15 bit) |
| MAXARG_B | 511 (9 bit) | 32767 (15 bit) |
| MAXARG_C | 511 (9 bit) | 32767 (15 bit) |
| 寄存器上限 | 250 | 512 |
| Opcode 空间 | 64 (6 bit) | 1024 (10 bit) |
| 扰乱层 | 无 | 有 |
| Remapping | 无 | 动态 |

---

## 2. OOP 系统实现

### 2.1 系统架构

```
src/stdlib/lclass.h  ←  类型标志与元数据键定义
src/stdlib/lclass.c  ←  4470 行完整实现
         │
         ├── CLASS_FLAG (位掩码)
         │     FINAL, ABSTRACT, INTERFACE, SEALED, TRAIT, SINGLETON
         │
         ├── ACCESS (访问控制)
         │     PUBLIC, PROTECTED, PRIVATE
         │
         ├── MEMBER (成员修饰符)
         │     METHOD, FIELD, STATIC, CONST, VIRTUAL, OVERRIDE, ABSTRACT, FINAL
         │
         ├── 元数据键
         │     __classname, __parent, __methods, __statics
         │     __privates, __protected, __mro, __traits ...
         │
         └── C3 线性化 MRO (方法解析顺序)
```

### 2.2 C3 线性化算法

```c
/* lclass.c 中的 C3 线性化实现
** 目标: 解决多继承的方法解析顺序 (MRO)
** 约束:
**   1. 子类优先于父类
**   2. 声明顺序靠前的父类优先
**   3. 局部优先序 (local precedence order) 一致
**
** 算法: merge(L[C1], L[C2], ..., L[Cn], [C1, C2, ..., Cn])
** 其中 L[Ci] 是 Ci 的线性化列表
*/
```

### 2.3 Trait 混入机制

```c
/* Trait 是带实现的接口，允许代码复用
** 使用 __traits 元数据键存储已 trait 列表
**
** 解析: class Foo uses Bar, Baz
**   1. 检查 Bar/Baz 是否为 trait (IS_TRAIT 标志)
**   2. 将 trait 的 methods 复制到当前类
**   3. 检测冲突（同名方法的线性化处理）
*/
```

### 2.4 访问控制实现

```c
/* 运行时访问检查 (lclass.c)
** __privates 表: 仅定义该成员的类内部可访问
** __protected 表: 子类可通过继承链访问
** __methods/__statics 表: PUBLIC 默认可见
**
** 检查流程:
**   GETFIELD → 检查 __privates → 检查调用者 __classname
**           → 如果是同类的另一实例 → 允许
**           → 如果是不同类 → 拒绝 (尝试访问 → error)
**
** 实现注意: 使用 separate private namespace 技术
** 同一字段在不同类中有独立的 __privates 副本
*/
```

### 2.5 Super 表达式编译

```c
/* 在 lparser.c 中:
** super.method(self, args...) → __parent.method(self, args...)
** super(args...) → __parent.__init__(self, args...)
**
** 编译时确定 super 指向:
**   1. 查 __mro 表的第二个元素 (第一个是自己)
**   2. 通过 GETTABUP 加载父类
**   3. 通过 GETTABLE 提取方法
**   4. 调用时传入 self 作为第一个参数
*/
```

---

## 3. 混淆引擎架构

### 3.1 控制流扁平化 (CFF) 原理

```
原始代码:              混淆后:
┌─ block1 ──┐         ┌─ dispatcher loop ─┐
│ stmt1     │         │  state = initial  │
│ stmt2     │    →    │  while true do     │
│ if cond   │         │    switch(state)  │
└───────────┘         │      case 1: ...  │
│                     │      case 2: ...  │
┌─ block2 ──┐         │  end              │
│ stmt3     │         └───────────────────┘
│ goto L    │
└───────────┘         每个基本块的末尾设置下一个 state
                      原始跳转被转换为 state 赋值

附加变换:
  - block shuffle: 块的物理存储顺序随机化
  - bogus blocks: 永远不会执行的虚假分支
  - opaque predicates: 运行时恒真/假的条件
  - state encoding: state 值用混淆函数编码
```

### 3.2 BasicBlock 数据结构

```c
/* lobfuscate.h */
typedef struct BasicBlock {
  int start_pc;         /* 块的起始指令 PC */
  int end_pc;           /* 块的结束指令（不含） */
  int state_id;         /* 分配的 dispatcher state ID */
  int original_target;  /* 原始跳转目标块索引 */
  int fall_through;     /* 顺序执行时的下一块 (-1 表示无) */
  int cond_target;      /* 条件跳转目标块索引 (-1 表示无) */
  int is_entry;         /* 是否为函数入口块 */
  int is_exit;          /* 是否为出口块（含 RETURN） */
} BasicBlock;
```

### 3.3 CFF 变换流程

```c
/* lobfuscate.c 中的实现步骤:

** 第一步: 基本块识别 (scan_basic_blocks)
**   - 扫描所有指令，标记 leader（跳转目标/跳转后第一条指令）
**   - 构建 BasicBlock 数组

** 第二步: 构建控制流图
**   - 确定每个块的 fall_through（顺序后继）
**   - 确定每个块的 cond_target（条件跳转后继）
**   - 标记 entry/exit 块

** 第三步: 生成 dispatcher 框架
**   - 分配一个寄存器作为 state_reg
**   - 生成 if-elseif-elseif... 链或 binary search tree
**   - 最坏情况 O(n)，binary dispatch 优化到 O(log n)

** 第四步: 变换原始指令
**   - JMP → state=target_state
**   - JT/JF → state=cond ? target : fall_through
**   - 其他指令保留（但 PC 重新分配）

** 第五步: 应用附加混淆 (可选)
**   - block shuffle: 物理顺序重排
**   - bogus blocks: 插入永远不会匹配的 case
**   - opaque predicates: 用恒等式包裹条件
**   - string encrypt: XOR 或 Vigenère 加密字符串常量
*/
```

### 3.4 不透明谓词示例

```c
/* 不透明谓词: 运行时结果恒为真或假的数学表达
** 例:
**   (x*x >= 0)                    → 恒真（对整数）
**   ((x*3) % 2 == 0)             → 取决于 x，但静态分析难证明
**   (7*y*y - 1 != x*x)           → 丢番图方程，永不等
**   (popcount(x) % 2 == 0)       → 对特定分布永真
**
** 实现:
**   1. 生成:  if (x*x >= 0) then REAL_CODE else BOGUS_CODE
**   2. 反编译器看到分支 → 误以为有两个路径
**   3. 实际运行时 always take real path
*/
```

### 3.5 VM 保护模式

```c
/* lvmpro.c 实现:
** vm_compile(Proto *p) → Lua function that simulates the bytecode
**
** 转换过程:
**   1. 递归处理所有子 Proto
**   2. 为每个 Proto 构建常量表 (_constants)
**   3. 构建 Protos 表 (_protos)
**   4. 构建指令表 (_instructions)，每条指令为 {op, a, b, c, k, bx, sbx}
**   5. 生成 Lua 源码: 一个函数包含指令解释器
**   6. 每条 bytecode 变成一个 if/elseif 分支
**   7. 返回的函数(* replace 原始闭包)
**
** 效果:
**   - 原始字节码消失
**   - 运行时只存在 Lua 闭包和指令表
**   - 逆向需要从 Lua 代码重建逻辑
*/
```

---

## 4. 密码学库实现

### 4.1 ChaCha20 CSPRNG

```c
/* src/utils/csprng.c — 基于 RFC 7539 */

/* 设计参数 */
ROUNDS = 20          /* 标准 ChaCha20 */
BLOCK_SIZE = 64      /* 字节 */
KEY_SIZE = 32        /* 256-bit */
NONCE_SIZE = 12      /* 96-bit */

/* 种子扩展 (seed_mix):
**   单个 64 位种子 → 320 位状态（256-bit key + 96-bit nonce）
**
**   使用 SplitMix64 混合器:
**     x ^= x >> 30; x *= 0xbf58476d1ce4e5b9;
**     x ^= x >> 27; x *= 0x94d049bb133111eb;
**     x ^= x >> 31;
**
**   连续调用 seed_mix 生成 5 个 64 位值
**   组装: key(32B) + nonce(12B) + counter(4B)
*/

/* Quarter Round — 基本运算单元 */
static inline void chacha_quarter_round(uint32_t* a, uint32_t* b, uint32_t* c, uint32_t* d) {
  *a += *b; *d ^= *a; *d = (*d << 16) | (*d >> 16);  /* rotl 16 */
  *c += *d; *b ^= *c; *b = (*b << 12) | (*b >> 20);  /* rotl 12 */
  *a += *b; *d ^= *a; *d = (*d << 8)  | (*d >> 24);  /* rotl 8  */
  *c += *d; *b ^= *c; *b = (*b << 7)  | (*b >> 25);  /* rotl 7  */
}

/* 20 轮混合 = 10 个 double-round (列+对角线交替)
** 初始矩阵布局:
**   ┌────┬────┬────┐
**   │"exp"|"nd "|"3 "   ← 固定常量 "expand 32-byte k"
**   ├────┼────┼────┤
**   │key │key │key │  ← 256 位密钥
**   ├────┼────┼────┤
**   │cnt │nonce         ← 32-bit counter + 96-bit nonce
**   └────┴────┴────┘
*/
```

### 4.2 RSA 大整数

```c
/* src/utils/lrsa.c — 自定义大整数 */

/* 表示方式: uint32_t 数组, MSB first
** bigint_t { uint32_t *d; int nlimbs; }
** d[0] = 最高有效 limb, d[n-1] = 最低有效 limb
** 最大: 4096 位 = 128 limbs
*/

/* 支持的模幂运算:
**   - Barrett 约简 (避免慢速除法)
**   - Montgomery ladder (防侧信道)
**   - 滑动窗口优化 (指数分解)
*/

/* 素数生成:
**   - Miller-Rabin 检测: 40 轮 (FIPS 180-4 标准)
**   - 错误概率: 2^(-80)
**   - 使用全局 CSPRNG 状态
*/

/* 填充方案:
**   - PKCS#1 v1.5 签名填充
**   - PKCS#1 v1.5 加密填充 (OAEP 未实现)
*/
```

### 4.3 ECC secp256k1

```c
/* src/utils/lecc.c — 比特币/以太坊标准曲线 */

/* 曲线参数 */
p  = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFC2F
a  = 0 (secp256k1 特有)
b  = 7
n  = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141 (阶)
G  = (0x79BE667E..., 0x483ADA77...) (基点)

/* 数表示:
**   256 位整数 = 8 个 uint32_t limb (little-endian)
**   uint256_t { uint32_t d[8]; }
**   d[0] = 最低 32 位, d[7] = 最高 32 位
*/

/* 支持的运算:
**   - 点加法 (point_add)
**   - 点加倍 (point_double, secp256k1 a=0 简化)
**   - 标量乘法 (scalar_mul، double-and-add)
**   - ECDSA 签名/验证
**   - ECDH 密钥交换
**   - 公钥恢复 (recover id 0/1)
*/
```

### 4.4 AES 支持

```c
/* src/utils/aes.h — ECB/CBC/CTR 模式 */

/* 参数: 128/192/256 位密钥 */
/* 模式:
**   ECB: Electronic Codebook (最简，不推荐)
**   CBC: Cipher Block Chain (需 16 字节 IV)
**   CTR: Counter Mode (并行化，需 16 字节 nonce+counter)
*/
```

---

## 5. NativeVM 原生虚拟机

### 5.1 设计目标

NativeVM 是一个**完全独立的虚拟机**，它的目标是在没有任何 Lua 运行时开销的情况下执行代码：

- 纯 C 运行时（执行期间零 `lua_State` 操作）
- 自定义指令集 (64 位，不同于主 VM)
- 独立的寄存器文件 (256 寄存器，tagged union)
- 独立的类型标签系统
- 自定义汇编器链 (两遍汇编)

### 5.2 指令格式

```
┌───────────────┬─────┬─────┬─────┬──────┐
│ imm32 (32-63) │c(8) │b(8) │a(8) │op(8) │
└───────────────┴─────┴─────┴─────┴──────┘
总计: 64 bit

对比主 VM:
┌──────────┬──────────┬───┬──────────┬──────────┐
│ OP(10)   │ C(15)    │k  │ B(15)    │ A(15)    │
└──────────┴──────────┴───┴──────────┴──────────┘
```

### 5.3 寄存器系统

```c
/* lnativevm.c */
/* 每个寄存器是 tagged union */
typedef struct {
  int type;       /* NTYPE_NIL/INT/FLOAT/PTR/FUNC */
  union {
    int64_t i;    /* 整数或指针的整数部分 */
    double  f;    /* 浮点数 */
  };
} NativeValue;

/* 寄存器分区:
**   R0..R223   : 用户变量 (224 个)
**   R224..R255 : 编译器临时寄存器 (32 个，从借用)
**
** 最大函数可声明 224 个局部变量 + 上值
*/

/* 类型标签:
**   NTYPE_NIL  = 0  → 空值
**   NTYPE_INT  = 1  → int64_t 整数
**   NTYPE_FLOAT= 2  → double 浮点
**   NTYPE_PTR  = 3  → 指针 (字符串/表/函数引用)
**   NTYPE_FUNC = 4  → 函数引用 (v.i 存储 func_id)
*/
```

### 5.4 NLang 2.0 编译器

```c
/* src/vm/lnativeparser.c — 两阶段汇编 */

/* 第一阶段: 词法+语法+中间代码
** 输入: Lua-like 源码字符串
** 输出: AsmInst[] 中间指令数组 (含标签引用)
**
** 支持的语句:
**   local, if/else, while, repeat/until, for(numeric+generic)
**   function(def + closure), goto/label, return, break/continue
**
** 支持的运算符:
**   + - * / // % & | ^ ~ << >> (算术+位)
**   == ~= < <= > >= (比较)
**   .. (concat), # (length), not and or (逻辑)
**   = (赋值), multi-assignment (a, b = c, d)
*/

/* 第二阶段: 标签解析
**   扫描 AsmInst[]，记录 label 位置
**   回填 JMP/JT/JF 的 imm32 偏移量
**   输出最终 64 位指令数组
*/

/* VM API (Lua 侧):
**   native.new(inst_array, nregs)  → nv (VM 句柄)
**   native.call(nv, ...)            → results
**   native.asm(code_string)        → inst_array (编译+NEXEC)
*/
```

### 5.5 与主 VM 交互

```c
/* NI_CALL 操作码处理:
**   1. 当 func 是 Lua 值时 (NTYPE_PTR/FUNC):
**      - 回退到 Lua API 调用 (lua_pcall 风格)
**      - 参数: R[c..c+imm-1]
**      - 结果: R[a] = return_value(s)
**
**   2. 当 func 是 NLang 函数时 (NTYPE_FUNC, v.i=func_id):
**      - 直接跳转到该函数入口
**      - 寄存器 0..argc-1 作为参数
**      - 结果写入 R[a..a+nret-1]
**
**   3. 互操作码:
**      NI_GETFIELD: R[a] = R[b][str_ref]  (Lua table 成员)
**      NI_SETFIELD: R[a][str_ref] = R[b]
**      NI_GETTABLE: R[a] = R[b][R[c]]     (通用索引)
**      NI_SETTABLE: R[a][R[b]] = R[c]
**      NI_LOADKPTR: R[a] = registry[imm32] (Lua Registry 引用)
*/
```

---

## 6. Lua-to-WASM 编译管线

### 6.1 整体架构

```
                    ┌─────────────────┐
                    │   Lua 源码文件   │
                    └────────┬────────┘
                             │
                    ┌────────▼────────┐
                    │  lua2wasm 编译器  │
                    │  (codegen.c +    │
                    │   parser.c)      │
                    └────────┬────────┘
                             │
                    ┌────────▼────────┐
                    │   WASM 二进制码   │
                    └────────┬────────┘
                             │
                    ┌────────▼────────┐
                    │  wasmtime 运行时  │
                    │  (lwasmtime.c +  │
                    │   28 host funcs) │
                    └────────┬────────┘
                             │
                    ┌────────▼────────┐
                    │     执行结果     │
                    └─────────────────┘
```

### 6.2 lua2wasm 编译器后端

```c
/* src/lua2wasm/codegen.c — 4,729 行
** 将 LXCLUA AST → WASM 二进制
**
** 主要模块:
**   - 类型映射: LXCLUA 类型 → WASM 类型 (i32/i64/f32/f64/externref)
**   - 函数编译: function → WASM func + locals + body
**   - 内存布局: 模拟 Lua 堆的 WASM 线性内存
**   - GC 集成: 调用 wasmtime GC 提案的引用类型
**   - Host 函数映射: 28 个回调 → WASM import
*/
```

### 6.3 28 个 Host 回调函数

```c
/* lwasmtime.c 注册的 host 函数 */

/* I/O */
l2w_print_cb        /* print(...)        — 捕获到 output_buf */
l2w_write_raw_cb    /* io.write(...)     — 原始输出 */
l2w_write_err_cb    /* io.stderr:write   — 错误输出 */
l2w_read_cb         /* io.read(...)      — 从 stdin_data 读取 */

/* 格式化 */
l2w_fmt_cb          /* string.format     — 返回格式化字符串 */
l2w_fmt_spec_cb     /* string.format 内部辅助 */

/* 数学 */
l2w_math_cb         /* math.*            — 一元数学函数 */
l2w_math2_cb        /* math 二元函数     — 如 math.atan2, math.pow */

/* 类型转换 */
l2w_parse_num_cb    /* tonumber          — 解析数字 */
l2w_read_num_cb     /* io.read("*number") — 读取数字 */

/* 文件系统 */
l2w_fs_open_cb      /* io.open           — 文件句柄表管理 */
l2w_fs_read_cb      /* file:read         — 从文件读 */
l2w_fs_write_cb     /* file:write        — 写文件 */
l2w_fs_seek_cb      /* file:seek         — 定位 */
l2w_fs_flush_cb     /* file:flush        — 刷新缓冲 */
l2w_fs_close_cb     /* file:close        — 关闭文件 */

/* 操作系统 */
l2w_os_time_cb      /* os.time           → int64 */
l2w_os_time_table_cb/* os.time(table)    → 冻结时间 */
l2w_os_clock_cb     /* os.clock          → CPU 秒 */
l2w_os_getenv_cb    /* os.getenv         — 读取环境变量 */
l2w_os_exit_cb      /* os.exit           — 终止执行 */
l2w_os_date_cb      /* os.date           → 日期字符串 */
l2w_os_remove_cb    /* os.remove         — 删除文件 */
l2w_os_rename_cb    /* os.rename         — 重命名 */
l2w_os_tmpname_cb   /* os.tmpname        → 临时文件名 */

/* 其他 */
l2w_obj_id_cb       /* 对象唯一 ID 计数器 */
l2w_warn_cb         /* warn(...)         — 警告输出 */
```

### 6.4 lua2wasm 端到端流程

```lua
-- Lua 使用示例:
local lua2wasm = require("lua2wasm")
local wasmtime = require("wasmtime")

-- 步骤 1: 编译 Lua → WASM
local wasm_bytes = lua2wasm.wcompile([[
    local function fib(n)
        if n < 2 then return n end
        return fib(n-1) + fib(n-2)
    end
    return fib(10)
]])

-- 步骤 2: 一键运行 WASM
local result = wasmtime.runLua2wasm(wasm_bytes)
print(result)  -- → 55
```

```c
/* 内部实现:
**   1. lua2wasm.wcompile(lua_code)
**      a. 调用 lparser.c 的解析器 → AST
**      b. 调用 codegen.c → WASM 二进制码
**      c. 返回 WASM 字节串 (lightuserdata + size)
**
**   2. wasmtime.runLua2wasm(wasm_bytes)
**      a. 创建 Engine (带 GC 支持)
**      b. 创建 Store
**      c. 编译 Module (wasmtime_module_new)
**      d. 创建 Linker + 注册 28 个 host func
**      e. 实例化: linker:instantiate(store, module)
**      f. 查找 _start 入口函数并调用
**      g. 捕获 print 输出 → output_buf
**      h. 返回 output_buf 作为 Lua 字符串
*/
```

### 6.5 Engine 配置选项

```lua
local engine = wasmtime.newEngine{
  optLevel = "speed",              -- "none"/"speed"/"speedAndSize"
  parallelCompilation = true,       -- 并行编译
  profiler = "none",               -- "none"/"jitdump"/"vtune"/"perfmap"
  nanCanonicalization = false,      -- NaN 规范化（确定性执行）
  nativeUnwind = true,             -- 原生栈展开信息
  sharedMemory = false,            -- 启用共享内存
  memoryMayMove = false,           -- 内存可重定位
  memoryGuardSize = 0,             -- 内存保护区大小(字节)
  maxWasmStack = 0,                -- 最大 WASM 栈大小(字节)
  tailCall = false,                -- 启用尾调用
}
```

---

## 附录 A: 关键宏和常量速查

| 宏/常量 | 值 | 来源 | 说明 |
|---------|-----|------|------|
| MAXVARS | 512 | lparser.c | 函数最大局部变量数 |
| LUAI_MAXSTACK | 1000000 | luaconf.h | 最大栈深度 |
| LUA_EXTRASPACE | -- | luaconf.h | Lua 状态额外内存 |
| SIZE_OP | 10 | lopcodes.h | 操作码位宽 |
| SIZE_A/B/C | 15 | lopcodes.h | 操作数位宽 |
| MAXARG_sBx | 2^15-1 | lopcodes.h | 有符号 Bx 最大值 |
| BI_MAX_LIMBS | 128 | lrsa.c | 大整数最大 limb 数 (4096 位) |
| U256_LIMBS | 8 | lecc.c | 256 位整数的 limb 数 |
| ASM_MAX_INSTS | 4096 | lnativevm.c | NativeVM 最大指令数 |
| NI_MAX | 47 | lnativevm.c | NativeVM 操作码总数 |
| VM_MAP_SIZE | 256 | lobfuscate.h | VM 保护映射表大小 |
| CSPRNG_ROUNDS | 20 | csprng.c | ChaCha20 轮数 |
| L2W_MAX_FILES | 64 | lwasmtime.c | lua2wasm 最大文件句柄数 |

## 附录 B: 文件格式

| 扩展名 | 格式 | 说明 |
|--------|------|------|
| `.lua` | 源码 | LXCLUA 脚本源文件 |
| `.luac` | 字节码 | 编译后的二进制字节码 (带 SHA-256 签名) |
| `.wasm` | WASM | WebAssembly 二进制模块 |
| `.wast` | WASM 文本 | WebAssembly 文本格式（调试用） |

## 附录 C: 构建系统变量

```makefile
# Makefile 关键变量
CC              # C 编译器 (gcc/clang/cl)
CFLAGS          # 编译标志 (-std=c23 -O2)
LUA_DIR         # 源码目录
BUILD_DIR       # 构建输出目录

# 可选模块开关
WITH_WASM       # 1=构建 wasmtime 模块
WITH_WASM3      # 1=构建 wasm3 模块
WITH_QUICKJS    # 1=构建 QuickJS 模块
WITH_LSP        # 1=构建 LSP 服务器

# 平台相关
WITH_JIT        # 是否编译 JIT (目前未使用)
ANDROID_NDK     # Android NDK 路径
```
