# compiler/ — 编译器前端（文档索引）

双解析器架构：传统单遍 `lparser.c`（直出字节码）与 AST 系 `last_*.c`
（建 `last.h` AST 后经 `lcodegen.c` 出字节码）；表达式层共用 `core/lcode.c`。
词法双实现：`llex.c`（解释/编译主路径）与 `llexer_compiler.c`（工具链）。

| 文件 | 文档 | 状态 |
|---|---|---|
| llex.c | [llex.md](llex.md) | ✅ 完成（全部词法规则 + 65 保留字 + 警告指令 + include/别名） |
| lparser.c | — | 待办（15426 行，单遍解析器 + 全部语句文法） |
| last.c | — | 待办（AST 节点库） |
| last_parse.c | — | 待办（7543 行，AST 解析器） |
| last_serialize.c | — | 待办（AST 序列化） |
| last_unparse.c | — | 待办（AST → 源码） |
| last_visitor.c | — | 待办（访问者） |
| lcodegen.c | — | 待办（5233 行，AST→Proto） |
| llexer_compiler.c | — | 待办（工具链词法器） |
| llexerlib.c | — | 待办（`lexer` 库） |
| lasm.c | — | 待办（汇编器） |
| lbctc.c | — | 待办（3529 行，字节码→C） |

## 已验证语法清单（跨模块实测汇总）

- **字面量**：`0x`/`0b`/`0o` 进制、`1_000` 分隔符、十六进制浮点 `0x1.8p1`、
  `1..5` 无空格范围表（≤200 元素）、`[k=...]` map、`[]` 空 map、
  `$"...{expr}"`/`` `...` ``/`f"..."` 插值串、`_raw"..."` 原生串、
  `/re/flags` 正则。
- **运算符**：`|>`、`<|`、`|?>`、`<=>`、`<>`（表合并）、`?:` 三元、
  `??`/`??=`/`?.`、`:=`、`=>`（箭头函数）、`->`（lambda）、`++`、
  复合赋值全族、`&&`/`||`/`!`/`!=`、`in`、`is`、`instanceof`、`..=`。
- **语句/声明**：`try/catch/finally`、`async function`（主线程返回
  `Promise<Fulfilled>`；协程内 `await` 直执行）、`class`/`extends`、
  `struct`（值语义）、`superstruct`、`namespace`、`enum`、
  `switch/case`、`guard`（待补验）、`fsleep/fwake` 调用拦截。
- **警告**：16 类可开关，`---@warnings: ...` 指令，`[unused]` 等经
  stderr 直出；语法错误带源码行 + `^ here` 定位。

## 编译管线

```
源码 → llex（token，nospace/行号/警告）
     → lparser（传统路径）──────────────┐
     → last_parse（AST 路径）→ last AST → lcodegen
                                         ↓
                    core/lcode.c（表达式发射，共用）
                                         ↓
                          Proto → lasm/lbcdump/lbctc/ldump
```
