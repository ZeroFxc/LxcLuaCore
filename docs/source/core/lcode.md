# core/lcode.c + lcode.h — 表达式代码生成（指令发射/跳转回填/扩展语法翻译）

> 职责：把解析出的表达式翻译成字节码——常量池管理、寄存器分配、指令发射
> （`luaK_code*`）、跳转链回填、布尔短路、比较指令选择，以及 LXCLUA
> 扩展语法（管道、范围、三元、插值、switch 表达式、箭头函数）的翻译。
> 被 `lparser.c` 与 `compiler/lcodegen.c` 共同使用（本文件物理上在
> core/ 目录，属编译器前端）。

---

## 一、特性介绍

1. **指令发射**：`luaK_code`（裸指令）、`luaK_codeABCk/codeABx/codeABC`
   （带格式断言）、行号同步（每条指令对应 `lineinfo` 增量）。
2. **常量池**：`luaK_intK/numberK/stringK/closureK` 去重入池
   （`fs->k[]`），`luaK_codek` 发 `OP_LOADK`（超大索引走 `LOADKX+EXTRAARG`）。
3. **跳转管理**：`luaK_jump`（发 `OP_JMP` 挂链）、`luaK_concat`（链合并）、
   `luaK_patchlist/patchtohere`（回填到目标/当前位置）、`finaltarget`
   （跳跳转优化，限 100 层防环）。
4. **表达式落位**：`luaK_dischargevars`（变量→值）、`luaK_exp2reg/
   exp2nextreg/exp2anyreg/exp2anyregup/exp2val/exp2K`、
   `luaK_storevar`（按变量种类发射 SET*）、`luaK_self`（`a:f()` 的
   `OP_SELF`）、`luaK_indexed`（索引表达式归一）。
5. **短路**：`luaK_goiftrue/goiffalse`（`and/or` 的 `OP_TEST/TESTSET`
   跳转布局）、`luaK_goifnil`（nil 测试，供 `??`/可选链用，发 `OP_TESTNIL`）。
6. **LXCLUA 扩展语法翻译**（见下节，全部实测可运行）。
7. **常量折叠**：`luaK_exp2const` + `luaK_posfix` 对算术/位运算/连接做
   编译期求值（实测 `1+2*3` → `LOADI`，见 `lopcodes.md` 反汇编）。

---

## 二、扩展语法的代码生成（语法 → 指令序列）

### 2.1 范围字面量 `a..b`（`luaK_range`）

- **语法判定**：`..` **无空格**（词法 `nospace` 标记）且两侧均为整数字面量
  → 范围表；有空格或右侧非整数 → 正常字符串拼接（`lparser.c` 约 5457 行）。
- 生成：`OP_NEWTABLE`（数组段 = 元素数）+ 分批 `LOADI/SETLIST`
  （每批 ≤ `LFIELDS_PER_FLUSH=50`）。
- 上限 `MAX_RANGE_SIZE=200`，超限或降序报 `range too large (max 200 elements)`
  （降序 `2..1` 回退为拼接，见仓库提交历史）。
- 实测：`local t = 1..5` → `type(t)=="table", #t==5`；`1 .. 5` → `"15"`。

### 2.2 管道 `|>` / `<|` / `|?>`（`luaK_pipe/luaK_revpipe/luaK_safepipe`）

- `x |> f` ≡ `f(x)`：函数与参数布局到相邻寄存器，`OP_CALL f,2,2`；
  链式管道时把结果 `OP_MOVE` 下沉回首个参数寄存器（寄存器只能自顶回收）。
- 方法形式（`obj:method`，`is_pipe_self`）：self + 管道值共 2 参数。
- `f <| x` ≡ `f(x)`：顺序布函数、参数后 `OP_CALL`。
- `x |?> f`（安全管道，源码注释记法）：`OP_TESTNIL` 判 nil →
  nil 则跳过调用并 `OP_LOADNIL` 结果；否则正常 `OP_CALL`。
- `x |> f(a, _, b)`（占位符，`luaK_pipe_call`）：管道值插入 `_` 所在
  寄存器位置，无占位符则追加末尾。
- 实测：`5 |> dbl` = 10；`inc <| 41` = 42；`nil |?> f` = nil。

### 2.3 三元表达式 `c ? a : b`（`luaK_condexp`）

条件入寄存器 → `goiffalse` 跳出假分支 → 真值计算 → 跳过假分支的
`JMP` → 假值计算；回填出口链。（等价 `(c and a) or b` 但精确控制求值。）

### 2.4 字符串插值 `` `$"{...}"` ``（`luaK_interpstring`）

词法切出的插值段经解析后拼接为 `OP_CONCAT` 序列（字面段入常量池，
表达式段经 `tostring` 语义连接）。实测：`` $"hello {name}" `` → `"hello world"`。

### 2.5 其它扩展入口

- `luaK_switchexpression`：switch 表达式（`case` 对 + 跳转表）；
- `luaK_arrow_statement/luaK_arrow_expression`：箭头函数/语句；
- `luaK_codecheckglobal`：global 重定义检查（发 `OP_ERRNNIL` 配套）。

---

## 三、关键函数（准确签名，节选）

```c
int luaK_code (FuncState *fs, Instruction i);
int luaK_codeABCk (FuncState *fs, OpCode o, int a, int b, int c, int k);
int luaK_codeABx (FuncState *fs, OpCode o, int a, unsigned int bc);
int luaK_exp2const (FuncState *fs, const expdesc *e, TValue *v);
void luaK_nil (FuncState *fs, int from, int n);
int luaK_jump (FuncState *fs);
void luaK_ret (FuncState *fs, int first, int nret);
void luaK_patchlist (FuncState *fs, int list, int target);
void luaK_patchtohere (FuncState *fs, int list);
void luaK_concat (FuncState *fs, int *l1, int l2);
int luaK_intK/numberK/stringK/closureK (FuncState *fs, ...);
void luaK_int (FuncState *fs, int reg, lua_Integer i);   /* LOADI/sBx/LOADK 选择 */
void luaK_dischargevars (FuncState *fs, expdesc *e);
void luaK_exp2reg (FuncState *fs, expdesc *e, int reg);
void luaK_exp2nextreg / luaK_exp2anyreg / luaK_exp2anyregup / luaK_exp2val;
void luaK_storevar (FuncState *fs, expdesc *var, expdesc *ex);
void luaK_self (FuncState *fs, expdesc *e, expdesc *key);
void luaK_goiftrue / luaK_goiffalse / luaK_goifnil (FuncState *fs, expdesc *e);
void luaK_indexed (FuncState *fs, expdesc *t, expdesc *k);
void luaK_prefix (FuncState *fs, UnOpr opr, expdesc *e, int line);
void luaK_infix (FuncState *fs, BinOpr op, expdesc *v);
void luaK_posfix (FuncState *fs, BinOpr opr, expdesc *e1, expdesc *e2, int line);
void luaK_setreturns (FuncState *fs, expdesc *e, int nresults);
void luaK_setoneret (FuncState *fs, expdesc *e);
void luaK_setlist (FuncState *fs, int base, int nelems, int tostore);
void luaK_settablesize (FuncState *fs, int pc, int ra, int asize, int hsize);
void luaK_fixline (FuncState *fs, int line);
void luaK_finish (FuncState *fs);
/* 扩展 */
void luaK_range (FuncState *fs, expdesc *v, lua_Integer start, lua_Integer end, int line);
void luaK_pipe (FuncState *fs, expdesc *e1, expdesc *e2);
void luaK_revpipe (FuncState *fs, expdesc *e1, expdesc *e2);
void luaK_safepipe (FuncState *fs, expdesc *e1, expdesc *e2);
void luaK_pipe_call (FuncState *fs, expdesc *pipe_arg, int func_reg,
                     int nargs, int placeholder_pos);
void luaK_condexp (FuncState *fs, expdesc *v1, expdesc *v2, expdesc *v3);
void luaK_interpstring (LexState *ls, expdesc *v);
void luaK_switchexpression (LexState *ls, expdesc *v);
void luaK_arrow_statement / luaK_arrow_expression (LexState *ls, expdesc *v);
void luaK_codecheckglobal (FuncState *fs, expdesc *var, int k, int line);
l_noret luaK_semerror (LexState *ls, const char *fmt, ...);
```

运算符映射：`binopr2op`（`BinOpr→OpCode`，基址偏移批量推导算术/位运算指令）、
`unopr2op`（`OPR_MINUS→OP_UNM` 等）、`binopr2TM`（元方法兜底事件）。

---

## 四、运行验证（实测输出）

脚本 `v_lcode.lua` + `v_range.lua`（`run_lua.sh`）：

```
range 1..5:	true	table	5	1	5       -- 无空格 .. = 范围表
concat:	string	15                          -- 有空格 .. = 拼接
pipe 5 |> dbl:	true	10
revpipe inc <| 41:	true	42
safepipe:	true	nil                        -- nil |?> f 短路为 nil
short-circuit:	true	1
interp:	true	hello world                  -- $"hello {name}"
```

配合 `lopcodes.md` 的反汇编（`luac -l`）：`1 + 2*3` 折叠为 `LOADI 3`、
`<=>` 生成 `OP_SPACESHIP`——常量折叠与新指令的发射路径均验证。

---

## 五、与其他模块的关系

- 上游：`compiler/llex.c`（token/`nospace` 标记）、`compiler/lparser.c`
  （表达式文法驱动本文件）；`compiler/lcodegen.c`（AST 路径复用同一套
  `luaK_*` 发射器）。
- 下游：指令格式 `core/lopcodes.h`；执行 `vm/lvm.c`。
- `expdesc` 定义在 `lparser.h`（表达式中间表示：`VVOID/VNIL/VTRUE/VKINT/
  VKFLT/VKSTR/VLOCAL/VUPVAL/VINDEXED/VCALL/VNONRELOC/VRELOC...`）。
