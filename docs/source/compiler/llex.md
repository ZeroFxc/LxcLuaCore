# compiler/llex.c + llex.h — 词法分析器（全部字面量与运算符的词法规则）

> 职责：把字符流转成 token 流。包含：65 个保留字、全部扩展运算符、
> 数字/字符串/正则/原生串字面量规则、字符串插值扫描、注释与
> `@warnings` 指令、**文件 include 栈（支持加密）**、别名替换、
> 3 级 lookahead（带 `nospace` 空白检测）、带源码定位的错误消息。

---

## 一、特性介绍

1. **65 个保留字**（`luaX_tokens` 前段，`ORDER RESERVED`）：
   `and asm astparser async await bool break case catch char command concept
   const continue default defer delete do double else elseif end enum export
   false finally float for function global guard goto if in int is instanceof
   keyword lambda local long namespace nil not operator or repeat requires
   return struct superstruct switch take then true try until using void when
   while with let`。
   （`LUA_COMPAT_GLOBAL` 编译时 `global` 退回普通标识符。）
2. **扩展符号 token**：`// .. ... == >= <= ~ << >> |> <| |?> :: => := ->`、
   复合赋值 `+= -= *= /= //= %= &= |= ~= >>= <<= ..= ++`、
   `?. ?? ??= &&= ||= ^= <=> <> $ $$`、正则/数字/整数/名字/字符串/
   插值串/原生串语义 token。
3. **警告系统**（16 类，`luaX_warnNames`）：`all var-shadow global-shadow
   type-mismatch unreachable-code excessive-arguments bad-practice
   possible-typo non-portable-code non-portable-bytecode non-portable-name
   implicit-global unannotated-fallthrough discarded-return field-shadow unused`。
   默认关闭：`global-shadow / non-portable-* / implicit-global / all` 之外全开；
   注释指令 `---@warnings: disable-unused, error-type-mismatch, disable-next`
   可逐行/逐类调整（`WS_ON/WS_OFF/WS_ERROR`，`WS_ERROR` 升级为语法错误）。
4. **错误消息带源码定位**：源是文件（`@` 前缀）时，`lexerror` 读回出错行
   渲染 `行号 | 内容` + `^ here` 指示列；否则回退
   `tokenpos/Line/LastToken/description` 格式（实测可见两种）。
5. **include 栈（LXCLUA）**：`luaX_pushincludefile` 打开被包含文件
   （支持 `Nirithy==` 加密文件：解码 + AES-CTR 解密为内存串），
   保存/切换 `ZIO`、行号、源名；EOZ 时自动弹栈继续外层文件。
6. **别名替换**：`luaX_addalias` 把标识符名映射到 token 序列
   （`pending_tokens` 队列逐个吐出）——预处理器基础设施。
7. **`nospace` 标记**：每个 token 记录"与前一 token 之间无空白"
   （`t.nospace`），范围字面量 `1..5` 的判定依赖它（见 `lcode.md`）。
8. **VMP 钩点**：`luaX_next` 入口。

---

## 二、词法规则（语法解释）

### 2.1 数字字面量（`read_numeral`）

宽松模式：`%d(%x|%.|([Ee][+-]?))* | 0[Xx](%x|%.|([Pp][+-]?))*`，另加：

- `0b`/`0B` 二进制、`0o`/`0O` 八进制（此后禁用指数）；
- `_` 视觉分隔符（读时丢弃：`1_000_000` 合法）；
- `.` 后紧跟 `.` 时**停止**（把 `..` 留给拼接/范围运算符）；
- 数字后紧跟字母强制报错（`malformed number`）。
- 最终值由 `luaO_str2num` 判定整数/浮点（`TK_INT`/`TK_FLT`）。

### 2.2 字符串

| 形式 | 规则 |
|---|---|
| `"..."` / `'...'` | 短串，不可跨行；转义 `\a \b \f \n \r \t \v \\ \" \' \xHH \u{...} \ddd \z \<换行>` |
| `` `...` `` | 模板字符串：允许多行；**恒返回 `TK_INTERPSTRING`**（无插值也是） |
| `$"..."` / `f"..."` | f-string 模式：`{expr}` 自动改写为 `${[expr]}`；`{{` 转义为字面 `{` |
| `${...}` | 统一插值：`${name}` 简单 / `${[expr]}` 复杂；预扫描闭合性，未闭合的 `${` 按普通字符（正则替换串 `"${%1}"` 因此安全） |
| `$$` | 串内转义：输出字面 `$` |
| `[[...]]` / `[=[...]=]` | 长字符串（`skip_sep` 计数等号）；与 map 字面量 `[[expr]=v]` 的歧义由缓冲扫描消解（见下） |
| `_raw"..."` / `_raw[[...]]` | 原生字符串：不处理任何转义（`TK_RAWSTRING`）；`_raw[k]` 仍是标识符 |

`[[` 歧义消解（源码 1244-1374）：前一个 token 是 `[`（嵌套键场景）→ 按普通
`[`；否则 `sep==2` 时前向扫描 `]]` 或 `]=` 特征判定 map 字面量；其余按长串。

### 2.3 正则字面量（`read_regex`）

`/pattern/flags`：pattern 内 `\` 转义、跨行即报 `unfinished regex`；
flags 为后续字母（`i m s g ...`）。pattern 与 flags 以 `\0` 分隔存入
`seminfo->ts`，token 为 `TK_REGEX`。

**除 `/` 与正则的消歧**（1407-1440）：`/` 后必须紧跟
`字母 | \ | [ | ( | . | ^` 才可能是正则；且**前一 token 是表达式终结符**
（名字/数字/字符串/`)` `]` `}`/`nil/true/false`/`++`）时按除法，否则按正则。
`//` 是整除、`/=` 是除法赋值，优先级最高。

### 2.4 运算符（最长匹配顺序，从源码 switch 逐分支）

- `-`：`->`（TK_ARROW，lambda 箭头）→ `-=` → `--`（注释：短注释支持
  `@warnings` 指令、长注释 `--[[]]`）→ 减号。
- `=`：`==` → `=>`（TK_MEAN，箭头函数）→ 赋值。
- `<`：`<=`→再探 `>` 得 `<=>`（TK_SPACESHIP）→ `<>`（TK_MERGE）→
  `<|`（TK_REVPIPE）→ `<<`/`<<=` → 小于。
- `>`：`>=` → `>>`/`>>=` → 大于。
- `/`：见 2.3。
- `~`：`~=`（TK_NE）→ 按位异或。
- `!`：`!=`（TK_NE）→ `!`（TK_NOT，逻辑非）。
- `&`：`&&`→`&&=`（TK_ANDANDEQ）/`&&`（TK_AND 等价 `and`）→ `&=` → 按位与。
- `|`：`||`→`||=`（TK_OROREQ）/`||`（**token 码复用 `@`**）→ `|?>`（安全管道，
  不完整时回落 `|`）→ `|>`（TK_PIPE）→ `|=` → 按位或。
- `?`：`?.`（TK_OPTCHAIN）→ `??`→`??=`（TK_NULLCOALEQ）/`??`（TK_NULLCOAL）→
  三元条件 `?`。
- `+`：`+=` → `++`（TK_PLUSPLUS）→ 加。
- `*`/`%`/`^`：对应 `*=`/`%=`/`^=`（幂赋值）。
- `:`：`::`（标签）→ `:=`（TK_WALRUS 海象）→ 冒号。
- `$`：`$$`（TK_DOLLDOLL）→ `$"`/`$'`（插值串）→ `$`（TK_DOLLAR，宏前缀）。
- `@`：单独符号（装饰器）。
- `.`：`...` → `..=`（TK_CONCATEQ）→ `..` → `.5` 类数字 → 点。
- `[[`：长串/map 消歧（2.2）。

### 2.5 注释

`--` 短注释（到行尾，内容扫描 `@warnings` 指令）；`--[==[` 长注释
（不产生 token）。

---

## 三、关键函数（准确签名）

```c
void luaX_init (lua_State *L);        /* 驻留并固定保留字 + "_ENV" */
void luaX_setinput (lua_State *L, LexState *ls, ZIO *z, TString *source,
                    int firstchar);   /* 初始化 LexState（警告默认值也在此） */
void luaX_next (LexState *ls);        /* 推进 token（消耗 3 级 lookahead 链） */
int luaX_lookahead / luaX_lookahead2 / luaX_lookahead3 (LexState *ls);
      /* 1/2/3 级前瞻，维护 nospace 与 lasttoken 语义（正则消歧依赖） */
TString *luaX_newstring (LexState *ls, const char *str, size_t l);
      /* 锚定在扫描器表，编译期内存唯一 */
const char *luaX_token2str (LexState *ls, int token);
void luaX_warning (LexState *ls, const char *msg, WarningType wt);
      /* WS_ERROR 升级为 syntaxerror；支持 @warnings: disable-next */
l_noret luaX_syntaxerror (LexState *ls, const char *msg);
void luaX_pushincludefile (LexState *ls, const char *filename);  /* include 压栈 */
void luaX_addalias (LexState *ls, TString *name, Token *tokens, int ntokens);
```

---

## 四、运行验证（实测输出）

以下均经 `run_lua.sh` 实测（对应脚本见各模块文档）：

```
字面量:  0xFF=255  0b101=5  0o17=15  0x1.8p1=3.0        (lobject 验证)
插值:    $"hello {name}" → hello world                    (lcode 验证)
模板串:  多行 `...` 合法
范围:    1..5 → 5 元素表；1 .. 5 → "15"                   (lcode 验证)
管道:    5 |> f / f <| x / nil |?> f                      (lcode 验证)
错误格式: "...:22: '|' expected
           22 |   return (nil ?| f) ...
              |                   ^ here"                  (本文件源码定位渲染)
         无源文件时: "tokenpos: 10, Line: 1, LastToken: ''+'', description: ..."
警告:    "v_x.lua:10: warning: unused local variable 'v' [unused]"（stderr 直出）
```

正则/`&&`/`!`/`??`/`?.` 的语义验证在 `stdlib/lstrlib.md`（正则库）与后续
运算符文档补充。

---

## 五、与其他模块的关系

- 下游：`lparser.c` 消费 token（`nospace` 供范围字面量；`lasttoken` 语义
  在此维护）；`llex.h` 定义 `LexState/Token/SemInfo/IncludeState/Alias/
  WarningType`。
- 加密 include 复用 `utils/aes.c + sha256.c`（与 `lauxlib.c` 的壳解码同源）。
- 数字值解析在 `core/lobject.c`；长串哈希在 `core/lstring.c`。
- `llexer_compiler.c` 是面向 AST/工具链的另一套词法器；`llexerlib.c` 把
  词法能力暴露为 `lexer` 库（`linit.c` 注册）。
