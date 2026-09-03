# 编译器架构

> 基于 `llex.h`(300行) + `lparser.c`(14294行) + `lcodegen.c`(4813行) + `lasm.c` + `lopcodes.h`(554行) 源码分析

---

## 1. 编译流水线

```
源码 (.lua) → 词法分析 → 语法分析 → AST → 代码生成 → 汇编 → 混淆 → 字节码
```

| 阶段 | 文件 | 输入 | 输出 |
|------|------|------|------|
| 词法分析 | `llex.c` | 源码字符流 | Token 流 |
| 语法分析 | `lparser.c` / `last_parse.c` | Token 流 | Proto / AST |
| 代码生成 | `lcodegen.c` | AST | Proto (中间代码) |
| 汇编 | `lasm.c` | Proto | 最终 64 位字节码 |
| 混淆 | `lobfuscate.c` | Proto | 混淆后的 Proto |

---

## 2. 词法分析器 (llex.c)

### 2.1 Token 系统

基于 `llex.h:35-73`，所有 Token 定义在 `RESERVED` 枚举中：

```
单字符 Token (ASCII): + - * / % ^ & | ~ < > = ( ) { } [ ] ; : , . #

关键字 Token: TK_AND, TK_BREAK, TK_CLASS, TK_DO, TK_ELSE, TK_END, TK_FUNCTION, TK_IF, TK_LOCAL, TK_NIL, TK_NOT, TK_OR, TK_RETURN, TK_THEN, TK_UNTIL, TK_WHILE ...

扩展关键字: TK_TAKE, TK_SWITCH, TK_TRY, TK_CATCH, TK_FINALLY, TK_DEFER, TK_GUARD, TK_WITH, TK_LET, TK_ASM, TK_ASTPARSER, TK_COMMAND, TK_OPERATOR, TK_ENUM, TK_NAMESPACE, TK_USING, TK_CONCEPT, TK_STRUCT, TK_SUPERSTRUCT, TK_ASYNC, TK_AWAIT

操作符 Token: TK_PIPE(|>), TK_REVPIPE(<|), TK_SAFEPIPE(?|), TK_NULLCOAL(??), TK_OPTCHAIN(?.),
TK_SPACESHIP(<=>), TK_MERGE(<>), TK_WALRUS(:=), TK_MEAN(=>), TK_ARROW(->),
TK_DOLLAR($), TK_DOLLDOLL($$), TK_REGEX, TK_DBCOLON(::)

复合赋值: TK_ADDEQ(+=), TK_SUBEQ(-=), TK_MULEQ(*=), TK_DIVEQ(/=), TK_IDIVEQ(//=),
TK_MODEQ(%=), TK_POWEQ(^=), TK_BANDEQ(&=), TK_BOREQ(|=), TK_SHREQ(>>=), TK_SHLEQ(<<=),
TK_CONCATEQ(..=), TK_ANDANDEQ(&&=), TK_OROREQ(||=), TK_NULLCOALEQ(??=),
TK_PLUSPLUS(++)

字面量 Token: TK_INT, TK_FLT, TK_NAME, TK_STRING, TK_INTERPSTRING, TK_RAWSTRING
```

### 2.2 预处理器

LexState 包含预处理器支持 (`llex.h:168-210`)：

```c
typedef struct LexState {
    // ...
    Alias *aliases;           // 别名定义
    IncludeState *inc_stack;  // 包含栈
    Table *defines;           // 编译时常量
    Table *named_types;       // 命名类型
    Table *declared_globals;  // 声明的全局变量
    // ...
} LexState;
```

支持 `$define`, `$include`, `$if`/`$endif` 等预处理器指令。

### 2.3 警告系统

```c
typedef enum {
    WT_VAR_SHADOW, WT_GLOBAL_SHADOW, WT_TYPE_MISMATCH,
    WT_UNREACHABLE_CODE, WT_IMPLICIT_GLOBAL, WT_UNUSED_VAR,
    // ... 18 种警告类型
} WarningType;
```

---

## 3. 语法分析器 (lparser.c / last_parse.c)

### 3.1 两套解析器

- **lparser.c** (14294行): 传统解析器，直接生成 Proto
- **last_parse.c** (6789行): AST 解析器，生成 AST 树后由 `lcodegen.c` 转换为 Proto

### 3.2 支持的语法结构

基于 `llex.h` Token 枚举和 `lparser.c` 语法规则：

- 标准 Lua 5.5 全部语法
- OOP: `class`/`interface`/`extends`/`implements`/`trait` 定义
- 控制流扩展: `switch`/`case`/`guard`/`defer`/`try`/`catch`/`finally`/`when`/`with`
- 函数: 箭头函数 `=>`、lambda `|params| -> expr`、`async`/`await`
- 模块: `namespace`/`using`/`export`
- 类型: `bool`/`int`/`float`/`void` 等类型注解
- 表达式: 管道 `|>`、空值合并 `??`、可选链 `?.`、三路比较 `<=>`、表合并 `<>`、海象 `:=`
- 字面量: 正则 `/pattern/`、插值字符串 `$"..."`、原生字符串 `_raw"..."`

### 3.3 循环深度跟踪

```c
// LexState 中
int loop_depth;  // 支持 break N 多层级跳转
```

---

## 4. 代码生成器 (lcodegen.c 4813行)

将 AST 转换为 Proto（函数原型）：

- 指令选择：AST 节点 → VM 操作码
- 寄存器分配：线性扫描
- 常量管理：常量表构建和去重
- 跳转修复：控制流目标解析

---

## 5. 汇编器 (lasm.c)

将 Proto 转换为最终 64 位字节码：

1. 指令编码：操作码和操作数编码为 64 位
2. 常数表处理：优化和排序
3. 子函数递归处理
4. 最终输出：完整 Proto 二进制

---

## 6. 混淆引擎 (lobfuscate.c 4312行)

### 6.1 控制流扁平化

```
原始控制流:
  if (cond) → A else → B
  → C

扁平化后:
  state = cond ? 1 : 2
  while (state) {
    switch (state):
      case 1: A; state = 3; break
      case 2: B; state = 3; break
      case 3: C; state = 0; break
  }
```

### 6.2 混淆模式

```c
#define OBFUSCATE_CFF               (1<<0)   // 控制流扁平化
#define OBFUSCATE_BLOCK_SHUFFLE     (1<<1)   // 基本块洗牌
#define OBFUSCATE_BOGUS_BLOCKS      (1<<2)   // 虚假块
#define OBFUSCATE_STATE_ENCODE      (1<<3)   // 状态编码
#define OBFUSCATE_NESTED_DISPATCHER (1<<4)   // 多层 dispatcher
#define OBFUSCATE_OPAQUE_PREDICATES (1<<5)   // 不透明谓词
#define OBFUSCATE_FUNC_INTERLEAVE   (1<<6)   // 函数交错
#define OBFUSCATE_VM_PROTECT        (1<<7)   // VM 保护
#define OBFUSCATE_BINARY_DISPATCHER (1<<8)   // 二分 dispatcher
#define OBFUSCATE_RANDOM_NOP        (1<<9)   // 随机 NOP
#define OBFUSCATE_STR_ENCRYPT       (1<<11)  // 字符串加密
```

### 6.3 公开 API

```c
// lua.h: 4 种混淆标志
#define LUA_OBFUSCATE_NONE           0
#define LUA_OBFUSCATE_CFF            (1<<0)
#define LUA_OBFUSCATE_BLOCK_SHUFFLE  (1<<1)
#define LUA_OBFUSCATE_BOGUS_BLOCKS   (1<<2)
#define LUA_OBFUSCATE_STATE_ENCODE   (1<<3)

int lua_dump_obfuscated(L, writer, data, strip, flags, seed, log_path);
```

### 6.4 字节码保护

Proto 结构包含防篡改字段：
```c
uint64_t bytecode_hash;  // 原始字节码哈希
int difierline_mode;      // 混淆模式
uint64_t difierline_data; // 混淆数据
```

---

## 7. 构建选项

```bash
cmake .. -DBUILD_LUA=ON -DBUILD_LUA_LIB=OFF -DBUILD_TESTS=ON \
         -DENABLE_LTO=OFF -DENABLE_WASM=ON -DENABLE_CRYPTO=ON
```