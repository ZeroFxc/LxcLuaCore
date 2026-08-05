# 原版 Parser vs AST Parser —— 面向对象(OOP)语法支持对比报告

## 1. 总体概况

| 维度 | 原版 Parser (`lparser.c`) | AST Parser (`last_parse.c`) |
|------|---------------------------|------------------------------|
| **架构目标** | 直接生成字节码（`expdesc` / `FuncState`） | 生成 AST 节点（`AstStmt` / `AstExpr`） |
| **软关键字机制** | 完整的上下文敏感软关键字系统（`softkw_check`），含上下文位掩码、前瞻匹配、排除列表、哈希表加速 | 简单字符串匹配（`lp_softkw_is` + `strcmp`），无上下文分层 |
| **节点存储** | 即时编译到寄存器/跳转列表 | 树形 AST 结构（`AstClassMember` 数组等） |

---

## 2. 详细特性对比表

### 2.1 Class 定义

| 特性 | 原版 Parser | AST Parser | 差异说明 |
|------|-------------|------------|----------|
| **基本语法** | `class Name ... end` | `class Name ... end` | ✅ 一致 |
| **类修饰符** | `abstract`, `final`, `sealed`, `singleton` | `abstract`, `final`, `sealed`, `singleton` | ✅ 一致（均通过 `class_flags` 传递） |
| **泛型参数** | `<T>` 支持（`has_typeparams`） | `<T>` 支持（`ngeneric_params`） | ✅ 一致 |
| **继承** | `extends A, B`（多继承） | `extends A, B` + **额外支持 `:` 语法** | ⚠️ AST 多一种 `class A: B` 写法 |
| **接口实现** | `implements I1, I2` | `implements I1, I2` | ✅ 一致 |
| **Trait 混入** | `use T1, T2` | `use T1, T2` | ✅ 一致 |
| **类体分隔符** | `{` 或 `do` | `{`, `do`, `begin`, **或隐式** | ⚠️ AST 更宽松，支持无显式分隔符 |
| **访问修饰符** | `private/protected/public/static` | `private/protected/public/static` | ✅ 一致 |
| **Getter/Setter** | `get name` / `set name` | `get name` / `set name` | ✅ 一致 |
| **抽象方法** | `abstract function name()` | `abstract function name()` | ✅ 一致 |
| **Final 方法** | `final function name()` | `final function name()` | ✅ 一致 |
| **Override** | `override function name()` | `override function name()` | ✅ 一致 |
| **静态成员** | `static function/property` | `static function/property` | ✅ 一致 |
| **属性(Property)** | 支持（内联到类表） | **显式 `AstClassMember` + `AST_MEMBER_PROPERTY`** | ⚠️ AST 有独立属性节点类型 |
| **嵌套类** | 支持（递归调用 `classstat`） | 支持（递归调用 `parse_class_stat`） | ✅ 一致 |
| **装饰器** | 支持 + **`apply_decorators_inline` 即时应用** | 支持（记录到节点） | ⚠️ 原版在解析期应用装饰器，AST 仅记录 |
| **静态构造函数** | **`OP_STATICINIT` 专用指令** | 未显式处理 | ❌ AST 可能缺失静态构造支持 |

### 2.2 Trait 定义

| 特性 | 原版 Parser | AST Parser | 差异说明 |
|------|-------------|------------|----------|
| **方法定义** | `function name() ... end` | `function name() ... end` | ✅ 一致 |
| **Require 签名** | `require function name(params)` | `require function name(sig)` | ✅ 一致 |
| **体分隔符** | `{` / `do` | `{`, `do`, `begin`, **隐式** | ⚠️ AST 更宽松 |
| **self 注入** | 不自动注入 self（需显式声明） | 不自动注入 self（`need_self=0`） | ✅ 一致 |
| **存储结构** | 直接生成到 Proto | `AstClassMember` + `AstMethodSig` | 架构差异 |

### 2.3 Interface 定义

| 特性 | 原版 Parser | AST Parser | 差异说明 |
|------|-------------|------------|----------|
| **接口继承** | `extends I1, I2`（多继承） | `extends I1, I2` | ✅ 一致 |
| **方法签名** | `function name(params)` | `function name(sig)` | ✅ 一致 |
| **返回类型注解** | 支持 | 支持 | ✅ 一致 |
| **体分隔符** | `{` / `do` | `{`, `do`, `begin`, **隐式** | ⚠️ AST 更宽松 |
| **空语句** | `;` 允许 | `;` 允许 | ✅ 一致 |

### 2.4 new 表达式

| 特性 | 原版 Parser | AST Parser | 差异说明 |
|------|-------------|------------|----------|
| **语法** | `new ClassName(args)` | `new ClassName(args)` | ✅ 一致 |
| **解析位置** | 独立 `newexpr` 函数 | `parse_primary` 内联处理 | 架构差异 |
| **类名解析** | 简单名字 | **支持 `suffixedexpr`（如 `new mod.Class()`）** | ⚠️ AST 支持更复杂的类名表达式 |

### 2.5 super 表达式

| 特性 | 原版 Parser | AST Parser | 差异说明 |
|------|-------------|------------|----------|
| **构造函数调用** | `super(args)` | `super(args)` | ✅ 一致 |
| **方法调用** | `super:method(args)` | `super:method(args)` | ✅ 一致 |
| **字段访问** | `super.field` | `super.field` | ✅ 一致 |
| **解析方式** | 独立 `superexpr` 函数，内部分支处理三种形式 | `ast_new_expr_super` + `parse_suffixedexpr` 后缀解析 | ⚠️ 原版集中处理，AST 分散到通用后缀机制 |

---

## 3. 关键架构差异

### 3.1 软关键字系统

```c
/* 原版：上下文敏感的完整软关键字系统 */
SoftKWID softkw_check(LexState *ls, unsigned int context);
// 支持：上下文位掩码、前瞻 token 匹配、排除列表、哈希表加速

/* AST：简单的字符串匹配 */
int lp_softkw_is(ParserState *ps, const char *name);
// 仅做 strcmp，无上下文感知
```

**影响**：AST Parser 的软关键字识别能力较弱，可能在复杂上下文（如 `new` 作为变量名）中表现不同。

### 3.2 装饰器处理

```c
/* 原版：解析期即时应用 */
static void apply_decorators_inline(LexState *ls, expdesc *v, expdesc *e) {
    // 直接生成 OP_MOVE + OP_CALL 字节码
}

/* AST：仅记录到 AST 节点 */
// decorators[] 数组存入 AstStmt，由后续阶段处理
```

### 3.3 静态构造函数

原版 Parser 在 `classstat` 中检测 `static` + `function` 组合时，会特殊处理生成 `OP_STATICINIT` 指令。AST Parser 的 `parse_class_stat` 中没有发现对应的特殊处理逻辑，这可能是一个**功能缺失**。

### 3.4 属性(Property)节点

AST Parser 显式定义了 `AST_MEMBER_PROPERTY` 成员类型，将 `name = value` 形式的类属性作为一等 AST 节点存储。原版 Parser 则将其作为普通表字段内联处理。

---

## 4. 互补建议

| 方向 | 建议 |
|------|------|
| **AST → 原版** | 将 `:` 继承语法、`begin` 体分隔符、隐式体结束的支持回 port 到原版 Parser，提升语法一致性 |
| **原版 → AST** | 补充 `OP_STATICINIT` 静态构造支持；增强软关键字系统以支持更复杂的上下文识别；实现装饰器的即时应用或 codegen 阶段支持 |
| **共同改进** | 统一 Trait/Interface 的体分隔符语义；明确 `new` 表达式中类名是否允许复杂表达式（如 `new (getClass())()`） |

---

## 5. 结论

- **语法覆盖度**：两者对核心 OOP 语法（class/trait/interface/new/super/修饰符）的支持高度一致，用户层面代码基本可互编译。
- **AST Parser 优势**：更灵活的语法（`:` 继承、隐式体结束）、显式的 AST 属性节点、支持复杂类名表达式。
- **原版 Parser 优势**：完整的软关键字上下文系统、装饰器即时应用、静态构造函数 (`OP_STATICINIT`)、更严格的语法检查（减少歧义）。
- **风险点**：AST Parser 的简化软关键字机制可能在边缘场景（如 `new`/`super` 作为普通标识符）引入解析歧义；静态构造支持的缺失可能导致部分类初始化代码无法正确编译。
