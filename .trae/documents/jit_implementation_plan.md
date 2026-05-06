# JIT 编译器完整实现计划书

## 1. 项目概述

### 1.1 背景

当前项目中的 JIT 编译器仅实现了框架结构，核心的字节码到机器码翻译逻辑尚未实现。目前的 `luaJIT_compile` 函数只是一个空实现，直接返回一个空的函数体，导致开启 JIT 后性能反而下降（约 1.7 秒 vs 原生的 1.3 秒）。

### 1.2 目标

实现完整的 Lua 字节码到机器码的翻译，通过 sljit 编译器将高频使用的字节码转换为平台原生汇编指令，实现性能的显著提升。

### 1.3 成功标准

- JIT 编译后的代码执行性能比原生 C Switch 分发提升 **2-5 倍**
- 支持至少 80% 的 Lua 标准字节码
- 实现完整的寄存器分配和优化

---

## 2. 技术架构分析

### 2.1 当前 JIT 架构

```
Lua 源码 → 字节码 → [JIT 编译] → 机器码
                         ↓
                    sljit_compiler
                         ↓
                   原生汇编指令
```

### 2.2 sljit 编译器特性

| 特性 | 说明 |
|------|------|
| 跨平台支持 | x86, x86-64, ARM, ARM64, MIPS |
| 指令发射 | sljit_emit_* 系列函数 |
| 寄存器分配 | 内置寄存器分配器 |
| 代码生成 | sljit_generate_code |

### 2.3 Lua 虚拟机状态结构

需要访问的关键数据结构：
- `lua_State` - 线程状态
- `CallInfo` - 调用信息
- `TValue` - 值类型
- `Proto` - 原型（字节码容器）

---

## 3. 字节码分析与优先级排序

### 3.1 Lua 字节码分类

根据 Lua 5.4 规范，字节码分为以下类别：

| 类别 | 字节码数量 | 典型指令 |
|------|-----------|----------|
| 加载/存储 | 12 | LOADK, LOADBOOL, GETTABLE, SETTABLE |
| 算术运算 | 14 | ADD, SUB, MUL, DIV, MOD, POW |
| 比较运算 | 6 | EQ, LT, LE |
| 控制流 | 10 | JMP, CALL, RETURN, FORPREP, TFORLOOP |
| 表操作 | 8 | NEWTABLE, SETLIST, GETI, SETI |
| 函数操作 | 6 | CLOSURE, VARARG, CALL |

### 3.2 优先级排序（按执行频率）

**第一优先级（高频核心指令）：**
1. LOADK - 加载常量
2. GETTABLE - 表读取
3. SETTABLE - 表写入
4. ADD/SUB/MUL/DIV - 算术运算
5. EQ/LT/LE - 比较运算
6. JMP - 无条件跳转
7. CALL - 函数调用
8. RETURN - 返回

**第二优先级（中频指令）：**
9. NEWTABLE - 创建表
10. GETI/SETI - 数组索引访问
11. CLOSURE - 创建闭包
12. FORPREP/FORLOOP - for 循环
13. TFORLOOP - 泛型 for 循环

**第三优先级（低频指令）：**
14. VARARG - 可变参数
15. SETLIST - 设置表列表
16. POW/MOD - 幂运算/取模
17. 位运算指令

---

## 4. 实现计划

### 4.1 阶段一：基础设施准备（1-2 周）

**任务：**
- 深入学习 sljit API 和指令发射机制
- 建立测试基准（性能测试用例）
- 完善 JIT 上下文管理

**输出：**
- sljit API 封装层
- 性能测试框架
- JIT 编译状态管理

### 4.2 阶段二：核心指令实现（4-6 周）

**任务：**
- 实现 LOADK 指令翻译
- 实现算术运算指令（ADD, SUB, MUL, DIV）
- 实现比较运算指令（EQ, LT, LE）
- 实现