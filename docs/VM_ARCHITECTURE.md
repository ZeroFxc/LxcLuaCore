# VM 架构

> 基于 `lopcodes.h`(554行) + `lvm.h`(156行) + `llimits.h`(501行) 源码精确分析

---

## 1. 指令集架构

### 1.1 指令格式

Instruction 定义为 `typedef l_uint64 Instruction;`（llimits.h:264）。6 种格式：

```
位: 63 62 61 60 59 58 57 56 55 54 53 52 ... 46 45 ... 31 30 29 ... 15 14 ... 0
     |--- OP (10 bit) ---|  |  gap  |  | C (15) |k| B (15) |  |  A (15)  |

iABC:   C(15) | B(15) | k(1) | A(15) | OP(10)
ivABC:  vC(16) | vB(14) | k(1) | A(15) | OP(10)
iABx:   Bx(31) | A(15) | OP(10)
iAsBx:  sBx(31 signed) | A(15) | OP(10)
iAx:    Ax(46) | OP(10)
isJ:    sJ(46 signed) | OP(10)
```

### 1.2 位域位置（lopcodes.h:42-76）

| 字段 | 位置 | 位数 | 最大值 |
|------|------|------|--------|
| OP | 54-63 | 10 | 1023 |
| A | 0-14 | 15 | 32767 |
| B | 15-29 | 15 | 32767 |
| k | 30 | 1 | 1 |
| C | 31-45 | 15 | 32767 |
| vB | 15-28 | 14 | 16383 |
| vC | 31-46 | 16 | 65535 |
| Bx | 15-45 | 31 | 2^31-1 |
| Ax | 0-45 | 46 | 2^46-1 |
| sJ | 0-45 | 46 | 2^46-1 |

### 1.3 指令编码宏

```c
// 创建指令
CREATE_ABCk(o,a,b,c,k)   // iABC 格式
CREATE_vABCk(o,a,b,c,k)  // ivABC 格式
CREATE_ABx(o,a,bc)        // iABx 格式
CREATE_Ax(o,a)            // iAx 格式
CREATE_sJ(o,j,k)          // isJ 格式

// 提取字段
GET_OPCODE(i)             // 操作码
GETARG_A(i) / GETARG_B(i) / GETARG_C(i)  // 寄存器
GETARG_Bx(i) / GETARG_sBx(i)             // 常量/跳转
GETARG_Ax(i) / GETARG_sJ(i)              // 扩展参数
```

### 1.4 指令属性位掩码（lopcodes.h:527-549）

```c
#define getOpMode(m)    (luaP_opmodes[m] & 7)     // 位0-2: 指令格式
#define testAMode(m)    (luaP_opmodes[m] & (1<<3)) // bit3: 设置寄存器A
#define testTMode(m)    (luaP_opmodes[m] & (1<<4)) // bit4: 测试指令(后跟跳转)
#define testITMode(m)   (luaP_opmodes[m] & (1<<5)) // bit5: 使用L->top (B==0)
#define testOTMode(m)   (luaP_opmodes[m] & (1<<6)) // bit6: 设置L->top (C==0)
#define testMMMode(m)   (luaP_opmodes[m] & (1<<7)) // bit7: 元方法指令
```

---

## 2. 操作码全表

### 2.1 数据移动

| 指令 | 格式 | 说明 |
|------|------|------|
| OP_MOVE | iABC | R[A] := R[B] |
| OP_LOADI | iAsBx | R[A] := sBx (整数即值) |
| OP_LOADF | iAsBx | R[A] := (lua_Number)sBx |
| OP_LOADK | iABx | R[A] := K[Bx] |
| OP_LOADKX | iABx | R[A] := K[EXTRAARG] |
| OP_LOADFALSE | iABC | R[A] := false |
| OP_LFALSESKIP | iABC | R[A] := false; pc++ |
| OP_LOADTRUE | iABC | R[A] := true |
| OP_LOADNIL | iABC | R[A..A+B] := nil |
| OP_GETUPVAL | iABC | R[A] := UpValue[B] |
| OP_SETUPVAL | iABC | UpValue[B] := R[A] |

### 2.2 表访问

| 指令 | 格式 | 说明 |
|------|------|------|
| OP_GETTABLE | iABC | R[A] := R[B][R[C]] |
| OP_GETI | iABC | R[A] := R[B][C] |
| OP_GETFIELD | iABC | R[A] := R[B][K[C]] |
| OP_SETTABLE | iABC | R[A][R[B]] := RK(C) |
| OP_SETI | iABC | R[A][B] := RK(C) |
| OP_SETFIELD | iABC | R[A][K[B]] := RK(C) |
| OP_NEWTABLE | ivABC | R[A] := {} |
| OP_GETTABUP | iABC | R[A] := UpValue[B][K[C]] |
| OP_SETTABUP | iABC | UpValue[A][K[B]] := RK(C) |
| OP_SELF | iABC | R[A+1]:=R[B]; R[A]:=R[B][K[C]] |

### 2.3 算术/位运算

| 指令 | 说明 |
|------|------|
| OP_ADD/OP_ADDI/OP_ADDK | R[A] := R[B] + (R[C]/sC/K[C]) |
| OP_SUB/OP_SUBK | R[A] := R[B] - (R[C]/K[C]) |
| OP_MUL/OP_MULK | R[A] := R[B] * (R[C]/K[C]) |
| OP_DIV/OP_DIVK | R[A] := R[B] / (R[C]/K[C]) |
| OP_IDIV/OP_IDIVK | R[A] := R[B] // (R[C]/K[C]) |
| OP_MOD/OP_MODK | R[A] := R[B] % (R[C]/K[C]) |
| OP_POW/OP_POWK | R[A] := R[B] ^ (R[C]/K[C]) |
| OP_BAND/OP_BANDK | R[A] := R[B] & (R[C]/K[C]) |
| OP_BOR/OP_BORK | R[A] := R[B] \| (R[C]/K[C]) |
| OP_BXOR/OP_BXORK | R[A] := R[B] ~ (R[C]/K[C]) |
| OP_SHL/OP_SHLI | R[A] := R[B] << (R[C]/sC) |
| OP_SHR/OP_SHRI | R[A] := R[B] >> (R[C]/sC) |
| OP_UNM | R[A] := -R[B] |
| OP_BNOT | R[A] := ~R[B] |
| OP_NOT | R[A] := not R[B] |
| OP_SPACESHIP | R[A] := R[B] <=> R[C] |

### 2.4 比较/跳转

| 指令 | 说明 |
|------|------|
| OP_EQ/OP_EQK/OP_EQI | if (R[A] == R[B]/K[B]/sB) ~= k then pc++ |
| OP_LT/OP_LTI | if (R[A] < R[B]/sB) ~= k then pc++ |
| OP_LE/OP_LEI | if (R[A] <= R[B]/sB) ~= k then pc++ |
| OP_GTI/OP_GEI | if (R[A] >/>= sB) ~= k then pc++ |
| OP_TEST | if (not R[A] == k) then pc++ |
| OP_TESTSET | if (not R[B] == k) then pc++ else R[A]:=R[B] |
| OP_JMP | pc += sJ |
| OP_IS | if (type(R[A]) == K[B]) ~= k then pc++ |
| OP_TESTNIL | if (R[B] is nil) == k then pc++ else R[A]:=R[B] |

### 2.5 函数调用

| 指令 | 说明 |
|------|------|
| OP_CALL | R[A..A+C-2] := R[A](R[A+1..A+B-1]) |
| OP_TAILCALL | return R[A](R[A+1..A+B-1]) |
| OP_RETURN | return R[A..A+B-2] |
| OP_RETURN0 | return (无返回值) |
| OP_RETURN1 | return R[A] |
| OP_CLOSURE | R[A] := closure(KPROTO[Bx]) |
| OP_VARARG | R[A..A+C-2] = varargs |
| OP_VARARGPREP | 调整 varargs |
| OP_GETVARG | R[A] := R[B][R[C]] (R[B] is vararg) |
| OP_ERRNNIL | raise error if R[A] ~= nil |

### 2.6 循环

| 指令 | 说明 |
|------|------|
| OP_FORLOOP | 更新计数器; if 继续 then pc-=Bx |
| OP_FORPREP | 检查值; if 不执行 then pc+=Bx+1 |
| OP_TFORPREP | 创建 upvalue for R[A+3]; pc+=Bx |
| OP_TFORCALL | R[A+4..A+3+C] := R[A](R[A+1],R[A+2]) |
| OP_TFORLOOP | if R[A+2] ~= nil then {R[A]=R[A+2]; pc-=Bx} |

### 2.7 OOP 指令

| 指令 | 说明 |
|------|------|
| OP_NEWCLASS | R[A] := 创建新类，类名 K[Bx] |
| OP_INHERIT | R[A].__parent := R[B] |
| OP_MULTIINHERIT | R[A] 从 R[B] 数组多继承 |
| OP_GETSUPER | R[A] := R[B].__parent[K[C]] |
| OP_SETMETHOD | R[A][K[B]] := R[C] |
| OP_CHECKOVERRIDE | 校验父类存在 K[B] 方法 |
| OP_SETSTATIC | R[A].__static[K[B]] := R[C] |
| OP_NEWOBJ | R[A] := R[B]()，C 个参数 |
| OP_GETPROP | R[A] := R[B][K[C]]（考虑继承链） |
| OP_SETPROP | R[A][K[B]] := RK(C) |
| OP_INSTANCEOF | if (R[A] instanceof R[B]) ~= k then pc++ |
| OP_IMPLEMENT | R[A] implements R[B] |
| OP_SETIFACEFLAG | 设置 R[A] 为接口 |
| OP_ADDMETHOD | R[A].__methods[K[B]] := K[C] |
| OP_EXTENDIFACE | R[A].__parent := R[B] |
| OP_ASCLASS | R[A] := (R[B] instanceof R[C])?R[B]:nil |

### 2.8 Trait 指令

| 指令 | 说明 |
|------|------|
| OP_SETTRAITFLAG | 设置 R[A] 为 Trait |
| OP_SETTRAITREQUIRE | R[A].__trait_requires[K[B]] := C |
| OP_USETRAIT | R[A] use R[B] |
| OP_STATICINIT | R[A] := static_init(R[B]) |

### 2.9 扩展指令

| 指令 | 说明 |
|------|------|
| OP_NEWMAP | R[A] := [] (创建 map) |
| OP_MAPGET | R[A] := R[B][R[C]] |
| OP_MAPSET | R[A][R[B]] := RK(C) |
| OP_NEWCONCEPT | R[A] := concept(KPROTO[Bx]) |
| OP_NEWNAMESPACE | R[A] := newnamespace(K[Bx]) |
| OP_LINKNAMESPACE | R[A]->using_next = R[B] |
| OP_NEWSUPER | R[A] := newsuperstruct(K[Bx]) |
| OP_SETSUPER | R[A][B] := R[C] |
| OP_SLICE | R[A] := slice(R[B..B+3]) |
| OP_MERGE | R[A] := merge(R[B], R[C]) |
| OP_REGEX | R[A] := regex(K[Bx]) |
| OP_ASYNCWRAP | R[A] := async_wrap(R[B]) |
| OP_AWAIT | R[A] := await(R[B]) |
| OP_GENERICWRAP | R[A] := generic_wrap(R[B..B+2]) |
| OP_CHECKTYPE | if check_type(R[A],R[B])!=true then error(K[C]) |
| OP_GETCMDS | R[A] := LXC_CMDS |
| OP_GETOPS | R[A] := LXC_OPERATORS |
| OP_CASE | R[A] := { R[B], R[C] } |
| OP_IN | R[A] := R[B] in R[C] |
| OP_NOP | 空操作（混淆用） |
| OP_EXTRAARG | 前一条指令的扩展参数 |
| OP_CUSTOM | 自定义操作码分发（opcode in Ax） |

### 2.10 操作码统计

```c
#define NUM_OPCODES  ((int)(OP_CUSTOM) + 1)  // 内建操作码总数
#define OP_CUSTOM_BASE  256                    // 自定义操作码起始
#define OP_CUSTOM_COUNT 256                    // 自定义操作码最大数量
```

---

## 3. 执行引擎

### 3.1 线程化解释器

```c
void luaV_execute(lua_State *L, CallInfo *ci);
```

使用计算 goto 的指令分发，每条指令处理完直接跳转到下一条。

### 3.2 寄存器机

每个函数有固定数量的寄存器 R[0] ~ R[maxstack-1]：
- R[0]: 返回值/第一个参数
- R[1..numparams-1]: 参数
- R[numparams..maxstack-1]: 局部变量和临时值

最大寄存器数: `MAX_FSTACK = MAXARG_A = (1<<15)-1 = 32767`

### 3.3 常量表

RK(x) 语义:
- 如果 k(i) == 0: 取 R[x]
- 如果 k(i) == 1: 取 K[x] (常量表)

---

## 4. 自定义操作码系统

```c
#define OP_CUSTOM_BASE  256   // 用户自定义 opcode 起始值
#define OP_CUSTOM_COUNT 256   // 最多 256 个自定义操作码
#define OP_CUSTOM_MAX   (OP_CUSTOM_BASE + OP_CUSTOM_COUNT - 1) // 511
```

OP_CUSTOM 指令使用 Ax 字段携带实际的用户操作码号（0-255），经过 OP_CUSTOM_BASE 偏移到 256-511。

---

## 5. 垃圾回收

三色增量标记-清除 GC（lgc.c）：
- 增量模式: 分步执行，减少暂停
- 分代模式: 新生代 + 老年代

```c
// GC 控制系统
lua_gc(L, LUA_GCSTOP, ...)       // 停止 GC
lua_gc(L, LUA_GCRESTART, ...)    // 重启
lua_gc(L, LUA_GCCOLLECT, ...)    // 完整 GC
lua_gc(L, LUA_GCCOUNT, ...)      // 内存使用
lua_gc(L, LUA_GCSTEP, ...)       // 增量步进
lua_gc(L, LUA_GCGEN, ...)        // 分代模式
lua_gc(L, LUA_GCINC, ...)        // 增量模式
```

---

## 6. 异步系统

```c
// 纯语法级函数指针设置（替代注册表查找）
LUAI_FUNC void lvm_set_async_runner(void *fn);
LUAI_FUNC int lvm_async_invoke(lua_State *L);
```

`PF_ASYNC` 标志标记异步函数，调用时走 VM 直接路径而非注册表查找。

---

## 7. 调用队列

```c
#define MAX_CALL_ARGS 64

typedef struct CallNode {
    int nargs;
    TValue args[MAX_CALL_ARGS];
    struct CallNode *next;
} CallNode;

typedef struct {
    CallNode *head;
    CallNode *tail;
    int size;
} CallQueue;
```

用于函数 sleep/wake 机制，函数休眠时将调用排队，唤醒时恢复执行。