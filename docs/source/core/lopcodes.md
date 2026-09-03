# core/lopcodes.c + lopcodes.h + lopnames.h — 64 位指令集定义

> 职责：定义虚拟机指令的编码格式（**64 位、乱序位域**）、全部操作码枚举
> （标准 5.5 指令 + LXCLUA 扩展指令）、每条指令的模式表（`luaP_opmodes`）
> 与 OT/IT 判定。这是编译器（`lcodegen`）与 VM（`lvm`）之间的契约。

---

## 一、特性介绍

1. **64 位指令**：`Instruction` 为 64 位无符号整数；操作码占 10 位
   （`SIZE_OP=10`，`POS_OP=54`，位于指令顶部），最多 1024 个操作码，
   当前 `NUM_OPCODES = OP_CUSTOM+1`。
2. **乱序位域（Custom Scrambled Layout）**：头部注释画的是标准 5.5 布局
   （Op 9 位），但实际宏采用自定布局——`POS_OP=54`、A/B/C 各 15 位、
   k 在 B 之后（`POS_k=30`），中间留空隙。这是静态反混淆措施之一。
3. **六种格式**：`iABC / ivABC / iABx / iAsBx / iAx / isJ`
   （`v`=变体宽度、`s`=有符号 excess-K、`x`=扩展）。
4. **自定义操作码空间（LXCLUA 扩展）**：`OP_CUSTOM` 用 `Ax` 携带用户操作码号，
   用户空间 256~511（`OP_CUSTOM_BASE=256`，`OP_CUSTOM_COUNT=256`），
   分派实现在 `vm/lvmustom.c`。
5. **大量扩展指令**：宇宙飞船 `<=>`（`OP_SPACESHIP`）、类型判断 `is`（`OP_IS`）、
   nil 测试（`OP_TESTNIL`）、OOP 全套（15 条）、Trait（3 条）、切片（`OP_SLICE`）、
   `OP_NOP`（混淆占位）、`OP_CASE`、Concept/Namespace/SuperStruct、
   async（`OP_ASYNCWRAP/OP_AWAIT`）、泛型包装、`OP_CHECKTYPE`、表合并（`OP_MERGE`）、
   正则字面量（`OP_REGEX`）、Map（`OP_NEWMAP/MAPGET/MAPSET`）。

---

## 二、指令编码（实际生效的宏，从源码抄录）

| 常量 | 值 | 说明 |
|---|---|---|
| `SIZE_OP` | 10 | 操作码位宽 |
| `SIZE_A / SIZE_B / SIZE_C` | 15 / 15 / 15 | iABC 三操作数 |
| `SIZE_vB / SIZE_vC` | 14 / 16 | ivABC 变体 |
| `SIZE_Bx` | 31（=15+15+1） | `POS_Bx = POS_B` |
| `SIZE_Ax / SIZE_sJ` | 46 | `POS_Ax = POS_sJ = POS_A` |
| `POS_A` | 0 | `POS_B=15`、`POS_k=30`、`POS_C=31`、`POS_OP=54` |

要点：

- 有符号参数用 **excess-K** 表示：`OFFSET_sBx = MAXARG_Bx>>1`，`GETARG_sBx(i) = raw - OFFSET`。
- `GETARG_sC` 同理（`OFFSET_sC = MAXARG_C>>1`）。
- 构造宏：`CREATE_ABCk(o,a,b,c,k)`、`CREATE_vABCk`、`CREATE_ABx`、`CREATE_Ax`、
  `CREATE_sJ(o,j,k)`——注意 `CREATE_sJ` 的调用方需自行加 `OFFSET_sJ`
  （宏内 `j<<POS_sJ` 未做偏移，`SETARG_sJ` 做了，两处语义不同，使用 `SETARG_sJ`）。
- 寄存器上限：`MAX_FSTACK = MAXARG_A`（32767），`NO_REG = MAX_FSTACK`。
- `LFIELDS_PER_FLUSH = 50`（SETLIST 批量阈值）。

## 三、模式表（opmode 位含义）

`luaP_opmodes[]` 每条一个字节，`opmode(mm,ot,it,t,a,m)` 打包：

| 位 | 宏 | 含义 |
|---|---|---|
| 0-2 | `getOpMode` | 指令格式（iABC..isJ） |
| 3 | `testAMode` | 是否设置寄存器 A |
| 4 | `testTMode` | 测试指令（下一条必须是 JMP） |
| 5 | `testITMode` | 使用上一条设置的 `top`（B==0 时） |
| 6 | `testOTMode` | 为下一条设置 `top`（C==0 时） |
| 7 | `testMMMode` | 元方法指令 |

`luaP_isOT(i)`：`OP_TAILCALL` 或（`testOTMode && C==0`）。
`luaP_isIT(i)`：`OP_SETLIST` 特判 `vB==0`，其余 `testITMode && B==0`。

---

## 四、操作码清单（按功能分组，语义注释从源码抄录）

**标准 5.5 指令（保留）**：`MOVE`、`LOADI/LOADF/LOADK/LOADKX`、
`LOADFALSE/LFALSESKIP/LOADTRUE/LOADNIL`、`GETUPVAL/SETUPVAL`、
`GETTABUP/GETTABLE/GETI/GETFIELD`、`SETTABUP/SETTABLE/SETI/SETFIELD`、
`NEWTABLE`、`SELF`、`ADDI`、`ADDK/SUBK/MULK/MODK/POWK/DIVK/IDIVK`、
`BANDK/BORK/BXORK`、`SHLI/SHRI`、`ADD/SUB/MUL/MOD/POW/DIV/IDIV`、
`BAND/BOR/BXOR/SHL/SHR`、`MMBIN/MMBINI/MMBINK`、`UNM/BNOT/NOT/LEN`、
`CONCAT`、`CLOSE/TBC`、`JMP`、`EQ/LT/LE/EQK/EQI/LTI/LEI/GTI/GEI`、
`TEST/TESTSET`、`CALL/TAILCALL`、`RETURN/RETURN0/RETURN1`、
`FORLOOP/FORPREP`、`TFORPREP/TFORCALL/TFORLOOP`、`SETLIST`、`CLOSURE`、
`VARARG`、`VARARGPREP`、`EXTRAARG`。

**LXCLUA 扩展指令**（语义注释照抄源码）：

| 指令 | 格式 | 语义 |
|---|---|---|
| `OP_SPACESHIP` | iABC | `R[A] := R[B] <=> R[C]`（返回 -1,0,1） |
| `OP_GETVARG` | iABC | `R[A] := R[B][R[C]]`，R[B] 为变参参数 |
| `OP_ERRNNIL` | iABx | `R[A] ~= nil` 时报错（`K[Bx-1]` 是全局名） |
| `OP_IS` | iABC k | `if ((type(R[A]) == K[B]) ~= k) then pc++` |
| `OP_TESTNIL` | iABC k | `if (R[B] is nil) == k then pc++ else R[A] := R[B]` |
| `OP_NEWCLASS` | iABx | 创建新类，类名 `K[Bx]` |
| `OP_INHERIT` | iABC | `R[A].__parent := R[B]` |
| `OP_MULTIINHERIT` | iABC | R[A] 继承 R[B] 中的多个父类（数组） |
| `OP_GETSUPER` | iABC | `R[A] := R[B].__parent[K[C]]`（调父类方法） |
| `OP_SETMETHOD` | iABC | `R[A][K[B]] := R[C]` |
| `OP_CHECKOVERRIDE` | iABC | 校验父类存在 `K[B]` 方法（override 关键字） |
| `OP_SETSTATIC` | iABC | `R[A].__static[K[B]] := R[C]` |
| `OP_NEWOBJ` | iABC | `R[A] := R[B]()`，C 个参数 |
| `OP_GETPROP/OP_SETPROP` | iABC | 属性读写（考虑继承链） |
| `OP_INSTANCEOF` | iABC k | `if ((R[A] instanceof R[B]) ~= k) then pc++` |
| `OP_IMPLEMENT` | iABC | R[A] 实现接口 R[B] |
| `OP_SETIFACEFLAG` | iABC | 置 R[A] 为接口（CLASS_FLAG_INTERFACE） |
| `OP_ADDMETHOD` | iABC | `R[A].__methods[K[B]] := K[C]`（接口方法签名） |
| `OP_EXTENDIFACE` | iABC | 接口继承父接口 |
| `OP_ASCLASS` | iABC | `R[A] := (R[B] instanceof R[C]) ? R[B] : nil` |
| `OP_IN` | iABC | `R[A] := R[B] in R[C]` |
| `OP_SETTRAITFLAG` | iABC | 置 R[A] 为 Trait |
| `OP_SETTRAITREQUIRE` | iABC | 注册 trait 所需方法 |
| `OP_USETRAIT` | iABC | R[A] use R[B]（复制 trait 方法） |
| `OP_STATICINIT` | iABC | `R[A] := static_init(R[B])` |
| `OP_SLICE` | iABC | `R[A] := slice(R[B], R[B+1..3])`；C: 0=普通 1=带步长 |
| `OP_NOP` | iABC | 空操作，混淆占位（参数携带虚假数据） |
| `OP_CASE` | iABC | `R[A] := { R[B], R[C] }`（case 对） |
| `OP_NEWCONCEPT` | iABx | `R[A] := concept(KPROTO[Bx])` |
| `OP_NEWNAMESPACE` | iABx | `R[A] := newnamespace(K[Bx])` |
| `OP_LINKNAMESPACE` | iABC | `R[A]->using_next = R[B]` |
| `OP_NEWSUPER` | iABx | `R[A] := newsuperstruct(K[Bx])` |
| `OP_SETSUPER` | iABC | `R[A][B] := R[C]` |
| `OP_GETCMDS` | iABC | `R[A] := LXC_CMDS` |
| `OP_GETOPS` | iABC | `R[A] := LXC_OPERATORS` |
| `OP_ASYNCWRAP` | iABC | `R[A] := async_wrap(R[B])` |
| `OP_AWAIT` | iABC | `R[A] := await(R[B])`（协程 yield Promise） |
| `OP_GENERICWRAP` | iABC | `R[A] := generic_wrap(R[B], R[B+1], R[B+2])` |
| `OP_CHECKTYPE` | iABC | 类型检查失败报 `K[C]` |
| `OP_MERGE` | iABC | `R[A] := merge(R[B], R[C])`（表合并 `<>`） |
| `OP_REGEX` | iABx | `R[A] := regex(K[Bx])`（正则字面量） |
| `OP_NEWMAP` | ivABC | `R[A] := []`（创建 map） |
| `OP_MAPGET/OP_MAPSET` | iABC | map 下标读/写 |
| `OP_CUSTOM` | iAx | 分派到自定义操作码处理器（Ax 携带用户 opcode） |

语法对照（用户可见语法 → 指令）：`<=>`→SPACESHIP；`x is T`→IS；
`a instanceof B`→INSTANCEOF；`t[a:b:c]`→SLICE；`{} <> {}`→MERGE；
`/re/`→REGEX；`map{}`→NEWMAP；`await e`→AWAIT；`async function`→ASYNCWRAP。

---

## 五、运行验证（实测输出）

`luac -l` 反汇编（`run_lua.sh` 同 PATH 规则，工具前需加
`/e/Env/llvm-mingw-.../bin`）：

```
main <op_snip.lua:0,0> (6 instructions)
0 params, 2 slots, 1 upvalue, 1 local, 0 constants, 0 functions
	1	[1]	VARARGPREP	0
	2	[1]	LOADI    	0 3
	3	[2]	LOADI    	1 3
	4	[2]	SPACESHIP	1 0 1
	5	[2]	RETURN   	1 2 1	; 1 out
	6	[2]	RETURN   	0 1 1	; 0 out
```

源码 `local x = 1 + 2; return x <=> 3`。可见：`1+2` 被常量折叠为
`LOADI 3`（载入立即数）；扩展指令 `SPACESHIP` 正确生成；`RETURN` 的
`k` 位（第三参 1）表示函数可能建上值。

---

## 六、与其他模块的关系

- 编码宏的消费者：`compiler/lcodegen.c`（发射）、`core/lcode.c`（表达式层）、
  `compiler/lasm.c`（汇编）、`core/ldump.c`/`lundump.c`（序列化）。
- 执行者：`vm/lvm.c` 主循环；`OP_CUSTOM` 分派到 `vm/lvmustom.c`。
- 指令名表 `lopnames.h` 供 `luac -l`/`lbcdump` 打印（操作数名与枚举顺序严格对应）。
