# LXCLUA-NCore AST 抽象语法树系统

## 1. AST 系统概述

### 1.1 AST 在编译管线中的位置

LXCLUA-NCore 的编译管线包含两条路径：传统解析器（lparser）和新的 AST 解析器（last_parse）。AST 系统位于语法分析阶段，将源码解析为结构化的 AST 树，后续可进行遍历、变换、序列化，最终由代码生成器（lcodegen）生成 Lua 字节码。

```
源码 (.lua)
    │
    ├─── 传统路径 ─────────────────────────────┐
    │                                         │
    ▼                                         ▼
┌──────────┐    词法分析                ┌──────────┐
│  llex    │ ──→ Token 流              │  llex    │
└──────────┘                            └────┬─────┘
    │                                        │
    ▼                                        ▼
┌──────────┐    语法分析 (直接→字节码)  ┌──────────────┐
│ lparser  │ ──→ Proto (FuncState)     │ last_parse   │
└──────────┘                            │  AST 解析器  │
                                        └──────┬───────┘
                                               │
                                               ▼
                                        ┌──────────────┐
                                        │  AstChunk    │
                                        │  (AST 树)    │
                                        └──────┬───────┘
                                               │
                                    ┌──────────┼──────────┐
                                    │          │          │
                                    ▼          ▼          ▼
                              ┌──────────┐ ┌────────┐ ┌──────────┐
                              │last_visit│ │last_ser│ │ lcodegen │
                              │ 访问者   │ │ 序列化 │ │ 代码生成 │
                              └──────────┘ └────────┘ └────┬─────┘
                                                           │
                                    ┌──────────────────────┘
                                    ▼
                              字节码 (.luac)
```

### 1.2 AST 与旧解析器（lparser）的区别

| 特性 | lparser（传统） | last_parse（AST） |
|------|----------------|-------------------|
| **输出形式** | 直接生成 Proto（字节码） | 生成 AST 树（AstChunk） |
| **中间表示** | FuncState 中间代码 | 结构化 AST 节点树 |
| **可编程性** | 无，解析即编译 | 可遍历、变换、序列化 AST |
| **可扩展性** | 语法扩展需修改解析器核心 | 可通过访问者/变换器扩展 |
| **语法支持** | 完整 LXCLUA 语法 | 完整 LXCLUA 语法（独立重新实现） |
| **内存管理** | 随解析器生命周期 | 独立 AstPool 内存池 |
| **使用场景** | 标准编译流程 | 代码分析、变换、LSP、工具链 |

两者共存，通过编译选项或运行时选择。`lparser.c` 直接生成字节码，`last_parse.c` 生成 AST 后再由 `lcodegen.c` 生成字节码。

### 1.3 文件结构图

```
src/compiler/
├── last.h/c              # AST 核心数据结构定义 + 内存池实现
├── last_parse.h/c        # AST 解析器（递归下降，Token流 → AST树）
├── last_serialize.h/c    # AST 序列化/反序列化（AST ↔ Lua table）
├── last_visitor.h/c      # AST 访问者模式（前序遍历框架）
├── lcodegen.h/c          # AST → Proto 代码生成器
│
src/stdlib/
└── lastlib.c             # Lua 层 AST API（require("ast")）
```

---

## 2. AST 数据结构（last.h/c）

### 2.1 节点类型标签

所有 AST 节点继承自 `AstNode` 基类，通过 `AstNodeKind` 区分四大类：

```c
typedef enum {
  AST_NODE = 0,   // 基类
  AST_EXPR,       // 表达式
  AST_STMT,       // 语句
  AST_FUNC,       // 函数
  AST_CHUNK       // 编译单元
} AstNodeKind;
```

### 2.2 AstNode 基类

```c
struct AstNode {
  AstNodeKind type;  // 节点类型标签
  int line;          // 源代码行号
  AstNode *next;     // 内存池链表指针
};
```

所有具体节点（`AstExpr`、`AstStmt`、`AstFunc`、`AstChunk`）都以 `AstNode` 作为首个成员，实现类似 C 继承的效果。

### 2.3 表达式节点（AstExpr）

#### 表达式类型枚举（AstExprKind）

```c
typedef enum {
  // 字面量
  AST_EXPR_NIL, AST_EXPR_TRUE, AST_EXPR_FALSE,
  AST_EXPR_INT, AST_EXPR_FLT, AST_EXPR_STRING,
  AST_EXPR_INTERPSTRING, AST_EXPR_REGEX,

  // 变量
  AST_EXPR_VARARG, AST_EXPR_IDENT, AST_EXPR_SUPER,

  // 运算
  AST_EXPR_BINOP, AST_EXPR_UNOP,

  // 调用/索引
  AST_EXPR_CALL, AST_EXPR_METHOD_CALL, AST_EXPR_INDEX,
  AST_EXPR_METHOD_REF, AST_EXPR_NEW,

  // 构造器
  AST_EXPR_TABLE_CTOR, AST_EXPR_MAP_CTOR,
  AST_EXPR_DICT_COMP, AST_EXPR_LIST_COMP, AST_EXPR_OBJECT,

  // 函数
  AST_EXPR_FUNC_EXPR, AST_EXPR_ARROW_FUNC,

  // 控制流表达式
  AST_EXPR_CONDEXPR, AST_EXPR_SWITCH_EXPR, AST_EXPR_SELECT_CASE,
  AST_EXPR_MATCH, AST_EXPR_PAREN,

  // 扩展语法
  AST_EXPR_AWAIT, AST_EXPR_PIPE, AST_EXPR_REVPIPE, AST_EXPR_SAFEPIPE,
  AST_EXPR_NULLCOAL, AST_EXPR_SPACESHIP, AST_EXPR_IS, AST_EXPR_IN,
  AST_EXPR_MERGE, AST_EXPR_OPTCHAIN, AST_EXPR_RANGE,
  AST_EXPR_TEST_TYPE, AST_EXPR_EMBED, AST_EXPR_SLICE,
  AST_EXPR_SPREAD, AST_EXPR_WALRUS, AST_EXPR_ASTPARSER
} AstExprKind;
```

#### AstExpr 结构体

```c
struct AstExpr {
  AstNode node;                // 基类
  AstExprKind kind;            // 表达式子类型
  int paren;                   // 括号层级
  unsigned int nodiscard:1;    // <nodiscard> 标记
  unsigned int is_pipe_self:1; // 管道操作中的 self 标记
  union {
    lua_Integer ival;          // AST_EXPR_INT
    lua_Number nval;           // AST_EXPR_FLT
    TString *strval;           // AST_EXPR_STRING/IDENT/REGEX/INTERPSTRING
    struct { AstBinOp op; AstExpr *lhs; AstExpr *rhs; } binop;  // 二元运算
    struct { AstUnOp op; AstExpr *operand; } unop;              // 一元运算
    struct { AstExpr *callee; AstExpr **args; int nargs; } call; // 函数调用
    struct { AstExpr *recv; TString *method; AstExpr **args; int nargs; } mcall; // 方法调用
    struct { AstExpr *table; AstExpr *key; int keystr; int is_opt; } index; // 索引
    struct { AstTableEntry *entries; int nentries; int narr; int nrec; } table; // 表构造器
    struct { AstMapEntry *entries; int nentries; } map;          // map构造器
    struct { AstFunc *func; } func;                              // 函数表达式
    struct { AstExpr *e1; AstExpr *e2; AstExpr *e3; } condexpr;  // 三元条件
    struct { AstExpr *start; AstExpr *end; } range;              // 范围
    struct { AstExpr *cond; AstCaseArm *arms; int narms; AstExpr *def; } switchx; // switch表达式
    struct { AstExpr *recv; AstExpr *placeholder; } pipe;        // 管道
    struct { AstExpr *obj; TString *method; } super;             // super调用
    struct { AstExpr *recv; TString *method; } method_ref;       // 方法引用
    struct { AstExpr *class_expr; AstExpr **args; int nargs; } newexpr; // new
    struct { struct AstStmt *stmt; } match;                      // match表达式
    struct { AstExpr *operand; TString *type_name; } test_type;  // 类型测试
    struct { int var_kind; int idx; } var;                       // 预解析变量
    struct { AstExpr *expr; } paren;                             // 括号
    struct { TString *filename; } embed;                         // $embed
    struct { AstExpr *ctor; } object;                            // $object
    struct { AstExpr *table; AstExpr *start; AstExpr *end; AstExpr *step; } slice; // 切片
    struct { AstExpr *expr; } spread;                            // 展开运算符
    struct { TString *name; AstExpr *expr; } walrus;             // 海象操作符
    struct { struct Proto *proto; struct AstChunk *chunk; } astparser; // astparser块
  } u;
};
```

#### 二元运算符枚举（AstBinOp）

```c
typedef enum {
  // 算术: + - * / // % ^
  AST_BIN_ADD, AST_BIN_SUB, AST_BIN_MUL, AST_BIN_DIV,
  AST_BIN_IDIV, AST_BIN_MOD, AST_BIN_POW,
  // 位运算: & | ~ << >>
  AST_BIN_BAND, AST_BIN_BOR, AST_BIN_BXOR, AST_BIN_SHL, AST_BIN_SHR,
  // 字符串: ..
  AST_BIN_CONCAT,
  // 管道: |> <| ?|>
  AST_BIN_PIPE, AST_BIN_REVPIPE, AST_BIN_SAFEPIPE,
  // 比较: == ~= < <= > >= <=>
  AST_BIN_EQ, AST_BIN_NE, AST_BIN_LT, AST_BIN_LE, AST_BIN_GT, AST_BIN_GE,
  AST_BIN_SPACESHIP,
  // 类型/成员: is in
  AST_BIN_IS, AST_BIN_IN,
  // 逻辑: and or
  AST_BIN_AND, AST_BIN_OR,
  // 空值合并: ??
  AST_BIN_NULLCOAL,
  // 其他: case infix merge
  AST_BIN_CASE, AST_BIN_INFIX, AST_BIN_MERGE
} AstBinOp;
```

#### 一元运算符枚举（AstUnOp）

```c
typedef enum {
  AST_UN_MINUS,     // -x
  AST_UN_BNOT,      // ~x
  AST_UN_NOT,       // not x
  AST_UN_LEN,       // #x
  AST_UN_AWAIT,     // await x
  AST_UN_TEST_Z,    // [-z expr] 字符串长度为零测试
  AST_UN_TEST_N,    // [-n expr] 字符串长度非零测试
  AST_UN_TEST_NIL,  // [-nil expr] nil 类型测试
  AST_UN_TEST_BOOL, // [-bool expr] boolean 类型测试
  AST_UN_TEST_FUNC  // [-func expr] function 类型测试
} AstUnOp;
```

### 2.4 语句节点（AstStmt）

#### 语句类型枚举（AstStmtKind）

```c
typedef enum {
  // 基本语句
  AST_STMT_BLOCK, AST_STMT_LOCAL, AST_STMT_ASSIGN, AST_STMT_EXPR,
  AST_STMT_EMPTY,

  // 控制流
  AST_STMT_IF, AST_STMT_WHILE, AST_STMT_REPEAT,
  AST_STMT_FOR_NUM, AST_STMT_FOR_GEN, AST_STMT_DO,
  AST_STMT_RETURN, AST_STMT_BREAK, AST_STMT_CONTINUE,
  AST_STMT_GOTO, AST_STMT_LABEL,

  // 多路分支
  AST_STMT_SWITCH, AST_STMT_MATCH,

  // 函数
  AST_STMT_LOCAL_FUNC,

  // 错误处理
  AST_STMT_TRY, AST_STMT_CATCH, AST_STMT_FINALLY,
  AST_STMT_THROW, AST_STMT_DEFER, AST_STMT_GUARD,

  // 全局/命名空间
  AST_STMT_GLOBAL, AST_STMT_NAMESPACE, AST_STMT_USING,

  // 类型定义
  AST_STMT_STRUCT, AST_STMT_SUPERSTRUCT, AST_STMT_ENUM,
  AST_STMT_CLASS, AST_STMT_TRAIT, AST_STMT_INTERFACE,
  AST_STMT_CONCEPT,

  // 操作符/关键字重载
  AST_STMT_COMMAND, AST_STMT_KEYWORD, AST_STMT_OPERATOR,

  // 其他
  AST_STMT_WITH, AST_STMT_ASM, AST_STMT_EXPORT,
  AST_STMT_WHILE_LET, AST_STMT_COMPOUND_ASSIGN, AST_STMT_INCR_DECR,
  AST_STMT_TAKE, AST_STMT_CONSTEXPR
} AstStmtKind;
```

#### AstStmt 结构体（核心字段）

```c
struct AstStmt {
  AstNode node;               // 基类
  AstStmtKind kind;           // 语句类型
  AstExpr **decorators;       // 装饰器数组
  int ndecorators;            // 装饰器数量
  union {
    struct { AstBlock block; } block;                        // 语句块
    struct { int nnames; TString **names; int *attrs;
             int nvalues; AstExpr **values;
             TypeHint **type_hints; } local;                 // 局部变量声明
    struct { int ntargets; AstAssignTarget *targets;
             int nvalues; AstExpr **values; } assign;        // 赋值
    struct { AstExpr *expr; } expr;                          // 表达式语句
    struct { AstIfArm *arms; int narms;
             int has_else; AstBlock else_body; } ifstmt;     // if语句
    struct { AstExpr *cond; AstBlock body;
             AstBlock else_body; int has_else; } whilestmt;  // while/repeat
    struct { int nnames; TString **names; AstExpr *expr;
             AstBlock body; AstBlock else_body;
             int has_else; } whilelet;                       // while let
    struct { TString *var; AstExpr *start; AstExpr *stop;
             AstExpr *step; AstBlock body;
             AstBlock else_body; int has_else; } fornum;     // 数值for
    struct { int nnames; TString **names; int nexprs;
             AstExpr **exprs; AstBlock body;
             AstBlock else_body; int has_else; } forgen;     // 泛型for
    struct { int nvalues; AstExpr **values; } retstmt;       // return
    struct { int level; } contbrk;                           // break/continue
    struct { TString *name; int label_id; int patch_pc; } label; // goto/label
    struct { AstExpr *cond; AstSwitchCase *cases;
             int ncases; AstBlock default_body;
             int has_default; } switchstmt;                  // switch
    struct { TString *name; AstFunc *func; } localfunc;      // 局部函数
    struct { int nnames; TString **names; int nvalues;
             AstExpr **values; int has_wildcard; } global;   // global
    struct { AstBinOp op; int ntargets;
             AstAssignTarget *targets; AstExpr *value; } compound; // 复合赋值
    struct { AstIncrKind kind; AstAssignTarget *target; } incr; // ++/--
    struct { AstExpr *cond; TString *let_var; AstExpr *let_value;
             AstBlock else_block; } guard;                   // guard
    struct { AstBlock body; AstExpr *catch_var;
             AstBlock catch_body; AstBlock finally_body; } trycatch; // try/catch/finally
    struct { AstExpr *expr; } throwstmt;                     // throw
    struct { AstBlock body; } deferstmt;                     // defer
    struct { int is_namespace; TString *name;
             TString *last_member; } usingstmt;              // using
    struct { TString *name; AstEnumEntry *entries;
             int nentries; int is_enum_class; } enumstmt;    // enum
    struct { TString *name; AstBlock body;
             AstKVPair *entries; int nentries; } nsstruct;   // namespace/struct/superstruct
    struct { TString *name; TString *extends_name;
             TString **implements; int nimplements;
             TString **use_traits; int nuse_traits;
             int class_flags; AstBlock body;
             AstClassMember *members; int nmembers; } classstmt; // class
    struct { int nvars; TString **varnames; AstExpr **defaults;
             AstExpr *source; int is_array; } take;           // take解构
    struct { TString *directive; AstExpr *cond;
             AstBlock body; } constexpr_stmt;                 // constexpr
    struct { AstExpr *control; AstMatchArm *arms;
             int narms; int is_expr; } matchstmt;             // match
    struct { AstExpr *target; AstBlock body; } withstmt;      // with
    struct { TString *raw_body; } asmstmt;                    // asm
  } u;
};
```

### 2.5 函数节点（AstFunc）

```c
struct AstFunc {
  AstNode node;                    // 基类
  int func_idx;                    // 唯一函数ID（主chunk为0）
  int parent_idx;                  // 父函数ID（主chunk为-1）
  int nparams;                     // 形参数量
  AstFuncParam *params;            // 形参数组
  int is_vararg;                   // 是否有可变参数 ...
  TString *vararg_name;            // 命名变参名称（...name语法）
  int is_async;                    // 是否async函数
  AstBlock body;                   // 函数体语句块
  int nlocals;                     // 局部变量总数
  AstUpvalueDesc *upvalues;        // upvalue描述符数组
  int nupvalues;                   // 当前upvalue数量
  int upval_cap;                   // upvalue数组容量
  int nups;                        // 实际upvalue数量（codegen阶段填充）
  int line_defined;                // 函数定义起始行号
  TString *source;                 // 源码文件名
  TypeHint *return_type_hint;      // 返回值类型注解
  TString **generic_params;        // 泛型类型参数名数组
  int ngeneric_params;             // 泛型参数数量
  TypeHint **generic_constraints;  // 泛型类型约束数组
  unsigned int nodiscard:1;        // <nodiscard> 标记
  AstFunc **child_funcs;           // 直接嵌套的子函数列表
  int nchild_funcs;                // 当前子函数数量
  int child_cap;                   // 子函数数组容量
};
```

### 2.6 编译单元（AstChunk）

```c
struct AstChunk {
  AstNode node;          // 基类
  AstFunc *main_func;    // 主chunk函数
  AstFunc **all_funcs;   // 所有函数的平铺列表（含main_func）
  int nfuncs;            // 函数总数
  int funcs_cap;         // 函数数组容量
  TString *source;       // 源码文件名
  AstPool *pool;         // 构建此chunk的内存池
};
```

### 2.7 内存池管理（AstPool）

AST 系统使用分块 Bump Allocator 进行内存管理：

```
┌──────────────────────────────────────────────────────┐
│                    AstPool                           │
│  L ──→ lua_State*                                   │
│  chunks ──→ AstPoolChunk 链表                        │
└──────┬───────────────────────────────────────────────┘
       │
       ▼
┌──────────────┐    ┌──────────────┐    ┌──────────────┐
│ AstPoolChunk │───→│ AstPoolChunk │───→│ AstPoolChunk │
│ next  ───────┘    │ next  ───────┘    │ next → NULL  │
│ used: 2048   │    │ used: 8192   │    │ used: 4096   │
│ cap:  8192   │    │ cap:  8192   │    │ cap:  8192   │
│ data[...]    │    │ data[...]    │    │ data[...]    │
└──────────────┘    └──────────────┘    └──────────────┘
```

**设计特点：**
- 默认块大小 8192 字节（`AST_POOL_CHUNK_SIZE`），8 字节对齐
- 小对象（≤8192）从当前块切分，大对象单独分配一个块
- 块从不 realloc，保证已分配指针稳定性
- 分配时自动清零（memset）
- 释放时整体释放所有块，O(1) 批量释放

**核心 API：**

```c
void ast_pool_init(lua_State *L, AstPool *p);   // 初始化
void ast_pool_free(AstPool *p);                 // 释放所有内存
void *ast_pool_alloc(AstPool *p, size_t bytes);  // 分配内存
```

**创建节点的宏：**

```c
#define ast_new_node(p, type, kind, line) \
  (cast(type *, ast_node_new((p), sizeof(type), (kind), (line))))
```

---

## 3. AST 解析器（last_parse.h/c）

### 3.1 概述

`last_parse.c` 实现了一个递归下降解析器，从词法分析器（`llex`）的 Token 流构建 AST 树。入口函数为 `luaY_parse_ast()`。

### 3.2 解析器状态（ParserState）

```c
struct ParserState {
  lua_State *L;           // Lua 状态机
  LexState *ls;           // 词法分析器状态
  AstPool *pool;          // AST 内存池
  AstChunk *chunk;        // 当前构建的 chunk
  AstFunc *curfunc;       // 当前正在解析的函数
  int func_idx_counter;   // 下一个函数ID
  int nerr;               // 错误计数
  ParseScope *scope;      // 当前作用域链
  Table *defines;         // $define 编译期常量表
};
```

### 3.3 作用域管理（ParseScope）

```c
struct ParseScope {
  ParseScope *prev;       // 外层作用域链
  AstFunc *func;          // 此作用域所属函数
  int nlocals;            // 可见局部变量总数
  int firstlocal;         // 此作用域第一个局部变量在func中的索引
  int is_loop;            // 是否是循环作用域（break/continue目标）
  TString **local_names;  // 此作用域声明的局部变量名列表
  int nnames;             // 当前作用域内变量数
  int names_cap;          // local_names数组容量
};
```

### 3.4 入口函数

```c
AstChunk *luaY_parse_ast(lua_State *L, ZIO *z, struct Mbuffer *buff,
                         struct Dyndata *dyd, const char *name, int firstchar);
```

解析流程：
1. 初始化词法分析器 `LexState`
2. 创建 `ParserState`，初始化 AST 内存池
3. 创建 `AstChunk`（含 `main_func`）
4. 递归下降解析：`parse_block()` → `parse_stat()` → `parse_expr()`
5. 检查 `TK_EOS` 结束标记
6. 返回构建好的 `AstChunk`

### 3.5 支持的语法结构

解析器通过 `parse_stat()` 函数分发到各专门的解析函数：

| 解析函数 | 处理的语法结构 |
|----------|---------------|
| `parse_block()` | 语句块（`{...}` 或 `do...end`） |
| `parse_expr()` | 表达式（含运算符优先级解析 `parse_subexpr()`） |
| `parse_funcbody()` | 函数体（含箭头函数 `=>`、async） |
| `parse_class_stat()` | 类定义（`class Name extends Parent implements I1, I2 use T1, T2`） |
| `parse_enum_stat()` | 枚举定义（`enum Name { ... }`） |
| `parse_struct_stat()` | 结构体定义（`struct Name { ... }`） |
| `parse_namespace_stat()` | 命名空间（`namespace Name { ... }`） |
| `parse_trait_stat()` | Trait 定义 |
| `parse_interface_stat()` | 接口定义 |
| `parse_match_stat()` | 模式匹配（`match expr { pat1 => ..., pat2 => ... }`） |
| `parse_try_stat()` | try-catch-finally |
| `parse_defer_stat()` | defer 延迟执行 |
| `parse_guard_stat()` | guard 守卫语句 |
| `parse_using_stat()` | using 语句 |
| `parse_switch_expr()` | switch 表达式 |
| `parse_dict_comprehension()` | 字典推导式 |
| `parse_list_comprehension()` | 列表推导式 |
| `parse_while_let_stat()` | while let 语句 |
| `parse_constexpr_stat()` | constexpr 预处理语句 |
| `parse_asm_stat()` | 内联汇编 |
| `parse_concept_stat()` | concept 类型概念 |
| `parse_command_stat()` | command 自定义命令 |
| `parse_keyword_stat()` | keyword 关键字重载 |
| `parse_operator_stat()` | operator 操作符重载 |
| `parse_global_stat()` | global 全局声明 |

### 3.6 扩展语法处理

解析器支持大量 LXCLUA 扩展语法：

| 语法 | 实现方式 | AstExprKind |
|------|---------|-------------|
| 箭头函数 `(x) => expr` | `parse_funcbody()` 中 `is_arrow=1` | `AST_EXPR_ARROW_FUNC` |
| 管道操作符 `x |> f` | `parse_subexpr()` 优先级处理 | `AST_EXPR_PIPE` |
| 反向管道 `f <| x` | 同上 | `AST_EXPR_REVPIPE` |
| 安全管道 `x ?|> f` | 同上 | `AST_EXPR_SAFEPIPE` |
| 海象操作符 `(x := expr)` | `parse_simpleexpr()` 中检测 `:=` | `AST_EXPR_WALRUS` |
| 可选链 `obj?.field` | `parse_suffixedexpr()` 中检测 `?.` | `AST_EXPR_OPTCHAIN` |
| 空值合并 `a ?? b` | `parse_subexpr()` 优先级处理 | `AST_EXPR_NULLCOAL` |
| 宇宙飞船 `a <=> b` | `parse_subexpr()` 优先级处理 | `AST_EXPR_SPACESHIP` |
| 类型测试 `-type expr "typename"` | `parse_test_value()` 中检测 | `AST_EXPR_TEST_TYPE` |
| 切片 `t[start:end:step]` | `parse_suffixedexpr()` 中检测 `:` | `AST_EXPR_SLICE` |
| 展开 `...expr` | `parse_simpleexpr()` 中检测 | `AST_EXPR_SPREAD` |
| 嵌入 `$embed "file"` | `parse_simpleexpr()` 中检测 | `AST_EXPR_EMBED` |
| 对象 `$object {...}` | `parse_simpleexpr()` 中检测 | `AST_EXPR_OBJECT` |
| astparser 块 | `parse_primary()` 中检测 | `AST_EXPR_ASTPARSER` |

### 3.7 错误恢复机制

解析器使用 `lp_error()` 报告语法错误。当前实现遇到错误即终止（`luaX_syntaxerror`），不进行错误恢复。错误计数通过 `ps->nerr` 追踪。

---

## 4. AST 序列化（last_serialize.h/c）

### 4.1 概述

AST 序列化模块提供 AST 树与 Lua table 之间的双向转换。序列化格式为 Lua table 结构，便于：
- 在 Lua 层面操作和检查 AST
- 跨编译单元传递 AST
- 与外部工具/语言交换 AST 数据

### 4.2 序列化（AST → Lua table）

```c
void ast_serialize_to_lua(lua_State *L, AstChunk *chunk);
```

**序列化格式：**

每个 AST 节点序列化为一个 Lua table，包含 `kind` 和 `line` 字段，以及节点特定的子字段：

```lua
-- 示例：二元表达式 a + b
{
  kind = "binop",
  line = 10,
  op = "+",
  lhs = { kind = "ident", line = 10, name = "a" },
  rhs = { kind = "ident", line = 10, name = "b" }
}

-- 示例：if 语句
{
  kind = "if",
  line = 5,
  arms = {
    { cond = { kind = "ident", name = "x" }, body = { kind = "block", body = {...} } }
  },
  has_else = false
}

-- 示例：chunk 根节点
{
  kind = "chunk",
  line = 1,
  body = { kind = "block", body = { ... } }
}
```

**节点类型映射：**

| AstExprKind | 序列化 kind 字符串 |
|-------------|-------------------|
| `AST_EXPR_BINOP` | `"binop"` |
| `AST_EXPR_CALL` | `"call"` |
| `AST_EXPR_FUNC_EXPR` | `"function"` |
| `AST_EXPR_ARROW_FUNC` | `"arrowfunc"` |
| `AST_EXPR_TABLE_CTOR` | `"table"` |
| `AST_EXPR_MAP_CTOR` | `"map"` |
| `AST_EXPR_CONDEXPR` | `"condexpr"` |
| `AST_EXPR_PIPE` | `"pipe"` |
| `AST_EXPR_NULLCOAL` | `"nullcoal"` |
| `AST_EXPR_SLICE` | `"slice"` |
| `AST_EXPR_WALRUS` | `"walrus"` |
| ... | ... |

### 4.3 反序列化（Lua table → AST）

```c
AstChunk *ast_deserialize_from_lua(lua_State *L, int idx);
```

从 Lua table 重建 AST 树：
1. 创建新的 `AstPool` 和 `AstChunk`
2. 递归解析 table 结构，创建对应的 AST 节点
3. 通过 `kind` 字段分发到各节点反序列化函数

**运算符字符串反向映射：**
- `"+"` → `AST_BIN_ADD`，`"|>"` → `AST_BIN_PIPE`，`"<=>"` → `AST_BIN_SPACESHIP` 等

### 4.4 版本兼容性

当前序列化格式不包含版本号字段。如果 AST 节点类型发生变化，需要同时更新序列化和反序列化逻辑。序列化模块通过字符串表示节点类型（而非整数枚举），天然具有较好的向前兼容性。

### 4.5 在字节码 dump/load 中的应用

序列化模块主要用于：
- **Lua API**：`lastlib.c` 中使用序列化将 C AST 转为 Lua table 供脚本操作
- **调试/分析**：将 AST dump 为可读的 Lua table 格式进行检查
- **工具链**：LSP 服务器等工具可以使用序列化后的 AST 进行分析

---

## 5. AST 访问者模式（last_visitor.h/c）

### 5.1 概述

访问者模式提供了一种在不修改 AST 结构的情况下遍历和操作 AST 树的机制。采用**前序遍历**（pre-order），对每个节点先调用用户回调，再递归遍历子节点。

### 5.2 遍历控制

```c
typedef enum {
  AST_VISIT_CONTINUE = 0,  // 继续遍历子节点
  AST_VISIT_SKIP,          // 跳过子节点，继续下一个兄弟节点
  AST_VISIT_TERMINATE      // 立即终止整个遍历
} AstVisitResult;
```

### 5.3 回调函数类型

```c
// 表达式访问回调
typedef AstVisitResult (*AstExprVisitor)(AstVisitorContext ctx, AstExpr *expr);

// 语句访问回调
typedef AstVisitResult (*AstStmtVisitor)(AstVisitorContext ctx, AstStmt *stmt);

// 函数入口访问回调
typedef AstVisitResult (*AstFuncVisitor)(AstVisitorContext ctx, AstFunc *func);
```

### 5.4 AstVisitor 结构体

```c
struct AstVisitor {
  AstExprVisitor expr_visitors[AST_EXPR_NEW + 1];          // 按 AstExprKind 索引
  AstStmtVisitor stmt_visitors[AST_STMT_CONSTEXPR + 1];    // 按 AstStmtKind 索引
  AstFuncVisitor func_visitor;                              // 函数入口回调
};
```

### 5.5 核心 API

```c
// 初始化访问者（将所有回调置为 NULL）
void ast_visitor_init(AstVisitor *v);

// 注册回调
void ast_visitor_on_expr(AstVisitor *v, AstExprKind k, AstExprVisitor cb);
void ast_visitor_on_stmt(AstVisitor *v, AstStmtKind k, AstStmtVisitor cb);
void ast_visitor_on_func(AstVisitor *v, AstFuncVisitor cb);

// 遍历入口
AstVisitResult ast_walk_expr(AstVisitor *v, AstVisitorContext ctx, AstExpr *e);
AstVisitResult ast_walk_stmt(AstVisitor *v, AstVisitorContext ctx, AstStmt *s);
AstVisitResult ast_walk_block(AstVisitor *v, AstVisitorContext ctx, AstBlock *blk);
AstVisitResult ast_walk_func(AstVisitor *v, AstVisitorContext ctx, AstFunc *f);
AstVisitResult ast_walk_chunk(AstVisitor *v, AstVisitorContext ctx, AstChunk *chunk);
```

### 5.6 遍历流程

```
ast_walk_chunk(chunk)
  │
  └─→ ast_walk_func(chunk->main_func)
        │
        ├─→ [func_visitor 回调]  ← 函数入口
        │     ├─ CONTINUE → 继续
        │     ├─ SKIP → 跳过函数体
        │     └─ TERMINATE → 终止
        │
        ├─→ 遍历参数默认值表达式
        │
        ├─→ ast_walk_block(body)
        │     │
        │     └─→ ast_walk_stmt(stmt)  × N
        │           │
        │           ├─→ [stmt_visitors[stmt->kind] 回调]
        │           └─→ 递归遍历语句中的表达式
        │                 │
        │                 └─→ ast_walk_expr(expr)
        │                       │
        │                       ├─→ [expr_visitors[expr->kind] 回调]
        │                       └─→ 递归遍历子表达式
        │
        └─→ 遍历子函数
```

### 5.7 使用场景

| 场景 | 实现方式 |
|------|---------|
| **代码分析** | 遍历 AST 收集统计信息（变量使用、函数调用次数等） |
| **代码变换** | 在回调中修改节点内容（替换变量名、重写表达式等） |
| **代码优化** | 遍历 AST 进行常量折叠、死代码检测等 |
| **LSP 支持** | 遍历 AST 提取符号信息、诊断问题 |
| **代码格式化** | 遍历 AST 生成格式化后的代码 |
| **调试 dump** | `ast_dump_*` 系列函数使用访问者模式打印 AST |

### 5.8 使用示例

```c
// 示例：统计所有函数调用次数
typedef struct { int count; } CallCounter;

static AstVisitResult count_calls(AstVisitorContext ctx, AstExpr *e) {
  CallCounter *cc = (CallCounter *)ctx;
  cc->count++;
  return AST_VISIT_CONTINUE;
}

void count_all_calls(AstChunk *chunk) {
  AstVisitor v;
  CallCounter cc = {0};
  ast_visitor_init(&v);
  ast_visitor_on_expr(&v, AST_EXPR_CALL, count_calls);
  ast_visitor_on_expr(&v, AST_EXPR_METHOD_CALL, count_calls);
  ast_walk_chunk(&v, &cc, chunk);
  printf("Total calls: %d\n", cc.count);
}
```

---

## 6. AST 代码生成（lcodegen.h/c）

### 6.1 概述

`lcodegen.c` 将 AST 树转换为 Lua 字节码（Proto）。它复用了传统编译器（`lcode.c`）的中间代码生成基础设施（`FuncState`、`expdesc`、`OpCode` 等），但以 AST 节点作为输入，而非直接解析 Token。

### 6.2 代码生成器状态（CodegenState）

```c
typedef struct CodegenState {
  lua_State *L;
  AstPool *pool;
  FuncState *fs;           // 当前函数状态（复用 lparser 的 FuncState）
  BlockCnt *bl;            // 当前块链
  Dyndata *dyd;            // 动态数据（actvar/label/goto 列表）
  LexState ls;             // 最小词法状态（供 open_func/new_localvar 等使用）
  int nerr;                // 错误计数
  LoopJump loop_stack[MAX_LOOP_DEPTH]; // 循环层级栈
  int loop_depth;          // 当前循环嵌套深度
  struct { TString **names; int *pcs; int n; int size; } labels; // label 处理
  struct { TString **names; int *pcs; int n; int size; } gotos;  // goto 处理
} CodegenState;
```

### 6.3 入口函数

```c
// 从 AstFunc 生成 Proto
Proto *luaY_codegen_func(lua_State *L, AstFunc *func, AstPool *pool, Dyndata *dyd);

// 从 AstChunk 生成 Proto（chunk 是 main func）
Proto *luaY_codegen_chunk(lua_State *L, AstChunk *chunk, Dyndata *dyd);
```

**`luaY_codegen_func` 流程：**
1. 暂停 GC（防止 AST 池中的 TString 被回收）
2. 初始化 `CodegenState`
3. 调用内部 `codegen_func()` 递归生成 Proto
4. 恢复 dyd 状态和 GC

### 6.4 核心代码生成函数

| 函数 | 功能 |
|------|------|
| `codegen_expr()` | 将 `AstExpr` 转换为 `expdesc` 中间表示 |
| `codegen_stmt()` | 将 `AstStmt` 转换为字节码指令序列 |
| `codegen_block()` | 处理 `AstBlock` 中的语句列表 |
| `codegen_func()` | 处理函数定义，创建子 Proto |
| `codegen_match_pattern()` | 处理模式匹配的条件跳转 |
| `codegen_match_body()` | 处理模式匹配的分支体 |

### 6.5 运算符映射

代码生成器将 AST 运算符映射到 Lua 操作码：

```c
// 二元运算符映射
static const BinOpr binop_map[] = {
  [AST_BIN_ADD] = OPR_ADD,       // +
  [AST_BIN_SUB] = OPR_SUB,       // -
  [AST_BIN_MUL] = OPR_MUL,       // *
  [AST_BIN_DIV] = OPR_DIV,       // /
  [AST_BIN_POW] = OPR_POW,       // ^
  [AST_BIN_CONCAT] = OPR_CONCAT, // ..
  [AST_BIN_PIPE] = OPR_PIPE,     // |>
  [AST_BIN_EQ] = OPR_EQ,         // ==
  [AST_BIN_AND] = OPR_AND,       // and
  [AST_BIN_OR] = OPR_OR,         // or
  [AST_BIN_NULLCOAL] = OPR_NULLCOAL, // ??
  // ...
};

// 一元运算符映射
static const UnOpr unop_map[] = {
  [AST_UN_MINUS] = OPR_MINUS,
  [AST_UN_BNOT] = OPR_BNOT,
  [AST_UN_NOT] = OPR_NOT,
  [AST_UN_LEN] = OPR_LEN,
  [AST_UN_AWAIT] = OPR_AWAIT,
};
```

### 6.6 寄存器分配

代码生成器复用 `lparser.c` 的寄存器分配机制：
- 通过 `add_local()` 创建局部变量
- 通过 `activate_locals()` 激活局部变量（分配寄存器）
- 通过 `find_local()` / `find_upval()` 查找变量引用
- 通过 `register_localvar()` 注册调试信息

### 6.7 与旧 lcode.c 的区别

| 特性 | lcode.c（传统） | lcodegen.c（AST） |
|------|----------------|-------------------|
| **输入** | 直接由 lparser 调用 | 接收 AST 节点 |
| **函数管理** | 通过 lparser 的 FuncState 栈 | 通过 AstChunk 的 all_funcs 列表 |
| **变量解析** | 解析时实时查找 | 使用 AST 中预解析的 var_kind/idx |
| **新增操作码** | 无 | OPR_PIPE, OPR_NULLCOAL, OPR_SPACESHIP, OPR_IS, OPR_IN, OPR_MERGE 等 |
| **循环处理** | 通过 BlockCnt 链 | 额外的 loop_stack 管理 break/continue |

---

## 7. Lua API（lastlib.c）

### 7.1 概述

`lastlib.c` 提供 `require("ast")` 模块，在 Lua 层操作序列化后的 AST（Lua table 格式）。当前接口操作的是序列化后的 AST，而非 C 层 AST。

### 7.2 模块函数列表

```lua
local ast = require("ast")
```

| 函数 | 签名 | 功能 |
|------|------|------|
| `ast.stmt` | `(kind, line, fields?) → table` | 创建语句节点 |
| `ast.expr` | `(kind, line, fields?) → table` | 创建表达式节点 |
| `ast.find` | `(node, kind) → table[]` | 递归查找匹配 kind 的子节点 |
| `ast.walk` | `(node, callback)` | 深度优先遍历 AST，callback(node, depth) |
| `ast.set` | `(node, key, value)` | 修改节点字段 |
| `ast.get` | `(node, key) → value` | 读取节点字段 |
| `ast.insert` | `(block, index, stmt)` | 插入语句到 block.body |
| `ast.remove` | `(block, index) → stmt` | 从 block.body 删除语句 |
| `ast.replace` | `(block, index, new_stmt)` | 替换 block.body 中的语句 |
| `ast.copy` | `(node) → node` | 深拷贝 AST 节点 |

### 7.3 使用示例

```lua
local ast = require("ast")

-- 创建节点
local ident = ast.expr("ident", 10, { name = "x" })
local num = ast.expr("int", 10, { value = 42 })
local assign = ast.stmt("assign", 10, {
  targets = { { var = "x" } },
  values = { { kind = "int", value = 42 } }
})

-- 创建 block 并插入语句
local block = ast.stmt("block", 1, { body = {} })
ast.insert(block, 1, assign)

-- 遍历 AST
ast.walk(block, function(node, depth)
  print(string.rep("  ", depth) .. (node.kind or "?"))
end)

-- 查找节点
local ids = ast.find(block, "ident")
for _, id in ipairs(ids) do
  print("Found ident:", id.name)
end

-- 修改节点
ast.set(ident, "name", "y")

-- 深拷贝
local copy = ast.copy(block)
```

### 7.4 子字段遍历

`ast.find` 和 `ast.walk` 通过预定义的子字段列表遍历 AST 树：

```c
static const char *child_fields[] = {
  "body", "arms", "cases", "else_body", "catch_body", "finally_body",
  "default_body", "expr", "cond", "values", "targets", "params",
  "callee", "args", "key", "value", "lhs", "rhs", "operand",
  "table", "recv", "entries", "start", "end", "step",
  "var", "vars", "exprs", NULL
};
```

---

## 8. 与旧解析器的关系

### 8.1 两条编译路径

```
                    ┌─── lparser.c ───→ Proto (字节码)
源码 ──→ llex ──→  │
                    └─── last_parse.c ───→ AstChunk ───→ lcodegen.c ───→ Proto
```

### 8.2 设计对比

| 维度 | lparser.c | last_parse.c + lcodegen.c |
|------|-----------|--------------------------|
| **解析策略** | 递归下降，直接调用 lcode 生成指令 | 递归下降，构建 AST 树 |
| **中间表示** | FuncState + expdesc（运行时即时） | AstChunk + AstExpr/AstStmt（结构化） |
| **代码生成** | 边解析边生成（lcode.c） | 解析完成后统一生成（lcodegen.c） |
| **语法支持** | 全部 LXCLUA 语法 | 全部 LXCLUA 语法（独立重新实现） |
| **内存管理** | 随解析器生命周期 | AstPool 独立内存池 |
| **可扩展性** | 语法扩展修改解析器+代码生成 | 语法扩展修改解析器+代码生成，但可通过 AST 变换插入中间处理 |

### 8.3 共存机制

两条路径通过编译选项或运行时选择。`lcodegen.c` 复用了 `lparser.c` 的以下基础设施：
- `FuncState` 和 `BlockCnt` 结构
- `Dyndata` 动态数据结构（actvar、label、goto 列表）
- `lcode.c` 的指令生成函数（`luaK_*`）
- `LexState` 最小状态（供 `open_func` 等使用）

### 8.4 完整编译流程（AST 路径）

```
luaY_parse_ast()
    │
    ├─→ 初始化 LexState（词法分析器）
    ├─→ 初始化 ParserState + AstPool
    ├─→ 创建 AstChunk（含 main_func）
    │
    ├─→ parse_block() → parse_stat() × N
    │     └─→ parse_expr() → parse_subexpr()
    │           └─→ primary → suffixed → subexpr（运算符优先级）
    │
    ├─→ 检查 TK_EOS
    └─→ 返回 AstChunk

luaY_codegen_chunk()
    │
    ├─→ 初始化 CodegenState
    ├─→ codegen_func(main_func)
    │     ├─→ 创建 FuncState + Proto
    │     ├─→ codegen_block(body)
    │     │     └─→ codegen_stmt(stmt) × N
    │     │           └─→ codegen_expr(expr) → expdesc → luaK_*
    │     └─→ 递归处理子函数
    └─→ 返回 Proto（可执行字节码）
```

---

## 附录：常用 AST 节点创建 API

### 表达式创建

| 函数 | 创建节点 |
|------|---------|
| `ast_new_expr_nil(p, line)` | nil 字面量 |
| `ast_new_expr_bool(p, is_true, line)` | true/false 字面量 |
| `ast_new_expr_int(p, v, line)` | 整数字面量 |
| `ast_new_expr_flt(p, v, line)` | 浮点数字面量 |
| `ast_new_expr_str(p, s, kind, line)` | 字符串/正则/插值字符串 |
| `ast_new_expr_vararg(p, line)` | ... 可变参数 |
| `ast_new_expr_ident(p, name, line)` | 标识符 |
| `ast_new_expr_binop(p, op, lhs, rhs, line)` | 二元运算 |
| `ast_new_expr_unop(p, op, operand, line)` | 一元运算 |
| `ast_new_expr_call(p, callee, args, nargs, line)` | 函数调用 |
| `ast_new_expr_methodcall(p, recv, method, args, nargs, line)` | 方法调用 |
| `ast_new_expr_index(p, table, key, is_opt, line)` | 表索引 |
| `ast_new_expr_table(p, entries, nentries, line)` | 表构造器 |
| `ast_new_expr_map(p, entries, nentries, line)` | map 构造器 |
| `ast_new_expr_func(p, func, is_arrow, line)` | 函数/箭头函数表达式 |
| `ast_new_expr_condexpr(p, cond, thn, els, line)` | 三元条件 |
| `ast_new_expr_paren(p, e, line)` | 括号表达式 |
| `ast_new_expr_range(p, start, end, line)` | 范围表达式 |
| `ast_new_expr_pipe(p, optype, e1, e2, line)` | 管道表达式 |
| `ast_new_expr_methodref(p, recv, method, line)` | 方法引用 |
| `ast_new_expr_test_type(p, operand, type_name, line)` | 类型测试 |
| `ast_new_expr_embed(p, filename, line)` | $embed 嵌入 |
| `ast_new_expr_object(p, table, line)` | $object 对象 |
| `ast_new_expr_slice(p, table, start, end, step, line)` | 切片 |
| `ast_new_expr_spread(p, expr, line)` | 展开运算符 |
| `ast_new_expr_new(p, class_expr, args, nargs, line)` | new 表达式 |
| `ast_new_expr_match(p, stmt, line)` | match 表达式 |
| `ast_new_expr_super(p, line)` | super 表达式 |
| `ast_new_expr_walrus(p, name, expr, line)` | 海象操作符 |
| `ast_new_expr_astparser(p, proto, chunk, line)` | astparser 块 |

### 语句创建

| 函数 | 创建节点 |
|------|---------|
| `ast_new_stmt_block(p, line)` | 空语句块 |
| `ast_new_stmt_local(p, nnames, names, nvalues, line)` | 局部变量声明 |
| `ast_new_stmt_assign(p, ntargets, nvalues, line)` | 赋值语句 |
| `ast_new_stmt_expr(p, e, line)` | 表达式语句 |
| `ast_new_stmt_if(p, line)` | if 语句 |
| `ast_new_stmt_while(p, cond, line)` | while 语句 |
| `ast_new_stmt_while_let(p, nnames, names, expr, line)` | while let 语句 |
| `ast_new_stmt_repeat(p, line)` | repeat 语句 |
| `ast_new_stmt_fornum(p, var, start, stop, step, line)` | 数值 for |
| `ast_new_stmt_forgen(p, nnames, nexprs, line)` | 泛型 for |
| `ast_new_stmt_return(p, nvalues, line)` | return 语句 |
| `ast_new_stmt_break(p, level, line)` | break 语句 |
| `ast_new_stmt_continue(p, level, line)` | continue 语句 |
| `ast_new_stmt_goto(p, name, line)` | goto 语句 |
| `ast_new_stmt_label(p, name, line)` | label 语句 |
| `ast_new_stmt_empty(p, line)` | 空语句 |
| `ast_new_stmt_compound(p, op, ntargets, value, line)` | 复合赋值 |
| `ast_new_stmt_incr(p, kind, line)` | 自增/自减 |
| `ast_new_stmt_guard(p, cond, let_var, let_value, else_block, line)` | guard 语句 |
| `ast_new_stmt_try(p, body, catch_var, catch_body, finally_body, line)` | try 语句 |
| `ast_new_stmt_defer(p, body, line)` | defer 语句 |
| `ast_new_stmt_namespace(p, name, body, line)` | namespace 语句 |
| `ast_new_stmt_typed(p, kind, name, body, line)` | 命名空间风格语句 |
| `ast_new_stmt_typed_pairs(p, kind, name, pairs, npairs, line)` | 带键值对的语句 |
| `ast_new_stmt_enum(p, name, entries, nentries, is_enum_class, line)` | enum 语句 |
| `ast_new_stmt_using(p, is_namespace, name, last_member, line)` | using 语句 |
| `ast_new_stmt_throw(p, expr, line)` | throw 语句 |
| `ast_new_stmt_localfunc(p, name, func, line)` | 局部函数声明 |
| `ast_new_stmt_global(p, nnames, nvalues, line)` | 全局变量声明 |
| `ast_new_stmt_take(p, nvars, varnames, defaults, source, is_array, line)` | take 解构 |
| `ast_new_stmt_constexpr(p, directive, cond, body, line)` | constexpr 语句 |
| `ast_new_stmt_match(p, control, arms, narms, is_expr, line)` | match 语句 |
| `ast_new_stmt_with(p, target, body, line)` | with 语句 |
| `ast_new_stmt_asm(p, raw_body, line)` | asm 内联汇编 |