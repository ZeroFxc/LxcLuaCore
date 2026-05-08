# LXCLUA-NCore JIT 编译器实现计划书

---

## 目录

1. [项目现状分析](#1-项目现状分析)
2. [模块化架构设计](#2-模块化架构设计)
   - 2.1 整体架构图
   - 2.2 目录结构规范
   - 2.3 模块职责划分
3. [分阶段实现计划](#3-分阶段实现计划)
4. [模块间调用流程](#4-模块间调用流程)
5. [代码规范](#5-代码规范)

---

## 1. 项目现状分析

### 1.1 当前 JIT 实现状态

当前项目中的 JIT 编译器位于 `src/vm/jit/ljit.c`，仅为占位符实现：

```c
int luaJIT_compile (lua_State *L, Proto *p) {
    struct sljit_compiler *compiler = sljit_create_compiler(NULL);
    sljit_emit_enter(compiler, 0, SLJIT_ARGS2V(W, W), 3, 3, 0);
    sljit_emit_return_void(compiler);  // 空实现 - 无实际字节码翻译
    void *code = sljit_generate_code(compiler, 0, NULL);
    p->jit_trace = code;
    return 1;
}
```

**问题分析**：
- 生成的机器码仅包含函数入口和空返回，无实际字节码翻译
- 开启 JIT 后因额外的 trace 验证开销，性能反而低于原生 C Switch 分发
- 需要实现完整的字节码到机器码的映射

### 1.2 项目架构基础

| 现有模块 | 路径 | 职责 |
|---------|------|------|
| sljit 库 | `src/jit/` | 跨平台机器码生成器（已集成） |
| 字节码定义 | `src/core/lopcodes.h` | 约 100+ 个操作码定义 |
| 虚拟机 | `src/vm/lvm.c` | 字节码解释执行 |
| 当前 JIT | `src/vm/jit/ljit.c` | JIT 编译器占位符 |

---

## 2. 模块化架构设计

### 2.1 整体架构图

```
┌──────────────────────────────────────────────────────────────────────────┐
│                         JIT 编译器模块化架构                            │
├──────────────────────────────────────────────────────────────────────────┤
│                                                                        │
│   ┌──────────────────────────────────────────────────────────────────┐  │
│   │                        核心控制层                              │  │
│   │                    src/vm/jit/core/                            │  │
│   │  ┌──────────┐  ┌──────────┐  ┌──────────┐                     │  │
│   │  │ ljit.c   │  │ ljit.h   │  │ ljit_int │                     │  │
│   │  │ 主入口   │  │ 公共API  │  │ 内部定义 │                     │  │
│   │  └────┬─────┘  └────┬─────┘  └──────────┘                     │  │
│   └───────│─────────────│──────────────────────────────────────────┘  │
│           │             │                                             │
│           ▼             ▼                                             │
│   ┌──────────────────────────────────────────────────────────────────┐  │
│   │                        前端处理层                              │  │
│   │                    src/vm/jit/frontend/                        │  │
│   │  ┌──────────┐  ┌──────────┐  ┌──────────┐                     │  │
│   │  │ translate│  │ analyze  │  │ analyze.h│                     │  │
│   │  │ 字节码翻译│  │ 数据流分析│  │ 分析结果 │                     │  │
│   │  └────┬─────┘  └────┬─────┘  └──────────┘                     │  │
│   └───────│─────────────│──────────────────────────────────────────┘  │
│           │             │                                             │
│           ▼             ▼                                             │
│   ┌──────────────────────────────────────────────────────────────────┐  │
│   │                        IR 中间层                               │  │
│   │                     src/vm/jit/ir/                             │  │
│   │  ┌──────────┐  ┌──────────┐  ┌──────────┐                     │  │
│   │  │ ljit_ir.h│  │ ljit_ir.c│  │ ir_list.c│                     │  │
│   │  │ IR定义   │  │ IR构建器 │  │ 链表操作 │                     │  │
│   │  └──────────┘  └────┬─────┘  └────┬─────┘                     │  │
│   │                     │             │                             │  │
│   │                     ▼             ▼                             │  │
│   │              ┌──────────┐  ┌──────────┐                        │  │
│   │              │ ir_label │  │ ir_bb.c  │                        │  │
│   │              │ 标签管理 │  │ 基本块   │                        │  │
│   │              └──────────┘  └──────────┘                        │  │
│   └──────────────────────────────────────────────────────────────────┘  │
│           │                                                           │
│           ▼                                                           │
│   ┌──────────────────────────────────────────────────────────────────┐  │
│   │                        优化层                                  │  │
│   │                    src/vm/jit/optimize/                        │  │
│   │  ┌──────────┐  ┌──────────┐  ┌──────────┐                     │  │
│   │  │ opt.c    │  │ opt_const│  │ opt_dce  │                     │  │
│   │  │ 优化器主入口│  │ 常量折叠 │  │ 死代码消除│                     │  │
│   │  └────┬─────┘  └────┬─────┘  └────┬─────┘                     │  │
│   │       │             │             │                             │  │
│   │       ▼             ▼             ▼                             │  │
│   │  ┌──────────┐  ┌──────────┐  ┌──────────┐                     │  │
│   │  │ opt_peep │  │ opt_cse  │  │ opt_inline│                    │  │
│   │  │ 窥孔优化 │  │ 公共子表达式│  │ 内联优化 │                     │  │
│   │  └──────────┘  └──────────┘  └──────────┘                     │  │
│   └──────────────────────────────────────────────────────────────────┘  │
│           │                                                           │
│           ▼                                                           │
│   ┌──────────────────────────────────────────────────────────────────┐  │
│   │                        寄存器分配层                             │  │
│   │                   src/vm/jit/regalloc/                         │  │
│   │  ┌──────────┐  ┌──────────┐  ┌──────────┐                     │  │
│   │  │ regalloc │  │ reg_graph│  │ reg_color│                     │  │
│   │  │ 分配器入口│  │ 干涉图构建│  │ 图着色算法│                     │  │
│   │  └────┬─────┘  └────┬─────┘  └────┬─────┘                     │  │
│   │       │             │             │                             │  │
│   │       ▼             ▼             ▼                             │  │
│   │  ┌──────────┐  ┌──────────┐  ┌──────────┐                     │  │
│   │  │ reg_spill│  │ reg_live │  │ reg_alloc│                     │  │
│   │  │ 溢出处理 │  │ 活跃分析 │  │ 分配策略 │                     │  │
│   │  └──────────┘  └──────────┘  └──────────┘                     │  │
│   └──────────────────────────────────────────────────────────────────┘  │
│           │                                                           │
│           ▼                                                           │
│   ┌──────────────────────────────────────────────────────────────────┐  │
│   │                        代码生成层                              │  │
│   │                    src/vm/jit/codegen/                         │  │
│   │  ┌──────────┐  ┌──────────┐  ┌──────────┐                     │  │
│   │  │ codegen  │  │ cg_arith │  │ cg_ctrl  │                     │  │
│   │  │ 生成器入口│  │ 算术指令 │  │ 控制流   │                     │  │
│   │  └────┬─────┘  └────┬─────┘  └────┬─────┘                     │  │
│   │       │             │             │                             │  │
│   │       ▼             ▼             ▼                             │  │
│   │  ┌──────────┐  ┌──────────┐  ┌──────────┐                     │  │
│   │  │ cg_table │  │ cg_call  │  │ cg_conv  │                     │  │
│   │  │ 表操作   │  │ 函数调用 │  │ 类型转换 │                     │  │
│   │  └──────────┘  └──────────┘  └──────────┘                     │  │
│   └──────────────────────────────────────────────────────────────────┘  │
│           │                                                           │
│           ▼                                                           │
│   ┌──────────────────────────────────────────────────────────────────┐  │
│   │                        sljit 封装层                             │  │
│   │                     src/vm/jit/sljit/                          │  │
│   │  ┌──────────┐  ┌──────────┐  ┌──────────┐                     │  │
│   │  │ sljit.h  │  │ sljit.c  │  │ sljit_mac│                     │  │
│   │  │ 封装定义 │  │ 封装实现 │  │ 辅助宏   │                     │  │
│   │  └──────────┘  └──────────┘  └──────────┘                     │  │
│   └──────────────────────────────────────────────────────────────────┘  │
│                                                                        │
└──────────────────────────────────────────────────────────────────────────┘
```

### 2.2 目录结构规范

```
src/vm/jit/
├── core/                  # 核心控制模块
│   ├── ljit.c             # JIT 主入口、编译触发
│   ├── ljit.h             # 公共 API 声明
│   └── ljit_internal.h    # 内部定义（仅模块内部使用）
├── ir/                    # 中间表示模块
│   ├── ljit_ir.h          # IR 类型定义、数据结构
│   ├── ljit_ir.c          # IR 节点创建、销毁
│   ├── ljit_ir_list.c     # IR 链表操作（插入、删除、遍历）
│   ├── ljit_ir_label.c    # IR 标签管理（跳转目标）
│   └── ljit_ir_bb.c       # 基本块分析与管理
├── frontend/              # 前端处理模块
│   ├── ljit_translate.c   # 字节码到 IR 的翻译
│   ├── ljit_analyze.c     # 数据流分析、类型推断
│   └── ljit_analyze.h     # 分析结果数据结构定义
├── optimize/              # 优化模块
│   ├── ljit_opt.c         # 优化器主入口、优化流程控制
│   ├── ljit_opt_const.c   # 常量折叠优化
│   ├── ljit_opt_dce.c     # 死代码消除
│   ├── ljit_opt_peep.c    # 窥孔优化
│   ├── ljit_opt_cse.c     # 公共子表达式消除
│   └── ljit_opt_inline.c  # 内联优化
├── regalloc/              # 寄存器分配模块
│   ├── ljit_regalloc.c    # 寄存器分配器主入口
│   ├── ljit_reg_graph.c   # 干涉图构建
│   ├── ljit_reg_color.c   # 图着色算法实现
│   ├── ljit_reg_spill.c   # 栈溢出处理
│   ├── ljit_reg_live.c    # 活跃变量分析
│   └── ljit_reg_alloc.c   # 分配策略选择
├── codegen/               # 代码生成模块
│   ├── ljit_codegen.c     # 代码生成器主入口
│   ├── ljit_cg_arith.c    # 算术运算指令生成
│   ├── ljit_cg_ctrl.c     # 控制流指令生成（跳转、分支）
│   ├── ljit_cg_table.c    # 表操作指令生成
│   ├── ljit_cg_call.c     # 函数调用指令生成
│   └── ljit_cg_conv.c     # 类型转换指令生成
└── sljit/                 # sljit 封装模块
    ├── ljit_sljit.h       # sljit 封装声明
    ├── ljit_sljit.c       # sljit 封装实现
    └── ljit_sljit_mac.h   # sljit 辅助宏定义
```

### 2.3 模块职责划分

#### 2.3.1 核心控制层 (core/)

| 文件 | 职责 | 详细说明 |
|------|------|---------|
| `ljit.c` | JIT 主入口 | 编译触发、状态管理、trace 缓存、对外 API |
| `ljit.h` | 公共 API | 对外暴露的函数声明、类型定义 |
| `ljit_internal.h` | 内部定义 | 模块间共享的内部数据结构、宏 |

#### 2.3.2 前端处理层 (frontend/)

| 文件 | 职责 | 详细说明 |
|------|------|---------|
| `ljit_translate.c` | 字节码翻译 | 逐条字节码转换为 IR 节点 |
| `ljit_analyze.c` | 数据流分析 | 类型推断、依赖分析、优化机会识别 |
| `ljit_analyze.h` | 分析结果定义 | 分析数据结构、类型信息表示 |

#### 2.3.3 IR 中间层 (ir/)

| 文件 | 职责 | 详细说明 |
|------|------|---------|
| `ljit_ir.h` | IR 定义 | IR 类型枚举、节点结构、上下文结构 |
| `ljit_ir.c` | IR 构建器 | IR 节点创建、销毁、属性设置 |
| `ljit_ir_list.c` | 链表操作 | IR 节点插入、删除、遍历、查找 |
| `ljit_ir_label.c` | 标签管理 | 跳转标签创建、目标设置、解析 |
| `ljit_ir_bb.c` | 基本块管理 | 基本块划分、CFG 构建、边界分析 |

#### 2.3.4 优化层 (optimize/)

| 文件 | 职责 | 详细说明 |
|------|------|---------|
| `ljit_opt.c` | 优化器入口 | 优化流程控制、优化 pass 调度 |
| `ljit_opt_const.c` | 常量折叠 | 编译时计算常量表达式 |
| `ljit_opt_dce.c` | 死代码消除 | 删除未使用的 IR 节点 |
| `ljit_opt_peep.c` | 窥孔优化 | 局部指令序列优化 |
| `ljit_opt_cse.c` | 公共子表达式消除 | 复用重复计算结果 |
| `ljit_opt_inline.c` | 内联优化 | 函数内联、减少调用开销 |

#### 2.3.5 寄存器分配层 (regalloc/)

| 文件 | 职责 | 详细说明 |
|------|------|---------|
| `ljit_regalloc.c` | 分配器入口 | 寄存器分配流程控制 |
| `ljit_reg_graph.c` | 干涉图构建 | 识别寄存器间冲突关系 |
| `ljit_reg_color.c` | 图着色算法 | Chaitin-Briggs 着色实现 |
| `ljit_reg_spill.c` | 溢出处理 | 无法分配寄存器时的栈溢出 |
| `ljit_reg_live.c` | 活跃分析 | 活跃变量分析、生命周期计算 |
| `ljit_reg_alloc.c` | 分配策略 | 物理寄存器分配、重命名 |

#### 2.3.6 代码生成层 (codegen/)

| 文件 | 职责 | 详细说明 |
|------|------|---------|
| `ljit_codegen.c` | 生成器入口 | 代码生成流程控制 |
| `ljit_cg_arith.c` | 算术指令 | ADD/SUB/MUL/DIV 等算术运算 |
| `ljit_cg_ctrl.c` | 控制流指令 | JMP/JMP_COND/CALL/RET 等 |
| `ljit_cg_table.c` | 表操作指令 | GETTABLE/SETTABLE/NEWTABLE |
| `ljit_cg_call.c` | 函数调用指令 | 调用约定处理、参数传递 |
| `ljit_cg_conv.c` | 类型转换指令 | 数值类型转换、装箱拆箱 |

#### 2.3.7 sljit 封装层 (sljit/)

| 文件 | 职责 | 详细说明 |
|------|------|---------|
| `ljit_sljit.h` | 封装声明 | sljit 封装函数声明 |
| `ljit_sljit.c` | 封装实现 | sljit API 的封装和辅助函数 |
| `ljit_sljit_mac.h` | 辅助宏 | 便捷宏定义、常量定义 |

---

## 3. 分阶段实现计划

### 3.1 第一阶段：核心框架搭建（2周）

**目标**：建立 JIT 编译器的核心框架

| 任务 | 模块 | 文件 | 完成标准 |
|------|------|------|---------|
| 创建核心入口 | core/ | `ljit.c`, `ljit.h` | 编译入口、状态管理 |
| 定义 IR 结构 | ir/ | `ljit_ir.h` | IR 类型、节点结构 |
| 实现 IR 构建器 | ir/ | `ljit_ir.c`, `ljit_ir_list.c` | 节点创建、链表操作 |
| 实现标签管理 | ir/ | `ljit_ir_label.c` | 跳转标签管理 |
| 集成 sljit 封装 | sljit/ | `ljit_sljit.h`, `ljit_sljit.c` | sljit 封装完成 |

### 3.2 第二阶段：前端处理（3周）

**目标**：实现字节码翻译和数据流分析

| 任务 | 模块 | 文件 | 完成标准 |
|------|------|------|---------|
| 实现字节码翻译 | frontend/ | `ljit_translate.c` | 高频字节码翻译 |
| 实现数据流分析 | frontend/ | `ljit_analyze.c`, `ljit_analyze.h` | 类型推断、依赖分析 |
| 实现基本块分析 | ir/ | `ljit_ir_bb.c` | CFG 构建 |

### 3.3 第三阶段：优化器（4周）

**目标**：实现基础优化 pass

| 任务 | 模块 | 文件 | 完成标准 |
|------|------|------|---------|
| 优化器主入口 | optimize/ | `ljit_opt.c` | 优化流程控制 |
| 常量折叠 | optimize/ | `ljit_opt_const.c` | 编译时常量计算 |
| 死代码消除 | optimize/ | `ljit_opt_dce.c` | 未使用代码删除 |
| 窥孔优化 | optimize/ | `ljit_opt_peep.c` | 局部指令优化 |

### 3.4 第四阶段：寄存器分配器（4周）

**目标**：实现基于图着色的寄存器分配

| 任务 | 模块 | 文件 | 完成标准 |
|------|------|------|---------|
| 活跃变量分析 | regalloc/ | `ljit_reg_live.c` | 生命周期分析 |
| 干涉图构建 | regalloc/ | `ljit_reg_graph.c` | 冲突图构建 |
| 图着色算法 | regalloc/ | `ljit_reg_color.c` | Chaitin-Briggs 算法 |
| 溢出处理 | regalloc/ | `ljit_reg_spill.c` | 栈溢出机制 |
| 分配器入口 | regalloc/ | `ljit_regalloc.c`, `ljit_reg_alloc.c` | 完整分配流程 |

### 3.5 第五阶段：代码生成器（4周）

**目标**：实现 IR 到机器码的翻译

| 任务 | 模块 | 文件 | 完成标准 |
|------|------|------|---------|
| 生成器入口 | codegen/ | `ljit_codegen.c` | 代码生成流程 |
| 算术指令生成 | codegen/ | `ljit_cg_arith.c` | ADD/SUB/MUL/DIV 等（含位运算） |
| 控制流生成 | codegen/ | `ljit_cg_ctrl.c` | JMP/CALL/RET |
| 表操作生成 | codegen/ | `ljit_cg_table.c` | GETTABLE/SETTABLE |
| 函数调用生成 | codegen/ | `ljit_cg_call.c` | 调用约定处理 |

### 3.6 第六阶段：完整字节码覆盖（3周）

**目标**：实现所有字节码的翻译

| 任务 | 模块 | 文件 | 完成标准 |
|------|------|------|---------|
| 类型转换指令 | codegen/ | `ljit_cg_conv.c` | 类型转换 |
| OOP 扩展支持 | frontend/ | `ljit_translate.c` | NEWCLASS/NEWOBJ 等 |
| 特殊指令支持 | frontend/ | `ljit_translate.c` | VARARG/CONCAT 等 |

---

## 4. 模块间调用流程

```
luaJIT_compile(L, proto)                    // core/ljit.c
       │
       ├──▶ ljit_context_create(L, proto)   // ir/ljit_ir.c
       │
       ├──▶ ljit_analyze(proto)             // frontend/ljit_analyze.c
       │
       ├──▶ ljit_translate(proto)           // frontend/ljit_translate.c
       │       │
       │       ├──▶ ljit_ir_create()        // ir/ljit_ir.c
       │       └──▶ ljit_ir_append()        // ir/ljit_ir_list.c
       │
       ├──▶ ljit_optimize(ctx)              // optimize/ljit_opt.c
       │       │
       │       ├──▶ ljit_opt_const()        // optimize/ljit_opt_const.c
       │       ├──▶ ljit_opt_dce()          // optimize/ljit_opt_dce.c
       │       └──▶ ljit_opt_peep()         // optimize/ljit_opt_peep.c
       │
       ├──▶ ljit_regalloc(ctx)              // regalloc/ljit_regalloc.c
       │       │
       │       ├──▶ ljit_reg_live()         // regalloc/ljit_reg_live.c
       │       ├──▶ ljit_reg_graph()        // regalloc/ljit_reg_graph.c
       │       └──▶ ljit_reg_color()        // regalloc/ljit_reg_color.c
       │
       ├──▶ ljit_codegen(ctx)               // codegen/ljit_codegen.c
       │       │
       │       ├──▶ ljit_cg_arith()         // codegen/ljit_cg_arith.c
       │       ├──▶ ljit_cg_ctrl()          // codegen/ljit_cg_ctrl.c
       │       └──▶ sljit_emit_*()          // sljit/ljit_sljit.c
       │
       └──▶ ljit_context_destroy(ctx)       // ir/ljit_ir.c
```

---

## 5. 代码规范

### 5.1 文件命名

| 类型 | 模式 | 示例 |
|------|------|------|
| 头文件 | `ljit_<模块>.h` | `ljit_ir.h` |
| 源文件 | `ljit_<模块>.c` | `ljit_ir.c` |
| 子模块文件 | `ljit_<模块>_<功能>.c` | `ljit_cg_arith.c` |

### 5.2 函数命名

| 类型 | 前缀 | 示例 |
|------|------|------|
| 公共 API | `luaJIT_` | `luaJIT_compile()` |
| 模块内部 | `ljit_` | `ljit_ir_create()` |
| 子模块函数 | `ljit_<模块>_` | `ljit_cg_emit_add()` |
| 静态函数 | 无前缀 | `emit_add_instr()` |

### 5.3 注释规范

- 所有公共函数必须有 Doxygen 风格注释
- 复杂算法必须有详细注释（不少于 3 行）
- 关键数据结构必须有字段说明
- 文件头部必须有版权声明和功能描述

---

## 6. 里程碑

| 里程碑 | 时间 | 交付物 |
|--------|------|--------|
| M1: 框架搭建 | 第 2 周 | IR 系统、核心入口、sljit 封装 |
| M2: 前端处理 | 第 5 周 | 字节码翻译、数据流分析 |
| M3: 优化器 | 第 9 周 | 4 个优化 pass |
| M4: 寄存器分配 | 第 13 周 | 完整的寄存器分配器 |
| M5: 代码生成 | 第 17 周 | IR 到机器码翻译 |
| M6: 完整覆盖 | 第 20 周 | 全部字节码支持、测试通过 (已完成) |