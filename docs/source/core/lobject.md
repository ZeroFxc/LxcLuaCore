# core/lobject.c + lobject.h — 对象系统与类型表示

> 职责：定义引擎中「一切值」的内存表示（Tagged Value），以及跨模块通用的
> 对象操作工具：算术分发、字符串↔数字转换、格式化压栈、chunk 标识。
> 这是整个引擎的地基，`lstate.h`/`lvm.c`/`ltable.c` 等均依赖本文件。

---

## 一、特性介绍

1. **15 种对外类型 + 2 种内部类型的统一表示**：所有值都是 `TValue`（值联合体 + 1 字节类型标签）。
2. **类型标签位布局**：`tt_` 字节的位 0-3 是基础类型（`LUA_T*`），位 4-5 是变体位（如整数/浮点/大整数），位 6 标记「可回收对象」。
3. **BigInt 一等公民**：大整数是独立的 number 变体 `LUA_VNUMBIG`（标签值 99），底层是堆上的 `TBigInt` GC 对象。
4. **struct 值语义深拷贝**：`setobj` 被改写——赋值一个 struct 值时自动调用 `luaS_copystruct` 深拷贝，而其它类型仍是平凡拷贝。这是「值类型结构体」语义的落点。
5. **字面量进制扩展**：`luaO_str2num` 的整数路径支持 `0x`（十六进制）、`0b`（二进制）、`0o`（八进制）前缀（标准 Lua 只有 `0x`）。
6. **十六进制浮点字面量**：自实现 `lua_strx2number`，按 C99 `strtod` 语义解析 `0x1.8p3` 形式。
7. **拒绝 `inf`/`nan` 字面量**：`l_str2d` 显式拦截含 `n`/`N` 的字符串。
8. **`luaO_pushvfstring` 格式化**：引擎内部所有错误消息的格式化工具，支持 `%s %c %d %I %f %p %U %%`。

---

## 二、关键数据结构与属性（lobject.h）

### 2.1 值表示核心

| 结构/宏 | 说明 |
|---|---|
| `Value`（union） | 所有裸值的联合体：`gc`（GC对象）、`p`（light userdata）、`ptr`（原始指针）、`struct_`、`superstruct`、`ns`（命名空间）、`f`（light C function）、`i`（整数）、`n`（浮点） |
| `TValue` | `{ Value value_; lu_byte tt_; }` —— 带标签的值，引擎内值的通用单位 |
| `StackValue` | 栈槽：`TValue val` + `tbclist.delta`（to-be-closed 变量链表偏移） |
| `StkId` | `StackValue*`，栈索引指针 |
| `GCObject` | 所有可回收对象的公共头：`next` 指针 + `tt` 标签 + `marked` GC 标记位 |

标签操作宏：`rawtt(o)` 取原始标签；`ttype(o)` 取去变体基础类型；`ctb(t)` 给标签加可回收位；`makevariant(t,v)` 合成变体标签。

### 2.2 nil 的四个变体（标准 Lua 语义保留）

| 变体 | 用途 |
|---|---|
| `LUA_VNIL` | 标准 nil |
| `LUA_VEMPTY` | 表空槽 |
| `LUA_VABSTKEY` | 键缺失返回值（`ABSTKEYCONSTANT`） |
| `LUA_VNOTABLE` | fast get 访问了非表对象时的信号 |

### 2.3 number 的三个变体

| 变体 | 宏 | 说明 |
|---|---|---|
| `LUA_VNUMINT` | `ttisinteger` / `ivalue` / `setivalue` | 64 位整数 |
| `LUA_VNUMFLT` | `ttisfloat` / `fltvalue` / `setfltvalue` | double |
| `LUA_VNUMBIG` = 99 | `ttisbigint` / `bigvalue` / `setbigvalue` | 堆上 `TBigInt`（GC 对象） |

```c
typedef struct TBigInt {
  CommonHeader;          // GC 头
  unsigned int len;      // limb 个数
  int sign;              // 1 或 -1
  l_uint32 buff[1];      // limb 数组（小端），柔性数组
} TBigInt;
```

`nvalue(o)` 对三种变体统一转 `lua_Number`（bigint 经 `luaB_bigtonumber`）。

### 2.4 字符串

```c
typedef struct TString {
  CommonHeader;
  lu_byte extra;      // 短串: 是否保留字；长串: 是否有 hash
  lu_byte shrlen;     // 短串长度；长串固定为 0xFF
  unsigned int hash;
  union { size_t lnglen; struct TString *hnext; } u;
  char contents[1];   // 柔性内容
} TString;
```

- 短串 `LUA_VSHRSTR` 全局驻留（哈希表去重）；长串 `LUA_VLNGSTR` 不去重。
- **外部字符串** `TExternalString`（LXCLUA 扩展）：内容不在对象内，而是 `const char *src` 外挂指针，可带释放回调 `falloc/ud`。`shrlen` 取值 `LSTRREG(-1)` 普通 / `LSTRFIX(-2)` 固定外部 / `LSTRMEM(-3)` 需释放的外部。判断宏 `isextstr(ts)`。
- 取内容统一用 `getstr(ts)`，取长度用 `tsslen(s)`。

### 2.5 Struct（值类型结构体，LXCLUA 扩展）

```c
typedef struct Struct {
  CommonHeader;
  GCObject *gclist;
  struct Table *def;      // 结构定义（类型信息，用 Table 承载）
  int *gc_offsets;        // 需要 GC 追踪的字段偏移数组
  int n_gc_offsets;
  size_t data_size;       // 数据块字节数
  struct GCObject *parent;// 若本对象是某个对象的视图，指向父对象
  lu_byte *data;          // 数据指针
  union { LUAI_MAXALIGN; lu_byte d[1]; } inline_data; // 内联数据块
} Struct;
```

语义要点：赋值即深拷贝（见 `setobj`）；`data` 可指向内联块或外部块；`gc_offsets` 告诉 GC 哪些偏移处有可回收值。

### 2.6 Userdata

- `Udata`（带 `nuvalue` 个用户值 + 元表 + `gclist`）；`Udata0`（无用户值的紧凑版，无需置灰）。
- `getudatamem(u)` 取数据区指针；`sizeudata(nuv,nb)` 算总分配大小。
- **Pointer 类型**（`LUA_VPOINTER`，LXCLUA 扩展）：裸指针值，`ptrvalue(o)`/`setptrvalue`，不是 userdata。

### 2.7 Proto（函数原型）——字节码的载体

```c
typedef struct Proto {
  CommonHeader;
  Instruction *code;         // 指令数组
  TValue *k;                 // 常量表
  struct Proto **p;          // 内嵌函数原型
  unsigned short numparams;  // 固定参数个数
  lu_byte flag;              // 见下方标志位
  unsigned short maxstacksize; // 寄存器数
  lu_byte nodiscard;         // nodiscard 标记（LXCLUA 扩展）
  unsigned int difierline_mode;  // 混淆模式标志（LXCLUA 保护字段）
  int difierline_pad;            // 混淆填充
  int difierline_magicnum;       // 识别魔数
  uint64_t difierline_data;      // 混淆附加数据
  uint64_t bytecode_hash;        // 字节码防篡改哈希
  int sizeupvalues, sizek, sizecode, sizelineinfo, sizep,
      sizelocvars, sizeabslineinfo;   // 各数组长度
  int linedefined, lastlinedefined;   // 起止行（调试）
  Upvaldesc *upvalues;       // upvalue 描述
  ls_byte *lineinfo;         // pc → 行号增量
  AbsLineInfo *abslineinfo;  // pc → 绝对行号
  LocVar *locvars;           // 局部变量调试信息
  TString *source;           // 源文件名
  GCObject *gclist;
  int is_sleeping;           // sleep/wake 机制状态（LXCLUA 扩展）
  CallQueue *call_queue;     // sleep 期间的调用队列
  struct VMCodeTable *vm_code_table; // VM 保护码表（lvmustom.c）
} Proto;
```

`flag` 位：`PF_VAHID`(1) 隐藏变参、`PF_VATAB`(2) 需要变参表、`PF_FIXED`(4) 定长内存、`PF_LOCKED`(8) 只读字节码、`PF_ASYNC`(16) async 函数（纯语法级标记）。

**调用队列**（sleep/wake 机制，LXCLUA 扩展）：`CallNode` 存一次调用的 `nargs` + `args[MAX_CALL_ARGS=64]`，`CallQueue` 是单链表 + 计数。

### 2.8 闭包与 UpVal

- `ClosureHeader` = CommonHeader + `nupvalues` + `ishotfixed`（热修补标记，LXCLUA 扩展）+ `gclist`。
- `LClosure`（Lua 闭包：`Proto *p` + `UpVal *upvals[]`）、`CClosure`（C 闭包：`lua_CFunction f` + `TValue upvalue[]`）、`Closure`（二者联合体）。
- 函数三变体：`LUA_VLCL`（Lua 闭包）、`LUA_VLCF`（light C function，非 GC）、`LUA_VCCL`（C 闭包）。
- `UpVal`：`v.p` 指向栈槽（open）或自身 `u.value`（closed）；open 时挂在 `lua_State` 的 openupval 链表上。
- **Concept**（`LUA_VCONCEPT`，LXCLUA 扩展）：概念约束对象，布局同 `LClosure`。

### 2.9 Namespace / SuperStruct / Map（LXCLUA 扩展类型）

```c
typedef struct Namespace {      // LUA_TNAMESPACE
  CommonHeader;
  struct Table *data;           // 成员表
  TString *name;
  GCObject *gclist;
  struct Namespace *using_next; // using 导入链
} Namespace;

typedef struct SuperStruct {    // LUA_TSUPERSTRUCT（带继承的结构体/类元数据）
  CommonHeader;
  GCObject *gclist;
  TString *name;
  unsigned int nsize, ncapacity;
  TValue *data;
} SuperStruct;

typedef struct MapNode {        // 纯哈希节点：任意类型键
  TValue key, val;
  struct MapNode *next;         // 链地址法
} MapNode;

typedef struct Map {            // LUA_TMAP：无数组段、无元表、无 flags
  CommonHeader;
  MapNode **buckets;            // 桶数组（2 的幂）
  unsigned int size, count;
  GCObject *gclist;
} Map;
```

`MAP_INITIAL_BUCKETS` = 8。Map 与 Table 完全隔离，无元方法开销。

### 2.10 Table

```c
typedef struct Table {
  CommonHeader;
  lu_byte flags;          // 1<<p 位为 1 表示没有元方法 p（缓存）
  lu_byte lsizenode;      // 哈希部分大小 = 2^lsizenode
  unsigned int alimit;    // 数组部分"界限"（可能非真实大小，见 BITRAS）
  TValue *array;          // 数组段
  Node *node;             // 哈希段
  Node *lastfree;         // 空闲位置探测起点
  struct GCObject *metatable;
  GCObject *gclist;
  lu_byte type;           // 自定义类型标志（LXCLUA 扩展）
  lu_byte is_shared;      // 共享锁启用标志（LXCLUA 扩展）
  l_rwlock_t lock;        // 读写锁（线程安全，LXCLUA 扩展）
  struct Namespace *using_next; // 命名空间导入链（LXCLUA 扩展）
} Table;
```

哈希节点 `Node` 把键拆成 `key_tt`+`key_val` 紧凑布局；`i_val` 联合体成员可直接把节点值当 `TValue` 读。
删除键后置 `LUA_TDEADKEY`（`setdeadkey`），保留原 gc 值供 `next` 遍历。
`lmod(s,size)` 是 2 的幂掩码取模；`sizenode(t)` = `1<<lsizenode`。

---

## 三、关键函数（lobject.c，准确签名）

```c
/* ceil(log2(x))，表扩容计算用 */
int luaO_ceillog2 (unsigned int x);

/* 把百分比编码为 1 字节浮点（eeeexxxx，excess-7），GC 参数用 */
lu_byte luaO_codeparam (unsigned int p);
/* 对 x 应用编码后的百分比参数 */
l_mem luaO_applyparam (lu_byte p, l_mem x);

/* 裸算术：不触发元方法。返回 1 成功 / 0 失败。
   op 取 LUA_OPADD/SUB/MUL/DIV/IDIV/MOD/POW/UNM/BAND/BOR/BXOR/SHL/SHR/BNOT。
   规则：任一大整数操作数 → 走 luaB_* 大数路径（仅加减乘除/模/幂）；
   位运算仅接受可转整数的操作数；DIV/POW 强制浮点；
   其余运算双整数走整数路径，否则转浮点。 */
int luaO_rawarith (lua_State *L, int op, const TValue *p1,
                   const TValue *p2, TValue *res);

/* 算术 + 元方法兜底：裸运算失败则调对应二元元方法（TM_ADD...） */
void luaO_arith (lua_State *L, int op, const TValue *p1,
                 const TValue *p2, StkId res);

/* 字符串 → 数字。成功返回消费的字符数+1，失败返回 0。
   整数路径支持 0x/0b/0o 前缀；浮点路径拒绝 inf/nan。 */
size_t luaO_str2num (const char *s, TValue *o);

/* 十六进制字符求值（0-15） */
lu_byte luaO_hexavalue (int c);

/* UTF-8 编码一个码点进 8 字节缓冲（从缓冲尾部倒着写），返回字节数 */
int luaO_utf8esc (char *buff, l_uint32 x);

/* 数字 → 字符串缓冲。整数走 lua_integer2str；浮点先按默认精度，
   回转不一致再用 %.17g；像整数的浮点补 ".0"。返回长度 */
unsigned luaO_tostringbuff (const TValue *obj, char *buff);

/* 数字（或 superstruct/bigint/boolean）原地转 Lua 字符串。
   superstruct 转其 name；bigint 走 luaB_tostring */
void luaO_tostring (lua_State *L, TValue *obj);

/* 引擎内部格式化。支持 %s %c %d %I(lua_Integer) %f(lua_Number)
   %p(指针) %U(unsigned long→UTF-8) %%。出错返回 NULL（消息在栈顶）。 */
const char *luaO_pushvfstring (lua_State *L, const char *fmt, va_list argp);
const char *luaO_pushfstring (lua_State *L, const char *fmt, ...);

/* chunk 可读标识：'='源→原样；'@'源→文件名（超长前缀截断加"..."）；
   其它→ [string "..."]。输出到 out（LUA_IDSIZE） */
void luaO_chunkid (char *out, const char *source, size_t srclen);
```

调用方式要点：

- `luaO_arith` 的 `res` 是 **StkId**（栈槽），因为元方法兜底可能触发 Lua 代码执行；`luaO_rawarith` 的 `res` 是普通 `TValue*`，纯计算无副作用。
- `luaO_pushfstring` 返回 `NULL` 表示构建失败（错误对象在栈顶），调用方必须处理（`pushvfstring` 宏封装了 `va_start/va_end` 并在 NULL 时 `luaD_throw(L, LUA_ERRMEM)`）。
- 设置值时选择正确的宏族：`setobj`（通用，struct 深拷贝）、`setobjs2s/setobj2s`（目标是栈）、`setsvalue2s`（字符串入栈）等。

---

## 四、语法关联

本文件不含解析器，但决定了**数字字面量词法**能接受的写法（`llex.c` 扫描出的数字文本最终由 `luaO_str2num` 解析）：

| 写法 | 结果 | 说明 |
|---|---|---|
| `0xFF` | 255 (integer) | 十六进制 |
| `0b101` | 5 (integer) | 二进制（LXCLUA 扩展） |
| `0o17` | 15 (integer) | 八进制（LXCLUA 扩展） |
| `0x1.8p1` | 3.0 (float) | C99 十六进制浮点 |
| `0x10` 溢出 | nil | 超 `LUA_MAXINTEGER` 时整数路径拒绝，回落浮点路径 |
| `inf` / `nan` | nil | 显式拒绝 |

---

## 五、运行验证（实测输出）

脚本 `v_lobject.lua`（经 `run_lua.sh` 执行，引擎 `Lua 5.5`）：

```
hex  0xFF =	255
bin  0b101 =	5
oct  0o17 =	15
neg hex -0xA =	-10
hexflt 0x1.8p1 =	3.0
reject inf =	nil
reject nan =	nil
tostring(1.0) =	1.0
tostring(1e30) =	1e+30
math.type(0xFF) =	integer
math.type(0b101) =	integer
chunkid:	...ilo/.qwenworkcn/workspace/.../v_lobject.lua:18: boom
```

结论：`0b/0o` 前缀、十六进制浮点、inf/nan 拒绝、浮点 `.0` 补齐、chunkid 截断格式均与源码一致。另观察到：编译器对未使用局部变量发出 `warning: unused local variable` 告警（词法/语法层特性，见 compiler 篇）。

---

## 六、与其他模块的关系

- **被所有人依赖**：`lobject.h` 是 `lstate.h`、`ltable.h`、`lvm.h`、`lparser`/`lcodegen` 的前置。
- `luaO_arith` 依赖 `lvm.c` 的整数辅助（`luaV_mod/luaV_idiv/luaV_shiftl`）与 `ltm.c` 的 `luaT_trybinTM`。
- BigInt 路径转发到 `utils/lbigint.c`（`luaB_add/sub/mul/div/mod/pow/tostring/bigtonumber`）。
- struct 深拷贝由 `luaS_copystruct` 实现（定义在 `stdlib/lstruct.c`，函数名前缀沿用字符串模块约定）。
- `luaO_str2num` 的消费者：`tonumber()` 标准库、词法数字常量折叠、`lvm.c` 的算术快速路径。
