# 类型系统与结构体

> 基于 `lobject.h`(1182行) + `lua.h`(1752行) + `llimits.h`(501行) 源码精确分析

---

## 1. 类型总览

### 1.1 类型常量（lua.h:85-103）

| # | 常量 | 值 | 说明 | 可 GC |
|---|------|-----|------|-------|
| - | `LUA_TNONE` | -1 | 无效索引占位 | - |
| 0 | `LUA_TNIL` | 0 | 空值 | - |
| 1 | `LUA_TBOOLEAN` | 1 | 布尔值 | - |
| 2 | `LUA_TLIGHTUSERDATA` | 2 | 轻量用户数据 | - |
| 3 | `LUA_TNUMBER` | 3 | 数字 | - |
| 4 | `LUA_TSTRING` | 4 | 字符串 | 是 |
| 5 | `LUA_TTABLE` | 5 | 表 | 是 |
| 6 | `LUA_TFUNCTION` | 6 | 函数 | 是 |
| 7 | `LUA_TUSERDATA` | 7 | 完整用户数据 | 是 |
| 8 | `LUA_TTHREAD` | 8 | 协程 | 是 |
| 9 | `LUA_TSTRUCT` | 9 | 值类型结构体 | 是 |
| 10 | `LUA_TPOINTER` | 10 | 原始指针 | - |
| 11 | `LUA_TCONCEPT` | 11 | 概念约束 | 是 |
| 12 | `LUA_TNAMESPACE` | 12 | 命名空间 | 是 |
| 13 | `LUA_TSUPERSTRUCT` | 13 | 带继承结构体 | 是 |
| 14 | `LUA_TMAP` | 14 | 纯哈希容器 | 是 |

`LUA_NUMTYPES` = 15，另有内部类型 `LUA_TUPVAL`(15) 和 `LUA_TPROTO`(16)。

### 1.2 标记值结构（lobject.h:71-79）

```c
// TValue 是 Lua 所有值的统一表示
typedef struct TValue {
    Value value_;   // 值联合体 (8 字节)
    lu_byte tt_;    // 类型标签 (1 字节)
} TValue;

// 值联合体
typedef union Value {
    struct GCObject *gc;      // 可 GC 对象
    void *p;                  // light userdata
    void *ptr;                // raw pointer
    struct Struct *struct_;   // 结构体
    struct SuperStruct *superstruct; // 超级结构体
    struct Namespace *ns;     // 命名空间
    lua_CFunction f;          // 轻量 C 函数
    lua_Integer i;            // 整数
    lua_Number n;             // 浮点数
    lu_byte ub;               // 占位
} Value;
```

### 1.3 类型标签编码（lobject.h:36-41）

```
tt_ 字节编码:
  bit 0-3: 基础类型标签 (LUA_T* 常量)
  bit 4-5: 变体位 (区分同类型的不同子类型)
  bit 6:   可 GC 标志 (BIT_ISCOLLECTABLE = 1<<6)
```

```c
#define makevariant(t,v)  ((t) | ((v) << 4))
#define ctb(t)            ((t) | BIT_ISCOLLECTABLE)  // 标记为可 GC
```

---

## 2. nil: 4 种变体（lobject.h:189-252）

| 变体 | 位模式 | 用途 |
|------|--------|------|
| `LUA_VNIL` = `makevariant(0,0)` | 0x00 | 标准 nil |
| `LUA_VEMPTY` = `makevariant(0,1)` | 0x10 | 空槽位（表内部） |
| `LUA_VABSTKEY` = `makevariant(0,2)` | 0x20 | 不存在的键（表查找结果） |
| `LUA_VNOTABLE` = `makevariant(0,3)` | 0x30 | 快速访问非表对象的结果 |

```c
#define ttisnil(o)          checktype((o), LUA_TNIL)       // 任何 nil 变体
#define ttisstrictnil(o)    checktag((o), LUA_VNIL)        // 仅标准 nil
#define isempty(v)          ttisnil(v)                      // 任何 nil 都算空
```

---

## 3. boolean: 2 种变体（lobject.h:256-278）

```c
#define LUA_VFALSE  makevariant(LUA_TBOOLEAN, 0)  // 0x10
#define LUA_VTRUE   makevariant(LUA_TBOOLEAN, 1)  // 0x11
#define l_isfalse(o)    (ttisfalse(o) || ttisnil(o))  // 条件判断中的"假"
```

---

## 4. number: 3 种内部表示（lobject.h:344-410）

| 子类型 | 标签 | 存储 | 范围 |
|--------|------|------|------|
| 整数 | `LUA_VNUMINT` = 0x30 | `val_.i` (int64) | -2^63 ~ 2^63-1 |
| 浮点 | `LUA_VNUMFLT` = 0x31 | `val_.n` (double) | IEEE 754 |
| 大整数 | `LUA_VNUMBIG` = 99 | `val_.gc` (TBigInt*) | 任意精度 |

```c
// LUA_VNUMBIG = 99 = 3 | (2<<4) | 64 = 0b01100011
// 3 = 基类型, 2<<4 = 变体 2, 64 = BIT_ISCOLLECTABLE
```

### 4.1 TBigInt 结构（lobject.h:399-408）

```c
typedef struct TBigInt {
    CommonHeader;
    unsigned int len;       // 32-bit limbs 数量
    int sign;               // 1 或 -1
    l_uint32 buff[1];       // 小端序，基=2^32
} TBigInt;
```

---

## 5. string: 3 种形态（lobject.h:412-503）

| 形态 | 条件 | 标签 |
|------|------|------|
| 短字符串 | 长度 < 40，内联化，去重 | `LUA_VSHRSTR` = 0x40 |
| 长字符串 | 长度 >= 40 | `LUA_VLNGSTR` = 0x41 |
| 外部字符串 | 引用外部内存 | `LUA_VLNGSTR` + shrlen != -1 |

### 5.1 TString 结构（lobject.h:450-460）

```c
typedef struct TString {
    CommonHeader;
    lu_byte extra;          // 短:保留字标记; 长:"has hash"
    lu_byte shrlen;         // 短:长度; 长:0xFF
    unsigned int hash;      // 哈希值
    union { size_t lnglen; struct TString *hnext; } u;
    char contents[1];       // 字符串数据
} TString;
```

### 5.2 外部字符串（lobject.h:465-477）

```c
typedef struct TExternalString {
    // ... 同 TString 前缀字段 ...
    const char *src;        // 指向外部数据
    lua_Alloc falloc;       // 释放函数
    void *ud;               // 释放函数用户数据
} TExternalString;
```

### 5.3 长字符串 shrlen 分类

| 值 | 常量 | 说明 |
|----|------|------|
| -1 | `LSTRREG` | 常规长字符串 |
| -2 | `LSTRFIX` | 固定外部字符串 |
| -3 | `LSTRMEM` | 可释放外部字符串 |

### 5.4 限制

```c
#define LUAI_MAXSHORTLEN  40  // 短字符串最大长度 (llimits.h)
```

---

## 6. function: 3 种闭包（lobject.h:770-871）

| 子类型 | 标签 | 说明 |
|--------|------|------|
| Lua 闭包 | `LUA_VLCL` = 0x60 | 完整 Lua 函数 |
| 轻量 C 函数 | `LUA_VLCF` = 0x61 | 纯 C 函数指针 |
| C 闭包 | `LUA_VCCL` = 0x62 | C 函数 + upvalues |

### 6.1 LClosure（lobject.h:852-856）

```c
typedef struct LClosure {
    ClosureHeader;          // CommonHeader + nupvalues + ishotfixed + gclist
    struct Proto *p;        // 函数原型
    UpVal *upvals[1];       // upvalue 列表
} LClosure;
```

### 6.2 Proto 函数原型（lobject.h:732-765）

```c
typedef struct Proto {
    CommonHeader;
    Instruction *code;          // 字节码（64位指令）
    TValue *k;                  // 常量表
    struct Proto **p;           // 嵌套函数
    unsigned short numparams;   // 固定参数数
    lu_byte flag;               // PF_VAHID|PF_VATAB|PF_FIXED|PF_LOCKED|PF_ASYNC
    lu_byte is_vararg;
    unsigned short maxstacksize; // 最大寄存器数
    uint64_t bytecode_hash;     // 防篡改哈希
    // ... 混淆字段 (difierline_mode/pad/magicnum/data) ...
    int is_sleeping;            // 休眠状态
    CallQueue *call_queue;      // 调用队列
    struct VMCodeTable *vm_code_table; // VM 保护
    // ... 调试信息 ...
} Proto;
```

Proto 标志位：
```c
#define PF_VAHID   1   // 隐藏可变参数
#define PF_VATAB   2   // 可变参数表
#define PF_FIXED   4   // 固定内存
#define PF_LOCKED  8   // 锁定只读字节码
#define PF_ASYNC   16  // 异步函数
```

---

## 7. table（lobject.h:1089-1103）

```c
typedef struct Table {
    CommonHeader;
    lu_byte flags;              // 元方法缓存
    lu_byte lsizenode;          // 哈希大小 (log2)
    unsigned int alimit;        // 数组边界
    TValue *array;              // 数组段
    Node *node;                 // 哈希段
    Node *lastfree;             // 最后空闲节点
    struct GCObject *metatable; // 元表
    GCObject *gclist;
    lu_byte type;               // 自定义类型标志
    lu_byte is_shared;          // 线程安全锁启用
    l_rwlock_t lock;            // 读写锁
    struct Namespace *using_next; // 使用的命名空间
} Table;
```

### 7.1 哈希节点（lobject.h:1048-1056）

```c
typedef union Node {
    struct NodeKey {
        TValuefields;       // 值
        lu_byte key_tt;     // 键类型
        int next;           // 链表
        Value key_val;      // 键值
    } u;
    TValue i_val;           // 直接作为 TValue 访问
} Node;
```

---

## 8. struct: 值类型结构体（lobject.h:521-543）

```c
typedef struct Struct {
    CommonHeader;
    GCObject *gclist;
    struct Table *def;      // 结构体定义
    int *gc_offsets;        // GC 偏移
    int n_gc_offsets;
    size_t data_size;       // 数据大小
    struct GCObject *parent; // 视图父对象
    lu_byte *data;          // 数据指针
    union { LUAI_MAXALIGN; lu_byte d[1]; } inline_data; // 内联数据
} Struct;
```

赋值时自动深拷贝：`setobj` 宏检测 `ttisstruct` 后调用 `luaS_copystruct`。

---

## 9. Map: 纯哈希容器（lobject.h:965-1017）

```c
typedef struct MapNode {
    TValue key;             // 键（任意类型）
    TValue val;             // 值
    struct MapNode *next;   // 链表解决冲突
} MapNode;

typedef struct Map {
    CommonHeader;
    MapNode **buckets;      // 哈希桶
    unsigned int size;      // 桶数（2 的幂）
    unsigned int count;     // 键值对数
    GCObject *gclist;
} Map;
```

与 Table 差异：无数组段、无元表、无 flags、初始桶 `MAP_INITIAL_BUCKETS=8`。

---

## 10. namespace / concept / superstruct / pointer

### 10.1 Namespace（lobject.h:920-926）

```c
typedef struct Namespace {
    CommonHeader;
    struct Table *data;             // 命名空间数据表
    TString *name;
    GCObject *gclist;
    struct Namespace *using_next;   // 使用的命名空间链表
} Namespace;
```

### 10.2 Concept（lobject.h:893-898）

```c
typedef struct Concept {
    ClosureHeader;          // 复用函数闭包头
    struct Proto *p;
    UpVal *upvals[1];
} Concept;
```

### 10.3 SuperStruct（lobject.h:949-956）

```c
typedef struct SuperStruct {
    CommonHeader;
    GCObject *gclist;
    TString *name;
    unsigned int nsize;
    unsigned int ncapacity;
    TValue *data;
} SuperStruct;
```

### 10.4 Pointer（lobject.h:559-576）

```c
#define LUA_VPOINTER  makevariant(LUA_TPOINTER, 0)  // 0xA0
// 存储: val_.ptr，非 GC 对象
```

---

## 11. GC 三色标记清除

### 11.1 可 GC 对象公共头

```c
#define CommonHeader  struct GCObject *next; lu_byte tt; lu_byte marked
```

`marked` 字段编码颜色（白/灰/黑），`next` 链入 GC 链表。

### 11.2 GC 模式

```lua
-- 增量模式（默认）
collectgarbage("incremental", pause, stepmul, stepsize)

-- 分代模式
collectgarbage("generational", minormul, majormul)
```

### 11.3 GC 参数（lua.h:1112-1123）

```c
// 分代: LUA_GCPMINORMUL(0), LUA_GCPMAJORMINOR(1), LUA_GCPMINORMAJOR(2)
// 增量: LUA_GCPPAUSE(3), LUA_GCPSTEPMUL(4), LUA_GCPSTEPSIZE(5)
```

---

## 12. 指令格式

```c
typedef l_uint64 Instruction;  // 64位指令 (llimits.h:264)

// 6 种格式: iABC, ivABC, iABx, iAsBx, iAx, isJ
// 操作码位: 54-63 (10位, 最多 1024 个操作码)
// 寄存器 A: 0-14 (15位, 最多 32768 个寄存器)
```

---

## 13. 类型转换

```lua
tostring(v)      -- 任意值→字符串
tonumber(s)      -- 字符串→数字
math.tointeger(x) -- 数字→整数
type(v)          -- 返回类型名 ("number", "string", "table", ...)
```

---

## 14. 版本信息

```c
#define LUA_VERSION_MAJOR       "5"
#define LUA_VERSION_MINOR       "5"
#define LUA_VERSION_RELEASE     "0"
#define LUA_VERSION_NUM         505
#define LUA_VERSION_RELEASE_NUM 50508  // 505 * 100 + 8
#define LUA_SIGNATURE           "\x1bXCF"
#define LUA_COPYRIGHT           "Lua 5.5.0  Copyright (C) 2026-2099 XCLUA"
#define LUA_AUTHORS             "DifierLine"
```