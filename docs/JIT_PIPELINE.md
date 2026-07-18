# LXCLUA-NCore JIT 编译管线文档

## 1. JIT 架构概览

### 1.1 完整目录结构

```
src/vm/jit/
├── core/                    # JIT 核心
│   ├── ljit.c               # JIT 编译器主入口：初始化、编译触发、缓存管理
│   ├── ljit.h               # JIT 公共头文件：全局开关、API 声明
│   ├── ljit_debug.h         # JIT 调试宏系统：JIT_VERBOSE_LOG 控制
│   └── ljit_internal.h      # JIT 内部定义（扩展点）
├── frontend/                # 前端：字节码 → IR 转换
│   ├── ljit_analyze.c       # 分析器：类型推断、数据流分析、活跃性分析
│   ├── ljit_analyze.h       # 分析器头文件：类型枚举、分析信息结构体
│   └── ljit_translate.c     # 字节码翻译器：将 Lua 操作码逐条映射为 IR 指令
├── ir/                      # 中间表示 (IR)
│   ├── ljit_ir.c            # IR 指令创建与销毁、上下文管理
│   ├── ljit_ir.h            # IR 核心定义：操作码枚举、操作数类型、节点结构、基本块
│   ├── ljit_ir_list.c       # IR 指令链表管理：追加、插入、删除、遍历
│   ├── ljit_ir_label.c      # IR 标签管理：跳转目标标签分配
│   └── ljit_ir_bb.c         # 基本块管理：BB 构建、CFG 边构建、前驱/后继
├── optimize/                # 优化器
│   ├── ljit_opt.c           # 优化主控：调度各优化遍
│   ├── ljit_opt.h           # 优化器头文件：各优化遍函数声明
│   ├── ljit_opt_const.c     # 常量折叠：编译期计算常量表达式
│   ├── ljit_opt_dce.c       # 死代码消除：移除无副作用且结果未使用的指令
│   ├── ljit_opt_cse.c       # 公共子表达式消除：消除重复计算
│   ├── ljit_opt_peep.c      # 窥孔优化：局部指令模式替换
│   └── ljit_opt_inline.c    # 函数内联：内联简单空函数
├── regalloc/                # 寄存器分配
│   ├── ljit_regalloc.c      # 寄存器分配主控：调度各分配遍
│   ├── ljit_regalloc.h      # 寄存器分配头文件：数据结构定义
│   ├── ljit_reg_live.c      # 活性分析：计算每个 IR 指令的活跃变量区间
│   ├── ljit_reg_graph.c     # 干涉图构建：同活跃变量需要不同寄存器
│   ├── ljit_reg_color.c     # 图着色算法：Chaitin-Briggs 风格分配物理寄存器
│   ├── ljit_reg_spill.c     # 溢出处理：将变量溢出到栈
│   └── ljit_reg_alloc.c     # 寄存器分配执行：应用映射到 IR 节点
├── codegen/                 # 代码生成
│   ├── ljit_codegen.c       # 代码生成主控：遍历 IR 列表，分发到各子模块
│   ├── ljit_codegen.h       # 代码生成头文件：各子模块函数声明、icall 辅助函数声明
│   ├── ljit_cg_arith.c      # 算术运算代码生成 (ADD/SUB/MUL/DIV/MOD/IDIV/POW/UNM/NOT/位运算)
│   ├── ljit_cg_ctrl.c       # 控制流代码生成 (JMP/CJMP/RET/FORPREP/FORLOOP 等)
│   ├── ljit_cg_table.c      # 表操作代码生成 (GETTABLE/SETTABLE/NEWTABLE/GETI/SETI 等)
│   ├── ljit_cg_conv.c       # 类型转换代码生成
│   ├── ljit_cg_closure.c    # 闭包代码生成
│   └── ljit_cg_oop.c        # 面向对象代码生成 (类、继承、方法调用)
└── sljit/                   # SLJIT 后端适配层
    ├── ljit_sljit.c         # SLJIT 绑定层
    ├── ljit_sljit.h         # SLJIT 头文件（包含 sljitLir.h）
    └── ljit_sljit_mac.h     # SLJIT 宏定义扩展
```

### 1.2 各模块职责简述

| 模块 | 职责 |
|------|------|
| **core/** | JIT 引擎生命周期管理、编译触发（热点阈值）、统计计数、Lua 侧 `jit` 模块接口 |
| **frontend/** | 字节码 → IR 的翻译和前置分析，包括类型推断、数据流分析、自递归检测 |
| **ir/** | IR 中间表示定义：指令类型、操作数、基本块、CFG、指令链表、标签 |
| **optimize/** | 5 种优化遍：常量折叠、死代码消除、公共子表达式消除、窥孔优化、函数内联 |
| **regalloc/** | 图着色寄存器分配：活性分析 → 干涉图 → 图着色 → 溢出处理 → 应用映射 |
| **codegen/** | 遍历 IR 节点，调用 SLJIT API 生成原生机器码，调用 icall 辅助函数处理复杂操作 |
| **sljit/** | SLJIT 后端的薄封装层，提供跨平台原生代码生成能力 |

### 1.3 编译管线流程图

```
                        ┌─────────────────────────────────────────────────────────────────────┐
                        │                         JIT 编译管线                                 │
                        └─────────────────────────────────────────────────────────────────────┘

  Lua 字节码
  (Proto->code)
      │
      │  luaJIT_compile()
      │
      ▼
┌──────────────────────────────────────────────────────────────────────────────────────────────┐
│                                     Phase 1: 前端分析                                        │
│                                                                                              │
│  ┌───────────────────────┐         ┌──────────────────────────┐                              │
│  │    ljit_analyze()     │         │    ljit_translate()       │                              │
│  │                        │         │                           │                              │
│  │  • 数据流分析          │         │  • 逐 BB 遍历字节码       │                              │
│  │  • 类型推断            │  ───→   │  • 操作码 → IR 指令映射   │                              │
│  │  • 活跃性分析          │         │  • 自递归检测              │                              │
│  │  • CFG 构建 (BB)       │         │  • 附加类型信息 (flags)    │                              │
│  └───────────────────────┘         └──────────┬───────────────┘                              │
│                                               │                                              │
└───────────────────────────────────────────────┼──────────────────────────────────────────────┘
                                                │
                                                │ IR 指令链表 (ljit_ir_node_t 双向链表)
                                                │ + CFG 基本块 (ljit_bb_t 链表)
                                                ▼
┌──────────────────────────────────────────────────────────────────────────────────────────────┐
│                                     Phase 2: 优化                                            │
│                                                                                              │
│  ┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐    │
│  │ ljit_opt_const()│    │  ljit_opt_cse() │    │ ljit_opt_peep() │    │ ljit_opt_dce()  │    │
│  │                 │    │                 │    │                 │    │                 │    │
│  │ • 常量折叠       │───→│ • 公共子表达式   │───→│ • 冗余 MOV 消除  │───→│ • 死代码消除     │    │
│  │ • ADD/SUB/MUL/  │    │   消除           │    │ • 恒等运算简化   │    │ • 未使用 LOADI   │    │
│  │   DIV 常量计算   │    │ • 算术运算去重   │    │ • ADD+0→MOV     │    │   移除           │    │
│  └─────────────────┘    └─────────────────┘    └─────────────────┘    └─────────────────┘    │
│                                                                                              │
│  ┌──────────────────────┐                                                                   │
│  │  ljit_opt_inline()   │                                                                   │
│  │                      │                                                                   │
│  │ • 内联空函数体        │                                                                   │
│  │   (OP_RETURN0)       │                                                                   │
│  └──────────────────────┘                                                                   │
│                                                                                              │
└──────────────────────────────────────────────────────────────────────────────────────────────┘
                                                │
                                                │ 优化后的 IR 指令链表
                                                ▼
┌──────────────────────────────────────────────────────────────────────────────────────────────┐
│                                  Phase 3: 寄存器分配                                          │
│                                                                                              │
│  ┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐    │
│  │ ljit_reg_live() │    │ ljit_reg_graph()│    │ ljit_reg_color()│    │ ljit_reg_spill()│    │
│  │                 │    │                 │    │                 │    │                 │    │
│  │ • 活跃区间分析   │───→│ • 干涉图构建     │───→│ • Chaitin-Briggs│───→│ • 溢出槽分配     │    │
│  │ • 首次定义/使用   │    │ • 区间重叠 → 边  │    │   图着色        │    │ • stack_ofs 计算 │    │
│  │ • live-in 检测   │    │ • live-in 互补   │    │ • 4 物理寄存器   │    │                 │    │
│  └─────────────────┘    └─────────────────┘    └────────┬────────┘    └─────────────────┘    │
│                                                         │                                    │
│                                            ┌────────────▼────────────┐                       │
│                                            │  ljit_reg_alloc_process()│                       │
│                                            │                          │                       │
│                                            │  • 将映射应用到 IR 节点   │                       │
│                                            │    (is_spilled, phys_reg, │                       │
│                                            │     stack_ofs)            │                       │
│                                            └──────────────────────────┘                       │
│                                                                                              │
└──────────────────────────────────────────────────────────────────────────────────────────────┘
                                                │
                                                │ 带寄存器映射的 IR 指令链表
                                                ▼
┌──────────────────────────────────────────────────────────────────────────────────────────────┐
│                                  Phase 4: 代码生成                                            │
│                                                                                              │
│  ┌───────────────────────────────────────────────────────────────────────────────────────┐   │
│  │                              ljit_codegen()                                            │   │
│  │                                                                                        │   │
│  │  sljit_create_compiler() → 创建 SLJIT 编译器实例                                        │   │
│  │  sljit_emit_enter()      → 函数序言                                                     │   │
│  │  live-in 参数加载         → 从 Lua 栈加载参数到物理寄存器                                 │   │
│  │  rec_entry_label          → 创建自递归入口标签                                           │   │
│  │                                                                                        │   │
│  │  ┌─────────────────────────────────────────────────────────────────────────────────┐    │   │
│  │  │                       遍历 IR 链表，分发到各子模块                                │    │   │
│  │  │                                                                                 │    │   │
│  │  │  IR_ADD/SUB/MUL/...  ──→ ljit_cg_arith.c   (算术运算)                           │    │   │
│  │  │  IR_JMP/CJMP/RET/...  ──→ ljit_cg_ctrl.c    (控制流)                            │    │   │
│  │  │  IR_GETTABLE/...      ──→ ljit_cg_table.c   (表操作)                            │    │   │
│  │  │  类型转换 IR           ──→ ljit_cg_conv.c    (类型转换)                           │    │   │
│  │  │  IR_CLOSURE            ──→ ljit_cg_closure.c (闭包)                              │    │   │
│  │  │  IR_NEWCLASS/...       ──→ ljit_cg_oop.c     (OOP)                               │    │   │
│  │  └─────────────────────────────────────────────────────────────────────────────────┘    │   │
│  │                                                                                        │   │
│  │  sljit_generate_code() → 生成原生机器码                                                  │   │
│  └───────────────────────────────────────────────────────────────────────────────────────┘   │
│                                                                                              │
└──────────────────────────────────────────────────────────────────────────────────────────────┘
                                                │
                                                │ 原生机器码 (void *code)
                                                ▼
                                      ┌──────────────────┐
                                      │ p->jit_trace =   │
                                      │     code          │
                                      │ (存入 Proto)      │
                                      └──────────────────┘
```

---

## 2. JIT 核心 (core/)

### 2.1 ljit.c / ljit.h — JIT 引擎初始化与编译入口

#### 全局变量

| 变量 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `XCLUA_JIT_ENABLED` | `int` | `0` | JIT 总开关，0=禁用，1=启用 |
| `XCLUA_REGEX_JIT_ENABLED` | `int` | `0` | PCRE2 正则 JIT 独立开关 |
| `XCLUA_PCRE2_ENABLED` | `int` | `0` | 是否启用 PCRE2 正则引擎 |
| `XCLUA_JIT_HOTCOUNT` | `int` | `56` | 热点阈值：函数被调用多少次后触发 JIT 编译（与 LuaJIT 默认值一致） |

#### 内部统计计数器

| 变量 | 说明 |
|------|------|
| `jit_compile_ok` | 成功编译次数 |
| `jit_compile_fail` | 编译失败次数 |
| `jit_fallback_count` | JIT 代码回退到解释器次数 |

#### 核心 API

```c
// 初始化（当前为空实现，无全局状态）
void luaJIT_init(lua_State *L);

// 清理（当前为空实现）
void luaJIT_free(lua_State *L);

// 编译单个 Proto：执行完整的 JIT 编译管线，返回 1=成功 0=失败
int luaJIT_compile(lua_State *L, Proto *p);

// 释放编译后的原生代码
void luaJIT_free_trace(lua_State *L, void *trace);

// 启用/禁用 JIT
void luaJIT_enable(void);
void luaJIT_disable(void);

// 记录 JIT 回退事件
void luaJIT_record_fallback(void);
```

#### 编译入口 `luaJIT_compile()` 流程

```
1. 检查 p->jit_trace 或 p->jit_failed
   └→ 如果已有 trace 或已标记失败，跳过编译

2. ljit_context_create(L, p)  ──→ 创建编译上下文

3. ljit_analyze(ctx)          ──→ Phase 1a: 分析
4. ljit_translate(ctx)        ──→ Phase 1b: 翻译
5. ljit_optimize(ctx)         ──→ Phase 2:  优化
6. ljit_regalloc(ctx)         ──→ Phase 3:  寄存器分配
7. ljit_codegen(ctx)          ──→ Phase 4:  代码生成

8. ljit_context_destroy(ctx)  ──→ 销毁上下文

9. 成功: p->jit_trace = code, jit_compile_ok++
   失败: p->jit_failed = 1, jit_compile_fail++
```

#### Lua 侧 `jit` 模块接口

| Lua 函数 | 说明 |
|----------|------|
| `jit.on()` | 启用 JIT |
| `jit.off()` | 禁用 JIT |
| `jit.status()` | 返回 JIT 是否启用 (boolean) |
| `jit.stats()` | 返回统计表：`{compiled, failed, fallback, self_calls, hotcount}` |
| `jit.threshold([n])` | 获取/设置热点阈值 |
| `jit.hotcount([func])` | 获取指定函数的热点计数 |
| `jit.regex.on()` / `.off()` / `.status()` | PCRE2 正则 JIT 控制 |
| `jit.regex.pcre2.on()` / `.off()` / `.status()` | PCRE2 引擎开关 |

### 2.2 ljit_debug.h — JIT 调试宏系统

#### JIT_VERBOSE_LOG 机制

```c
#ifdef JIT_VERBOSE_LOG
#define JIT_DBG(mod, fmt, ...) \
    do { \
        fprintf(stderr, "[%s] [%s] " fmt "\n", jit_timestamp(), mod, ##__VA_ARGS__); \
        fflush(stderr); \
    } while(0)
#else
#define JIT_DBG(mod, fmt, ...) do {} while(0)
#endif
```

- 编译时定义 `JIT_VERBOSE_LOG` 宏启用调试输出
- 未定义时，所有 `JIT_DBG()` 调用被编译为空操作，零开销

#### 时间戳格式

`[HH:MM:SS.mmm]` — 基于 `clock()` 的高精度计时，格式化为时:分:秒.毫秒

#### 模块标识常量

| 标识符 | 值 | 对应模块 |
|--------|-----|----------|
| `MOD_CORE` | `"JIT"` | JIT 核心 |
| `MOD_CTL` | `"JIT-CTL"` | JIT 控制（启用/禁用） |
| `MOD_TR` | `"JIT-TR"` | 字节码翻译 |
| `MOD_ANALYZE` | `"JIT-ANA"` | 分析器 |
| `MOD_OPT` | `"JIT-OPT"` | 优化主控 |
| `MOD_OPT_CONST` | `"JIT-OPT-CONST"` | 常量折叠 |
| `MOD_OPT_CSE` | `"JIT-OPT-CSE"` | 公共子表达式消除 |
| `MOD_OPT_DCE` | `"JIT-OPT-DCE"` | 死代码消除 |
| `MOD_OPT_PEEP` | `"JIT-OPT-PEEP"` | 窥孔优化 |
| `MOD_OPT_INLINE` | `"JIT-OPT-INLINE"` | 函数内联 |
| `MOD_CG` | `"JIT-CG"` | 代码生成主控 |
| `MOD_CG_ARITH` | `"JIT-CG-ARITH"` | 算术代码生成 |
| `MOD_CG_CTRL` | `"JIT-CG-CTRL"` | 控制流代码生成 |
| `MOD_CG_CALL` | `"JIT-CG-CALL"` | 调用代码生成 |
| `MOD_CG_TABLE` | `"JIT-CG-TABLE"` | 表操作代码生成 |
| `MOD_CG_CONV` | `"JIT-CG-CONV"` | 类型转换代码生成 |
| `MOD_CG_CLOS` | `"JIT-CG-CLOS"` | 闭包代码生成 |
| `MOD_CG_OOP` | `"JIT-CG-OOP"` | OOP 代码生成 |
| `MOD_DBG` | `"JIT-DBG"` | 调试 |
| `MOD_REG` | `"JIT-REG"` | 寄存器分配主控 |
| `MOD_REG_LIVE` | `"JIT-REG-LIVE"` | 活性分析 |
| `MOD_REG_GRAPH` | `"JIT-REG-GRAPH"` | 干涉图构建 |
| `MOD_REG_COLOR` | `"JIT-REG-COLOR"` | 图着色 |
| `MOD_REG_SPILL` | `"JIT-REG-SPILL"` | 溢出处理 |
| `MOD_IR` | `"JIT-IR"` | IR 上下文管理 |
| `MOD_IR_LIST` | `"JIT-IR-LIST"` | IR 指令链表 |
| `MOD_IR_LABEL` | `"JIT-IR-LABEL"` | IR 标签 |
| `MOD_IR_BB` | `"JIT-IR-BB"` | IR 基本块 |

#### 调试输出示例

```
[00:00:01.234] [JIT] compiling, sizecode=156, maxstacksize=12
[00:00:01.235] [JIT] analyze...
[00:00:01.236] [JIT-ANA] dataflow analysis: sizecode=156, max_regs=12
[00:00:01.237] [JIT] translate...
[00:00:01.238] [JIT-IR-BB] built 8 basic blocks
[00:00:01.239] [JIT-IR-BB] building CFG edges...
[00:00:01.240] [JIT] optimize...
[00:00:01.241] [JIT-OPT] constant folding...
[00:00:01.242] [JIT-OPT] CSE...
[00:00:01.243] [JIT-OPT] peephole...
[00:00:01.244] [JIT-OPT] DCE...
[00:00:01.245] [JIT-OPT] inlining...
[00:00:01.246] [JIT] regalloc...
[00:00:01.247] [JIT-REG] live interval analysis...
[00:00:01.248] [JIT-REG] interference graph...
[00:00:01.249] [JIT-REG] graph coloring...
[00:00:01.250] [JIT-REG] spill handling...
[00:00:01.251] [JIT-REG] applying mappings...
[00:00:01.252] [JIT] codegen...
[00:00:01.253] [JIT-CG] codegen start, ir_head=0x...
[00:00:01.260] [JIT] codegen done, code=0x...
[00:00:01.261] [JIT] compile OK, code=0x..., total_ok=1
```

### 2.3 ljit_internal.h — JIT 内部定义

当前为薄层头文件，包含 `ljit.h` 并提供扩展点。未来可在此定义 BCPos、BCOpLine 等核心数据结构。

---

## 3. IR 设计 (ir/)

### 3.1 IR 指令类型 (ljit_ir_op_t 枚举)

IR 指令按功能分为以下几类：

#### 数据移动

| 指令 | 说明 |
|------|------|
| `IR_NOP` | 空操作（窥孔优化后标记删除的指令） |
| `IR_MOV` | 寄存器间移动 |
| `IR_LOADK` | 从常量表加载 |
| `IR_LOADI` | 加载立即数整数 |
| `IR_LOADF` | 加载浮点数 |
| `IR_LOADNIL` | 加载 nil |
| `IR_LOADBOOL` | 加载布尔值 |
| `IR_LOADKX` | 加载扩展常量 |
| `IR_SELF` | 加载 self（obj:method 语法） |

#### 算术运算

| 指令 | 说明 |
|------|------|
| `IR_ADD` / `IR_SUB` / `IR_MUL` / `IR_DIV` | 四则运算 |
| `IR_IDIV` | 整数除法 |
| `IR_MOD` | 取模 |
| `IR_POW` | 幂运算 |
| `IR_UNM` | 取负 |
| `IR_ADDK` / `IR_SUBK` / `IR_MULK` / `IR_DIVK` / `IR_MODK` / `IR_POWK` / `IR_IDIVK` | 带常量操作数的算术运算 |

#### 位运算

| 指令 | 说明 |
|------|------|
| `IR_BAND` / `IR_BOR` / `IR_BXOR` | 按位与/或/异或 |
| `IR_SHL` / `IR_SHR` | 左移/右移 |
| `IR_BNOT` | 按位取反 |
| `IR_BANDK` / `IR_BORK` / `IR_BXORK` | 带常量操作数的位运算 |

#### 比较运算

| 指令 | 说明 |
|------|------|
| `IR_CMP_LT` / `IR_CMP_LE` | 小于/小于等于 |
| `IR_CMP_EQ` | 等于 |
| `IR_CMP_GT` / `IR_CMP_GE` | 大于/大于等于 |
| `IR_EQK` | 与常量比较 |
| `IR_SPACESHIP` | 三元比较 (<=>) |
| `IR_IS` | 类型/身份检查 |

#### 逻辑运算

| 指令 | 说明 |
|------|------|
| `IR_NOT` | 逻辑非 |
| `IR_TEST` / `IR_TESTSET` | 布尔测试 |
| `IR_TESTNIL` | nil 测试 |

#### 控制流

| 指令 | 说明 |
|------|------|
| `IR_JMP` | 无条件跳转 |
| `IR_CJMP` | 条件跳转 |
| `IR_RET` | 返回 |
| `IR_CASE` | switch-case 分支 |

#### 表操作

| 指令 | 说明 |
|------|------|
| `IR_NEWTABLE` | 创建新表 |
| `IR_GETTABLE` / `IR_SETTABLE` | 表读写 |
| `IR_GETI` / `IR_SETI` | 整数键表读写 |
| `IR_GETFIELD` / `IR_SETFIELD` | 字符串键表读写 |
| `IR_GETTABUP` / `IR_SETTABUP` | 上值表读写 |
| `IR_SETLIST` | 批量设置表数组部分 |
| `IR_SLICE` | 数组切片 |

#### Map 操作

| 指令 | 说明 |
|------|------|
| `IR_NEWMAP` | 创建新 Map |
| `IR_GETMAP` / `IR_SETMAP` | Map 读写 |

#### 函数调用与闭包

| 指令 | 说明 |
|------|------|
| `IR_CALL` | 函数调用 |
| `IR_CLOSURE` | 创建闭包 |
| `IR_GETUPVAL` / `IR_SETUPVAL` | 上值读写 |

#### 循环

| 指令 | 说明 |
|------|------|
| `IR_FORPREP` / `IR_FORLOOP` | 数值 for 循环 |
| `IR_TFORPREP` / `IR_TFORCALL` / `IR_TFORLOOP` | 通用 for 循环 |

#### 变长参数

| 指令 | 说明 |
|------|------|
| `IR_VARARG` / `IR_VARARGPREP` | 变长参数处理 |
| `IR_GETVARG` | 获取变长参数 |

#### 面向对象

| 指令 | 说明 |
|------|------|
| `IR_NEWCLASS` / `IR_NEWOBJ` | 创建类/对象 |
| `IR_INHERIT` | 继承 |
| `IR_GETSUPER` | 获取父类方法 |
| `IR_SETMETHOD` / `IR_SETSTATIC` | 设置方法/静态成员 |
| `IR_GETPROP` / `IR_SETPROP` | 属性读写 |
| `IR_INSTANCEOF` | 类型检查 |
| `IR_IMPLEMENT` | 接口实现 |
| `IR_SETIFACEFLAG` | 设置接口标志 |
| `IR_ADDMETHOD` | 添加方法 |
| `IR_IN` | in 运算符 |

#### 命名空间与概念

| 指令 | 说明 |
|------|------|
| `IR_NEWCONCEPT` | 创建概念 |
| `IR_NEWNAMESPACE` | 创建命名空间 |
| `IR_LINKNAMESPACE` | 链接命名空间 |
| `IR_NEWSUPER` / `IR_SETSUPER` | 超级结构体 |

#### 异步与特殊

| 指令 | 说明 |
|------|------|
| `IR_ASYNCWRAP` | 异步包装 |
| `IR_AWAIT` | await 操作 |
| `IR_GENERICWRAP` | 泛型包装 |
| `IR_CHECKTYPE` | 类型检查 |
| `IR_EXTRAARG` | 额外参数 |

#### LXCLUA 扩展

| 指令 | 说明 |
|------|------|
| `IR_SETTRAITFLAG` | 设置 Trait 标志 |
| `IR_SETTRAITREQUIRE` | 设置 Trait 要求 |
| `IR_USETRAIT` | 使用 Trait |
| `IR_MERGE` | 表合并 |
| `IR_REGEX` | 正则匹配 |

### 3.2 IR 操作数类型 (ljit_ir_val_t)

```c
typedef enum {
    IR_VAL_NONE = 0,   // 无操作数
    IR_VAL_REG,        // 虚拟寄存器 / 局部变量
    IR_VAL_CONST,      // 常量表索引
    IR_VAL_UPVAL,      // 上值索引
    IR_VAL_INT,        // 立即数整数
    IR_VAL_NUM,        // 立即数浮点数
    IR_VAL_LABEL       // 跳转标签
} ljit_ir_val_type_t;
```

每个操作数 (`ljit_ir_val_t`) 包含：
- **类型** (`type`)：上述枚举之一
- **值** (`v`)：联合体，根据类型存储 reg/k/uv/i/n/label_id
- **物理映射**：`is_spilled`（是否溢出到栈）、`phys_reg`（物理寄存器 ID）、`stack_ofs`（栈偏移）

### 3.3 IR 基本块 (ljit_ir_bb.c)

基本块是连续执行的指令序列，入口为第一条指令，出口为最后一条指令。

#### 数据结构

```c
typedef struct ljit_bb {
    int start_pc;        // BB 起始字节码 PC
    int end_pc;          // BB 结束字节码 PC
    int bb_id;           // BB 编号，从 0 开始
    struct ljit_bb *next; // 链表后继
    struct ljit_bb **preds; // 前驱 BB 数组（动态扩容）
    int pred_count;      // 前驱数量
    int pred_cap;        // 前驱容量
    struct ljit_bb **succs; // 后继 BB 数组（动态扩容）
    int succ_count;      // 后继数量
    int succ_cap;        // 后继容量
} ljit_bb_t;
```

#### BB 构建算法 `ljit_ir_bb_build()`

1. **Leader 标记**：扫描所有字节码指令，标记基本块入口：
   - 第一条指令始终是 leader
   - 跳转目标（`OP_JMP`、条件跳转后的目标）
   - 条件跳转的下一条指令（PC+1）
   - 循环跳转的目标和 fall-through
   - 返回/尾调用后的下一条指令

2. **BB 链表构建**：根据 leader 标记，将连续的指令分组为 BB

3. **CFG 边构建**：根据 BB 的最后一条指令类型，建立前驱/后继关系：
   - 无条件跳转 → 后继是跳转目标 BB
   - 条件跳转 → 后继是 true 分支 BB 和 false 分支 BB
   - 循环跳转 → 后继是循环体 BB 和循环出口 BB
   - 返回/尾调用 → 无后继
   - 其他 → 后继是下一个 BB

4. **动态扩容**：前驱/后继数组初始容量为 4，满时翻倍扩容，自动去重。

### 3.4 IR 标签 (ljit_ir_label.c)

标签用于 IR 中跳转指令的目标地址。

```c
int ljit_ir_new_label(ljit_ctx_t *ctx);
```

- 每次调用返回唯一的递增标签 ID（从 0 开始）
- 标签 ID 存储在 `IR_VAL_LABEL` 类型操作数的 `v.label_id` 字段中

### 3.5 IR 指令链表 (ljit_ir_list.c)

IR 指令以双向链表形式组织，通过 `ljit_ctx_t` 的 `ir_head` 和 `ir_tail` 指针管理。

```c
void ljit_ir_append(ljit_ctx_t *ctx, ljit_ir_node_t *node);
```

- 将新 IR 节点追加到链表尾部
- 自动维护 `prev`/`next` 指针
- 首个节点同时设置 `ir_head` 和 `ir_tail`

### 3.6 IR 指令 (ljit_ir.c)

#### 上下文管理

```c
void *ljit_context_create(lua_State *L, Proto *proto);
void ljit_context_destroy(void *ctx);
```

`ljit_ctx_t` 结构体包含：

| 字段 | 说明 |
|------|------|
| `L` | Lua 状态 |
| `proto` | 正在编译的函数原型 |
| `cfg` | CFG 基本块链表头 |
| `ir_head` / `ir_tail` | IR 指令双向链表头尾 |
| `next_label_id` | 下一个标签 ID |
| `compiler` | SLJIT 编译器实例 (codegen 阶段使用) |
| `labels` / `jumps` / `jump_targets` | SLJIT 标签和跳转管理 |
| `analyze_info` | 分析信息 (ljit_analyze_info_t) |
| `regalloc_info` | 寄存器分配信息 (ljit_regalloc_info_t) |
| `rec_ret_stack` / `rec_ret_top` | 自递归返回地址栈 (MAX_REC_DEPTH=256) |
| `rec_entry_label` | 自递归函数入口标签 |

#### IR 节点创建

```c
ljit_ir_node_t *ljit_ir_new(ljit_ir_op_t op, int pc);
```

- 分配并初始化 IR 节点
- `calloc` 零初始化，默认 `dest`/`src1`/`src2` 类型为 `IR_VAL_NONE`
- 设置 `original_pc` 映射回原始字节码 PC

#### `ljit_ir_node_t` 结构

| 字段 | 说明 |
|------|------|
| `op` | IR 操作码 |
| `dest` | 目标操作数 |
| `src1` | 源操作数 1 |
| `src2` | 源操作数 2 |
| `original_pc` | 映射回原始字节码 PC |
| `self_rec` | 自递归标记：1=IR_CALL 目标与当前函数相同 |
| `flags` | 类型推断结果，低 4 位存储目标类型 (ljit_type_t) |
| `prev` / `next` | 双向链表指针 |

---

## 4. 前端翻译 (frontend/)

### 4.1 ljit_analyze.c — 分析器

#### 类型推断

分析器通过 `infer_dest_type()` 函数根据操作码推断目标寄存器的类型：

| 操作码 | 推断类型 |
|--------|----------|
| `OP_LOADI` | `JIT_TYPE_INT` |
| `OP_LOADF` | `JIT_TYPE_NUM` |
| `OP_LOADK` (整数) | `JIT_TYPE_INT` |
| `OP_LOADK` (浮点) | `JIT_TYPE_NUM` |
| `OP_LOADK` (字符串) | `JIT_TYPE_STR` |
| `OP_LOADK` (布尔) | `JIT_TYPE_BOOL` |
| `OP_LOADK` (表) | `JIT_TYPE_TAB` |
| `OP_LOADK` (函数) | `JIT_TYPE_FUNC` |
| `OP_LOADFALSE` / `OP_LOADTRUE` | `JIT_TYPE_BOOL` |
| `OP_NEWTABLE` / `OP_NEWMAP` | `JIT_TYPE_TAB` |
| `OP_CLOSURE` | `JIT_TYPE_FUNC` |
| 算术运算 (ADD/SUB/...) | `JIT_TYPE_NUM` |
| 整数运算 (IDIV/BAND/...) | `JIT_TYPE_INT` |
| `OP_NOT` | `JIT_TYPE_BOOL` |
| `OP_CONCAT` | `JIT_TYPE_STR` |
| `OP_LEN` | `JIT_TYPE_INT` |
| `OP_MOVE` | 继承源寄存器类型 |
| `OP_GETTABLE` 等 | `JIT_TYPE_ANY`（保守） |

#### 类型枚举 (ljit_type_t)

```c
typedef enum {
    JIT_TYPE_ANY = 0,   // 未知类型（保守）
    JIT_TYPE_NIL,       // nil
    JIT_TYPE_BOOL,      // 布尔
    JIT_TYPE_INT,       // 整数
    JIT_TYPE_NUM,       // 浮点数
    JIT_TYPE_STR,       // 字符串
    JIT_TYPE_TAB,       // 表
    JIT_TYPE_FUNC,      // 函数
    JIT_TYPE_USERDATA   // 用户数据
} ljit_type_t;
```

#### 数据流分析 `ljit_analyze_dataflow()`

- 线性扫描字节码，记录每个寄存器的定义 PC (`def_pc`)
- 标记活跃寄存器 (`is_live`)
- 支持的操作码包括：MOVE, LOADI, LOADF, LOADK, LOADKX, LOADFALSE, LOADTRUE, LOADNIL, GETUPVAL, GETTABUP, GETTABLE, GETI, GETFIELD, NEWTABLE, 算术运算, 位运算, 逻辑运算, LEN, CONCAT, CALL, TAILCALL 等

#### 分析信息结构 (ljit_analyze_info_t)

| 字段 | 说明 |
|------|------|
| `max_regs` | 虚拟寄存器数量 (proto->maxstacksize) |
| `reg_types` | 各虚拟寄存器的推断类型数组 |
| `def_pc` | 各寄存器最后定义 PC |
| `is_live` | 各寄存器是否活跃 |
| `num_bbs` | BB 数量 |
| `in_types` | 扁平数组：`num_bbs * max_regs`，每个 BB 入口类型 |
| `out_types` | 扁平数组：`num_bbs * max_regs`，每个 BB 出口类型 |

### 4.2 ljit_translate.c — 字节码翻译器

#### 翻译流程

```
1. 遍历每个 BB（按 cfg 链表顺序）
2.   BB 入口：初始化 state 数组为该 BB 的入口类型（来自 CFG 数据流分析）
3.   BB 内逐 PC 翻译：
4.     switch (op) {
5.       case OP_MOVE:    → 创建 IR_MOV 节点
6.       case OP_LOADI:   → 创建 IR_LOADI 节点
7.       case OP_LOADF:   → 创建 IR_LOADF 节点
8.       case OP_LOADK:   → 创建 IR_LOADK 节点
9.       case OP_ADD:     → 创建 IR_ADD 节点
10.       case OP_CALL:    → 创建 IR_CALL 节点 + 自递归检测
11.       ...
12.     }
13.     更新 state 数组（类型传播）
14.     ljit_ir_append() 将节点加入 IR 链表
```

#### 自递归优化

当检测到 `OP_CALL` 的目标函数与当前函数相同时，标记 `node->self_rec = 1`。检测方式：

1. **CLOSURE 检测**：向前扫描，找到写入 func_reg 的 `OP_CLOSURE`，检查其 `bx` 索引的子 Proto 是否等于 `ctx->proto`
2. **UPVAL 检测**：向前扫描，找到 `OP_GETUPVAL`，然后追踪上值链，检测是否最终指向当前函数的闭包
3. **R0 隐式检测**：如果 `func_reg == 0` 且没有找到显式写入，标记为自递归（R0 常用于递归调用）

自递归标记在代码生成阶段用于直接跳转到函数入口标签，跳过 C 函数调用开销。

---

## 5. 优化遍 (optimize/)

优化调度顺序（`ljit_optimize()`）：

```
ljit_opt_const()  →  常量折叠
ljit_opt_cse()    →  公共子表达式消除
ljit_opt_peep()   →  窥孔优化
ljit_opt_dce()    →  死代码消除
ljit_opt_inline() →  函数内联
```

### 5.1 ljit_opt_const.c — 常量折叠

- 遍历 IR 链表，检测 `IR_ADD`/`IR_SUB`/`IR_MUL`/`IR_DIV` 指令
- 向前搜索（在同一基本块内）两个源操作数的最近赋值
- 如果两个操作数都是 `IR_LOADI` 常量，在编译期计算结果
- 将原算术指令替换为 `IR_LOADI`（直接加载常量结果）
- 遇到控制流指令（`IR_JMP`/`IR_CJMP`/`IR_RET`）时停止搜索（安全边界）

### 5.2 ljit_opt_cse.c — 公共子表达式消除

- 遍历 IR 链表，对算术指令（`IR_ADD` 到 `IR_SHR`）去重
- 向前搜索相同操作码和相同操作数的指令
- 检查是否存在寄存器冲突（中间指令修改了操作数寄存器）
- 如果找到匹配，将当前指令替换为 `IR_MOV`（复用之前的结果）
- 遇到控制流指令或 `IR_CALL` 时停止搜索

### 5.3 ljit_opt_peep.c — 窥孔优化

局部指令模式替换，简化冗余操作：

| 模式 | 替换 |
|------|------|
| `IR_MOV R, R` | → `IR_NOP`（自赋值消除） |
| `IR_ADD x, 0` | → `IR_MOV x`（加零消除） |
| `IR_SUB x, 0` | → `IR_MOV x`（减零消除） |
| `IR_MUL x, 1` | → `IR_MOV x`（乘一消除） |
| `IR_MUL x, 0` | → `IR_LOADI 0`（乘零消除） |
| `IR_DIV x, 1` | → `IR_MOV x`（除一消除） |

### 5.4 ljit_opt_dce.c — 死代码消除

- 从后向前遍历 IR 链表，检测无副作用指令
- 当前仅安全消除 `IR_LOADI`/`IR_LOADF`/`IR_LOADK`/`IR_MOV`
- 检查目标寄存器是否在后续被使用
- 遇到控制流指令、循环指令、`IR_CALL` 时假设被使用（保守策略）
- 表变异操作（SETI/SETTABLE/SETFIELD/SETTABUP）视为"使用"了目标寄存器
- 迭代执行直到没有变化（`changed` 标志）

### 5.5 ljit_opt_inline.c — 函数内联

- 检测 `IR_CALL` 指令，向前追踪闭包来源
- 如果闭包指向一个子 Proto 且子 Proto 只有一条 `OP_RETURN0` 指令
- 且调用方期望 0 个返回值
- 则将 `IR_CALL` 替换为 `IR_NOP`（空函数调用直接消除）

---

## 6. 后端代码生成 (codegen/)

### 6.1 ljit_codegen.c — 代码生成主控

#### `ljit_codegen()` 流程

```
1. sljit_create_compiler()           → 创建 SLJIT 编译器实例
2. sljit_emit_enter()                → 函数序言（设置栈帧、保存寄存器）
3. sljit_emit_op1(SLJIT_MOV, S1, 0)  → 初始化 S1=0（返回值寄存器）
4. live-in 参数加载                    → 扫描 IR，找出首次使用先于首次定义的寄存器
   ├─ 第一遍：记录 first_def / first_use 序列号
   └─ 第二遍：加载 live-in 寄存器
      ├─ spilled 寄存器：直接通过 Lua 栈访问（stack_ofs 指向栈内存）
      └─ 非 spilled 寄存器：sljit_emit_op1(MOV, phys_reg, MEM[S0 + reg*sizeof(TValue)])
5. rec_entry_label = sljit_emit_label() → 创建自递归入口标签
6. 遍历 IR 链表，按 opcode 分发：
   ├─ IR_ADD/SUB/MUL/DIV/...  → ljit_cg_arith.c
   ├─ IR_JMP/CJMP/RET/...      → ljit_cg_ctrl.c
   ├─ IR_GETTABLE/SETTABLE/... → ljit_cg_table.c
   ├─ 类型转换 IR               → ljit_cg_conv.c
   ├─ IR_CLOSURE               → ljit_cg_closure.c
   └─ IR_NEWCLASS/NEWOBJ/...   → ljit_cg_oop.c
7. sljit_generate_code()            → 生成原生机器码
8. 返回 void *code
```

#### 关键细节

- **SLJIT_S0**：基址寄存器，指向 Lua 栈基址
- **SLJIT_S1**：返回值寄存器（0=正常，1=已返回）
- **SLJIT_S2-S5**：通用物理寄存器（图着色分配的目标寄存器）
- **TValue 大小**：`sizeof(TValue)` 用于计算栈偏移
- **返回路径**：内联 `luaD_poscall()` 调用（nresults >= 0 时），避免额外的 C 函数包装

### 6.2 ljit_cg_arith.c — 算术运算代码生成

处理以下 IR 指令的 SLJIT 代码生成：

- 加法/减法/乘法/除法/取模/整数除法/幂运算
- 位运算：AND/OR/XOR/SHL/SHR/BNOT
- 取负/取反
- 数据移动：MOV/LOADI/LOADF/LOADK/LOADNIL/LOADBOOL

### 6.3 ljit_cg_ctrl.c — 控制流代码生成

处理以下 IR 指令的 SLJIT 代码生成：

- `IR_JMP`：无条件跳转
- `IR_CJMP`（`IR_CMP_LT/LE/EQ/GT/GE`）：条件跳转
- `IR_RET`：返回指令，内联 `luaD_poscall()` 调用
- `IR_FORPREP`/`IR_FORLOOP`：数值 for 循环
- `IR_TFORPREP`/`IR_TFORCALL`/`IR_TFORLOOP`：通用 for 循环
- `IR_NOP`：空操作

### 6.4 ljit_cg_table.c — 表操作代码生成

处理以下 IR 指令的 SLJIT 代码生成：

- `IR_GETTABLE`/`IR_SETTABLE`：表读写（通过 icall 辅助函数）
- `IR_NEWTABLE`：创建新表
- `IR_GETMAP`/`IR_SETMAP`/`IR_NEWMAP`：Map 操作
- `IR_GETI`/`IR_SETI`：整数键表读写
- `IR_GETFIELD`/`IR_SETFIELD`：字符串键表读写
- `IR_GETTABUP`/`IR_SETTABUP`：上值表读写
- `IR_SETLIST`：批量设置表数组部分

### 6.5 ljit_cg_conv.c — 类型转换代码生成

处理 Lua 值类型之间的转换。

### 6.6 ljit_cg_closure.c — 闭包代码生成

处理 `IR_CLOSURE`（创建闭包）、`IR_GETUPVAL`/`IR_SETUPVAL`（上值读写）。

### 6.7 ljit_cg_oop.c — 面向对象代码生成

处理 `IR_NEWCLASS`、`IR_NEWOBJ`、`IR_INHERIT`、`IR_GETSUPER` 等 OOP 相关指令。

### 6.8 icall 辅助函数

复杂操作（如表操作、类型转换、对象创建）通过 `SLJIT_FUNC` 声明的 C 辅助函数实现，在 JIT 代码中通过 `sljit_emit_icall()` 调用：

| 函数 | 功能 |
|------|------|
| `ljit_icall_gettable` | 表读取（优化路径：直接索引 + 调用 `luaV_finishget`） |
| `ljit_icall_settable` | 表写入（优化路径 + `luaV_finishset`） |
| `ljit_icall_newtable` | 创建新表 |
| `ljit_icall_geti` / `ljit_icall_seti` | 整数键表读写 |
| `ljit_icall_getfield` / `ljit_icall_setfield` | 字符串键表读写 |
| `ljit_icall_getupval` / `ljit_icall_setupval` | 上值读写 |
| `ljit_icall_gettabup` / `ljit_icall_settabup` | 上值表读写 |
| `ljit_icall_getmap` / `ljit_icall_setmap` / `ljit_icall_newmap` | Map 操作 |
| `ljit_icall_concat` | 字符串拼接 |
| `ljit_icall_pow` | 幂运算 |
| `ljit_icall_setlist` | 批量设置表数组 |
| `ljit_icall_testset` | 测试并设置 |
| `ljit_icall_self` | self 语法 |
| `ljit_icall_forprep` / `ljit_icall_forloop` | 数值 for 循环 |
| `ljit_icall_vararg` / `ljit_icall_varargprep` | 变长参数 |
| `ljit_icall_newclass` / `ljit_icall_newobj` | OOP 类/对象创建 |
| `ljit_icall_inherit` / `ljit_icall_getsuper` | OOP 继承 |
| `ljit_icall_asyncwrap` / `ljit_icall_await` | 异步支持 |
| `ljit_icall_settraitflag` / `ljit_icall_settraitrequire` / `ljit_icall_usetrait` | Trait 支持 |
| `ljit_icall_set_integer` / `ljit_icall_set_number` / `ljit_icall_set_nil` / `ljit_icall_set_bool` | 值设置 |
| `ljit_icall_compare` | 通用比较 |
| `ljit_icall_len` | 取长度 |
| `ljit_icall_close` / `ljit_icall_tbc` | 闭包/待关闭变量 |
| `ljit_icall_eqk` / `ljit_icall_test` | 等值比较/测试 |

---

## 7. 寄存器分配 (regalloc/)

### 7.1 整体流程

```
ljit_reg_live()      → 活性分析：计算每个虚拟寄存器的活跃区间
ljit_reg_graph()     → 干涉图构建：区间重叠的寄存器之间添加边
ljit_reg_color()     → 图着色：Chaitin-Briggs 算法分配物理寄存器
ljit_reg_spill()     → 溢出处理：为溢出寄存器分配栈槽位
ljit_reg_alloc_process() → 应用映射：将结果写入 IR 节点的 is_spilled/phys_reg/stack_ofs
```

### 7.2 数据结构 (ljit_regalloc_info_t)

```c
typedef struct {
    ljit_live_interval_t *intervals; // 活跃区间数组 (size = maxstacksize)
    char *interference_graph;        // 干涉图邻接矩阵 (maxstacksize * maxstacksize)
    int *reg_mapping;                // 虚拟寄存器 → 物理寄存器映射
    int *is_spilled;                 // 是否溢出到栈
    int *stack_offsets;              // 溢出栈偏移
    int *is_livein;                  // 是否为 live-in 寄存器
} ljit_regalloc_info_t;
```

### 7.3 ljit_reg_live.c — 活性分析

- 遍历 IR 链表，记录每个虚拟寄存器的首次定义时间 (`first_def`) 和首次使用时间 (`first_use`)
- 计算活跃区间 `[start, end]`：
  - `start`：首次出现（定义或使用）的 IR 序列号
  - `end`：最后一次出现（定义或使用）的 IR 序列号
- 检测 live-in 寄存器：`first_use < first_def`（使用先于定义，即参数寄存器）

### 7.4 ljit_reg_graph.c — 干涉图构建

- 分配 `max_vregs * max_vregs` 的邻接矩阵
- 如果两个寄存器的活跃区间重叠，则它们干涉（需要不同物理寄存器）
- **关键修复**：live-in 寄存器互相干涉——在函数入口处需从 Lua 栈加载，如果共享同一物理寄存器则后加载的会覆盖先加载的

### 7.5 ljit_reg_color.c — 图着色

- 使用 **Chaitin-Briggs** 风格的图着色算法
- 可用物理寄存器：`SLJIT_S2`、`SLJIT_S3`、`SLJIT_S4`、`SLJIT_S5`（共 4 个）
- 算法步骤：
  1. 计算各节点的度数（干涉边数量）
  2. 迭代简化：将度数 < `num_available_regs` 的节点压入栈
  3. 乐观溢出：如果所有节点度数都 >= `num_available_regs`，选择一个节点标记为溢出
  4. 从栈中弹出节点，分配不冲突的颜色
  5. 默认所有寄存器先标记为 spilled，着色成功的标记为 not spilled

### 7.6 ljit_reg_spill.c — 溢出处理

- 为每个虚拟寄存器分配栈偏移：`stack_offsets[i] = i * sizeof(TValue)`
- 溢出槽直接映射到 Lua 栈上的对应位置，无需额外分配栈空间

### 7.7 ljit_reg_alloc.c — 应用映射

- 遍历所有 IR 节点，将寄存器分配结果写入操作数的物理映射字段：
  - `is_spilled`：是否溢出到栈
  - `phys_reg`：物理寄存器 ID（SLJIT_S2-S5 之一）
  - `stack_ofs`：栈偏移（溢出时使用）

---

## 8. SLJIT 后端 (sljit/)

### 8.1 概述

SLJIT（Stack-less JIT）是一个独立的跨平台 JIT 编译后端库，位于 `src/jit/` 目录。LXCLUA-NCore 的 JIT 通过 `src/vm/jit/sljit/` 中的薄封装层使用 SLJIT。

### 8.2 ljit_sljit.h / ljit_sljit.c

- `ljit_sljit.h`：包含 `sljitLir.h`（SLJIT 的 LIR 层 API）和 `ljit_internal.h`
- `ljit_sljit.c`：当前为空实现，作为 SLJIT 绑定层的扩展点

### 8.3 ljit_sljit_mac.h

SLJIT 宏定义扩展，包含 `ljit_sljit.h`。

### 8.4 支持的架构

SLJIT 支持以下架构的原生代码生成：

| 架构 | 位宽 | 说明 |
|------|------|------|
| x86 | 32-bit | IA-32 |
| x86 | 64-bit | x86-64 / AMD64 |
| ARM | 32-bit | ARM / Thumb-2 |
| ARM | 64-bit | AArch64 / ARM64 |
| MIPS | 32-bit | MIPS32 |
| MIPS | 64-bit | MIPS64 |
| PowerPC | 32-bit | PPC-32 |
| PowerPC | 64-bit | PPC-64 |
| RISC-V | 32-bit | RV32 |
| RISC-V | 64-bit | RV64 |
| LoongArch | 64-bit | LA64 |
| S390X | 64-bit | IBM Z |

### 8.5 SLJIT 核心文件

| 文件 | 说明 |
|------|------|
| `sljitLir.c` / `sljitLir.h` | LIR 层：中间表示和编译器 API |
| `sljitNative*.c` | 各架构原生代码生成（x86/ARM/MIPS/PPC/RISC-V/LoongArch/S390X） |
| `allocator_src/` | 可执行内存分配器 |

---

## 9. 调试机制

### 9.1 调试输出格式

```
[HH:MM:SS.mmm] [模块标识] 具体内容
```

### 9.2 启用方式

编译时定义 `JIT_VERBOSE_LOG` 宏：

```c
#define JIT_VERBOSE_LOG
```

或在 Makefile 中添加 `-DJIT_VERBOSE_LOG` 编译选项。

### 9.3 各模块调试输出示例

#### JIT 核心

```
[00:00:01.234] [JIT] compiling, sizecode=156, maxstacksize=12
[00:00:01.234] [JIT] skip: trace=0x..., failed=1
[00:00:01.260] [JIT] compile OK, code=0x..., total_ok=1
[00:00:01.261] [JIT] compile FAILED, total_fail=1
[00:00:01.000] [JIT-CTL] JIT enabled, XCLUA_JIT_ENABLED=1
[00:00:01.000] [JIT-CTL] JIT hotcount threshold set to 100
```

#### 分析器

```
[00:00:01.236] [JIT-ANA] dataflow analysis: sizecode=156, max_regs=12
[00:00:01.236] [JIT-ANA] type inference: sizecode=156, max_regs=12
```

#### 翻译器

```
[00:00:01.237] [JIT-TR] translate sizecode=156, maxstacksize=12
[00:00:01.237] [JIT-TR]   pc=0 op=45 A=0 Bx=0
[00:00:01.237] [JIT-TR] OP_CALL pc=42: detected self-recursion (closure at pc=10)
```

#### IR 模块

```
[00:00:01.237] [JIT-IR] context created: 0x..., proto=0x..., sizecode=156
[00:00:01.237] [JIT-IR-LIST] append first node: op=5, pc=0
[00:00:01.237] [JIT-IR-LABEL] new label: id=0
[00:00:01.238] [JIT-IR-BB] build basic blocks: sizecode=156
[00:00:01.238] [JIT-IR-BB] built 8 basic blocks, head=0x...
[00:00:01.238] [JIT-IR-BB] building CFG edges...
[00:00:01.238] [JIT-IR-BB]   BB0: [0,15] preds=[(none)] succs=[BB1]
[00:00:01.238] [JIT-IR-BB]   BB1: [16,31] preds=[BB0] succs=[BB2,BB3]
```

#### 优化器

```
[00:00:01.241] [JIT-OPT] start
[00:00:01.241] [JIT-OPT] constant folding...
[00:00:01.242] [JIT-OPT-CONST] folded ADD: pc=20, result=42
[00:00:01.242] [JIT-OPT] CSE...
[00:00:01.243] [JIT-OPT-CSE] eliminated duplicate: pc=25, using pc=18
[00:00:01.243] [JIT-OPT] peephole...
[00:00:01.244] [JIT-OPT-PEEP] remove redundant MOV: pc=30, R3 <- R3
[00:00:01.244] [JIT-OPT-PEEP] ADD+0 -> MOV: pc=35, R4
[00:00:01.244] [JIT-OPT] DCE...
[00:00:01.245] [JIT-OPT-DCE] removed dead: pc=12, op=LOADI
[00:00:01.245] [JIT-OPT] inlining...
[00:00:01.245] [JIT-OPT] done
```

#### 寄存器分配

```
[00:00:01.247] [JIT-REG] regalloc start
[00:00:01.247] [JIT-REG] live interval analysis...
[00:00:01.247] [JIT-REG-LIVE] live intervals: R0=[1,42], R1=[3,38], R2=[5,25]
[00:00:01.248] [JIT-REG] interference graph...
[00:00:01.248] [JIT-REG-GRAPH] interference graph: max_vregs=12, total_edges=15, livein_interference=3
[00:00:01.249] [JIT-REG] graph coloring...
[00:00:01.249] [JIT-REG-COLOR] graph coloring: max_vregs=12, num_regs=4 (SLJIT_S2-S5)
[00:00:01.250] [JIT-REG] spill handling...
[00:00:01.250] [JIT-REG-SPILL] spill slots: max_vregs=12, tvalue_size=16
[00:00:01.251] [JIT-REG] applying mappings...
[00:00:01.251] [JIT-REG] register mapping (maxstack=12):
[00:00:01.251] [JIT-REG]   R0 -> spilled=0, phys_reg=12 (SLJIT_S2=12)
[00:00:01.251] [JIT-REG]   R1 -> spilled=1, phys_reg=0 (SLJIT_S-1=0)
```

#### 代码生成

```
[00:00:01.253] [JIT-CG] codegen start, ir_head=0x...
[00:00:01.253] [JIT-CG] emit_enter...
[00:00:01.254] [JIT-CG] entry label created for self-recursion, proto=0x...
[00:00:01.254] [JIT-CG] processing IR nodes...
[00:00:01.254] [JIT-CG] node 1: op=5, pc=0
[00:00:01.254] [JIT-CG-ARITH] emit_add: dest=12, src1=10, src2=13
[00:00:01.255] [JIT-CG] node 2: op=14, pc=18
[00:00:01.260] [JIT-CG] codegen done, code=0x...
```

---

## 10. 编译管线总结

### 10.1 完整数据流

```
Proto->code (字节码数组)
    │
    │  luaJIT_compile()
    ▼
ljit_context_create()          创建编译上下文
    │
    ▼
ljit_analyze()                 分析阶段
    │                           ├─ 数据流分析 (def_pc, is_live)
    │                           ├─ 类型推断 (reg_types)
    │                           └─ CFG 构建 (ljit_ir_bb_build)
    ▼
ljit_translate()               翻译阶段
    │                           ├─ 逐 BB 遍历字节码
    │                           ├─ 操作码 → IR 指令映射
    │                           ├─ 自递归检测 (self_rec)
    │                           └─ 生成 IR 双向链表
    ▼
ljit_optimize()                优化阶段
    │                           ├─ ljit_opt_const()  常量折叠
    │                           ├─ ljit_opt_cse()    CSE
    │                           ├─ ljit_opt_peep()   窥孔优化
    │                           ├─ ljit_opt_dce()    死代码消除
    │                           └─ ljit_opt_inline() 函数内联
    ▼
ljit_regalloc()                寄存器分配阶段
    │                           ├─ ljit_reg_live()   活性分析
    │                           ├─ ljit_reg_graph()  干涉图构建
    │                           ├─ ljit_reg_color()  图着色
    │                           ├─ ljit_reg_spill()  溢出处理
    │                           └─ ljit_reg_alloc_process() 应用映射
    ▼
ljit_codegen()                 代码生成阶段
    │                           ├─ sljit_create_compiler()
    │                           ├─ sljit_emit_enter()
    │                           ├─ live-in 参数加载
    │                           ├─ 遍历 IR → 分发到子模块
    │                           └─ sljit_generate_code()
    ▼
void *code (原生机器码)
    │
    ▼
p->jit_trace = code            存入 Proto，后续调用直接执行
```

### 10.2 关键设计决策

| 决策 | 说明 |
|------|------|
| **热点阈值 56** | 与 LuaJIT 一致，函数被调用 56 次后触发 JIT 编译 |
| **4 个通用物理寄存器** | SLJIT_S2-S5 用于图着色分配，平衡寄存器压力和代码质量 |
| **溢出槽 = Lua 栈** | `stack_ofs = reg * sizeof(TValue)`，溢出直接映射到 Lua 栈，无需额外内存 |
| **自递归优化** | 检测递归调用自身，跳过 C 函数调用开销，直接跳转到函数入口标签 |
| **icall 辅助函数** | 复杂操作（表、闭包、OOP）通过 SLJIT 的 icall 机制调用 C 辅助函数 |
| **内联返回路径** | `nresults >= 0` 时直接内联 `luaD_poscall()` 调用，避免 C 包装开销 |
| **保守的死代码消除** | 遇到控制流/循环/CALL 时假设寄存器被使用，安全优先 |
| **编译失败标记** | `p->jit_failed = 1` 防止重复尝试编译同一函数 |