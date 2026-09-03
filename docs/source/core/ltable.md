# core/ltable.c + ltable.h — 表（数组+哈希）实现与表访问日志

> 职责：Lua 表的双段存储（数组段 + 哈希段）完整实现：查找/插入/删除/遍历/
> 长度/扩容重哈希；并内建 LXCLUA 独有的**表访问拦截日志子系统**（GET/SET 插桩、
> 过滤、去重），经 `utils/logtable.c` 暴露为 `logtable` 库。

---

## 一、特性介绍

1. **双段结构**（标准设计）：非负整数键尽量放数组段（紧凑、快）；其余进哈希段。
   数组段大小取"超过一半槽位被使用"的最大 2 的幂区间；哈希段用链地址散列 + Brent 变体，
   核心不变式：若元素不在主位置，则与它冲突的元素必在自己的主位置——负载 100% 时性能仍好。
2. **键归一化**：整数值浮点键（如 `2.0`）插入/查找时归一为整数键；`nil` 键报
   `table index is nil`；`NaN` 键报 `table index is NaN`。
3. **`alimit` 提示机制**：`Table.alimit` 不一定是数组真实大小（见 `BITRAS` 标志位），
   `luaH_getn`（`#`）用二分找边界并把结果回写为提示，加速后续 `#`。
4. **数组段倒置布局**（Lua 5.5 新设计，`ltable.h`）：值数组与标签数组相向排布，
   `t->array` 指针指向中间，值用负下标（`getArrVal`）、标签用非负下标（`getArrTag`），
   中间的 `unsigned` 是 `#` 提示位（`lenhint`）——省掉表头字段、消除对齐填充。
5. **LXCLUA 扩展：表访问日志**：`luaH_get/luaH_set/luaH_setint` 内插桩，
   启用后把每次键值访问按 `[时间] [GET/SET] [GENERAL] KEY:类型:值 -> VALUE:类型(值)`
   写入日志文件；配套完整过滤器（键/值/操作/类型 的包含/排除模式、整数范围、去重、
   智能模式、JNIEnv/userdata 过滤）。默认路径 `/sdcard/XCLUA/hackv/table_access_<时间戳>.log`
   （Android 遗留；Windows 上解析为当前盘符根下同名路径）。
6. **LXCLUA 扩展字段初始化**：`luaH_new` 初始化 `is_shared=0`、`l_rwlock_init(&t->lock)`、
   `using_next=NULL`（线程安全与命名空间支持，见 `lobject.h` Table 字段）。
7. **`luaH_get_optimized`**：与 `luaH_get` 相同分派但不走日志插桩（快速路径）。

---

## 二、关键数据结构与属性

### 2.1 哈希段节点与辅助宏（ltable.h）

| 宏 | 含义 |
|---|---|
| `gnode(t,i)` | 第 i 个节点 `&(t)->node[i]` |
| `gval(n)` | 节点值（`i_val` 联合体成员，直接当 TValue） |
| `gnext(n)` | 冲突链偏移（相对指针差，非绝对指针） |
| `nodefromval(v)` | 由值地址反推节点（Node 首成员即值） |
| `isdummy(t)` | `t->lastfree == NULL`，即哈希段是共享哑节点 |
| `allocsizenode(t)` | 实际分配的节点数（哑表为 0） |
| `invalidateTMcache(t)` | 清 `flags` 元方法缓存位 |

`dummynode_`：所有空哈希表共享的单节点（值空 + 键 `LUA_TDEADKEY`），避免索引时判空分支。
`absentkey`（`ABSTKEYCONSTANT`）：查找未命中的统一返回哨兵。

### 2.2 数组段倒置布局（ltable.h，Lua 5.5）

```
             Values                              Tags
  --------------------------------------------------------
  ...  |   Value 1     |   Value 0     |unsigned|0|1|...
  --------------------------------------------------------
                                       ^ t->array
```

| 宏 | 定义/含义 |
|---|---|
| `getArrTag(t,k)` | `cast(lu_byte*, (t)->array) + sizeof(unsigned) + (k)` |
| `getArrVal(t,k)` | `(t)->array - 1 - (k)`（负下标方向） |
| `lenhint(t)` | `cast(unsigned*, (t)->array)`，夹在两数组之间的 `#` 提示 |
| `arr2obj/obj2arr` | 数组槽 ↔ TValue 整体搬运 |
| `farr2val/fval2arr` | 带预取标签的快速搬运 |

### 2.3 扩容上限常量

`MAXABITS` = `int` 位宽-1（数组段最大 2^MAXABITS）；`MAXASIZE` 再受
`MAX_SIZET/(sizeof(Value)+1)` 约束；哈希段 `MAXHBITS = MAXABITS-1`、
`MAXHSIZE = luaM_limitN(1<<MAXHBITS, Node)`。

### 2.4 过滤器状态（LXCLUA 扩展，静态全局）

```c
typedef struct {
  FilterPatternList include_keys, exclude_keys;
  FilterPatternList include_values, exclude_values;
  FilterPatternList include_ops, exclude_ops;
  FilterPatternList include_key_types, exclude_key_types;
  FilterPatternList include_value_types, exclude_value_types;
  int key_min_int, key_max_int, value_min_int, value_max_int;
  int range_enabled, dedup_enabled, show_only_unique;
} TableAccessFilter;   // 每类模式表最多 32 条，每条 ≤256 字节
```

去重缓存 `g_dedup_entries[1024][512]`；开关：`g_filter_enabled`、
`g_intelligent_mode_enabled`（忽略 INTEGER/BOOLEAN/NIL 键、NIL 值）、
`g_filter_jnienv_enabled`（忽略 `_JNIEnv` 键）、`g_filter_userdata_enabled`（忽略 USERDATA 值）。

---

## 三、内部流程

### 3.1 查找分派（`luaH_get`）

```
按 ttypetag(key) 分派:
  短串 → luaH_getshortstr（桶链 + 指针相等）
  整数 → luaH_getint（先试数组段 [1,alimit]；alimit 非真实大小时
          用位掩码再探一次并顺带更新 alimit 提示；否则走哈希链）
  nil  → absentkey
  浮点 → 能无损转整数则按整数查，否则通用链
  其它 → getgeneric（桶链 + equalkey）
[扩展] 启用日志时记录 "GET"
```

`equalkey`：变体标签不同直接不等（两个例外：外部长串可与短串键相等，走
`luaS_eqlngstr`；`deadok` 时 dead key 按 GC 对象同一性比较，供 `next` 使用）。

### 3.2 插入（`luaH_newkey`）

```
nil 键 → 报错；浮点键 → 归一整数或报 NaN；nil 值 → 直接返回（=删除语义）
主位置空 → 直接落座
主位置占用:
  无空位 → rehash 扩容后递归 luaH_set
  占用者不在自己主位置 → 把占用者挪到空位，新键占主位置（链重接）
  占用者在自己主位置 → 新键去空位，挂到主位置链上
写屏障: luaC_barrierback(t, key)
```

`luaH_set` = `luaH_get` 找槽 + `luaH_finishset`（槽是 `absentkey` 则 `luaH_newkey`，
否则原地 `setobj2t`）。

### 3.3 重哈希（`rehash`）

统计数组段各 2 的幂区间键数（`numusearray`）+ 哈希段整数键（`numusehash`）+
触发本次插入的新键 → `computesizes` 求"过半填充"的最优数组尺寸（`arrayXhash`：
一个哈希节点 ≈ 3 倍数组槽内存）→ `luaH_resize` 重建。`luaH_resize` 用交换哈希段
技巧避免缩容丢元素，分配失败则回滚原状再抛错。

### 3.4 长度（`luaH_getn`）

三段逻辑：数组尾空 → 向前二分（`binsearch`）；`alimit` 非真实大小 → 检查
`alimit+1` 空否，再向真实大小二分；数组满 → 查 `limit+1` 是否在哈希段，
在则 `hash_search`（指数倍增 + 二分，用状态随机种子防构造攻击）。
找到的边界若合法会写回 `alimit` 作提示（`newhint` 存入 `lenhint` 槽）。

### 3.5 遍历（`luaH_next`）

`findindex` 把上次键映射为线性序号（0 起于第一次；数组段 1..asize；哈希段
asize+1 起；键不在表中报 `invalid key to 'next'`，允许遍历中置空删除）。

---

## 四、关键函数（准确签名）

```c
/* 生命周期 */
Table *luaH_new (lua_State *L);                 /* 新表：元表NULL/flags全缓存位/空段/初始化锁 */
void luaH_free (lua_State *L, Table *t);        /* 释放哈希段+数组段+锁+表头 */

/* 查找（返回 &absentkey 表示未命中） */
const TValue *luaH_get (Table *t, const TValue *key);       /* 通用，带日志插桩 */
const TValue *luaH_get_optimized (Table *t, const TValue *key); /* 同语义，无插桩 */
const TValue *luaH_getint (Table *t, lua_Integer key);
const TValue *luaH_getshortstr (Table *t, TString *key);
const TValue *luaH_getstr (Table *t, TString *key);

/* 写入 */
void luaH_set (lua_State *L, Table *t, const TValue *key, TValue *value);
void luaH_setint (lua_State *L, Table *t, lua_Integer key, TValue *value);
void luaH_finishset (lua_State *L, Table *t, const TValue *key,
                     const TValue *slot, TValue *value);

/* 尺寸/遍历 */
void luaH_resize (lua_State *L, Table *t, unsigned int nasize, unsigned int nhsize);
void luaH_resizearray (lua_State *L, Table *t, unsigned int nasize);
int luaH_next (lua_State *L, Table *t, StkId key);
lua_Unsigned luaH_getn (Table *t);
unsigned int luaH_realasize (const Table *t);

/* 访问日志（LXCLUA 扩展；Lua 侧封装在 require("logtable")） */
int luaH_enable_access_log (lua_State *L, int enable);  /* 失败返回 0（路径不可写） */
const char *luaH_get_log_path (lua_State *L);
void luaH_set_access_filter_enabled (int enabled);
void luaH_clear_access_filters (void);
int luaH_add_include_key_filter (const char *pattern);   /* exclude/value/op/key_type/
                                                            value_type 同构各一对 */
void luaH_set_key_int_range (int min_val, int max_val);
void luaH_set_value_int_range (int min_val, int max_val);
void luaH_set_dedup_enabled (int enabled);
void luaH_set_show_unique_only (int enabled);
void luaH_reset_dedup_cache (void);
void luaH_set_intelligent_mode (int enabled);
int luaH_is_intelligent_mode_enabled (void);
void luaH_set_filter_jnienv (int enabled);
void luaH_set_filter_userdata (int enabled);
```

调用方式要点：

- `luaH_get*` 返回的指针**可能被后续插入失效**（rehash 搬移），写操作必须走
  `luaH_set`/`luaH_finishset` 流程，不要缓存返回槽位跨插入使用。
- 快速路径宏 `luaH_fastgeti`/`luaH_fastseti`（ltable.h）引用 `Table.asize` 字段，
  但当前 `Table` 只有 `alimit`——**这两个宏是死代码，勿用（用了编译不过）**；
  `vm/lvm.c` 也未使用它们。同理 `Limbox/getlastfree/sizehash` 为上游 5.5 遗留定义，
  本分支实际用 `Table.lastfree` 结构字段（`setnodevector` 置为末后一格，`getfreepos`
  自尾向前扫描）。

---

## 五、语法关联

表构造器 `{...}`、索引 `t[k]`、长度 `#t`、`pairs`/`next`、`t[k] = nil` 删除——
本文件是它们的运行期落点。语义要点（均实测）：`{1,2,3,nil,5}` 的 `#` 可以是 5
（边界不唯一时二分结果依赖布局）；浮点整数键与整数键同槽。

---

## 六、运行验证（实测输出）

脚本 `v_ltable.lua`（`run_lua.sh` 执行）：

```
A1 len:	5
A2 t[2]:	float-normalized	float-normalized
A3 nil key:	false	nil
A4 NaN key:	false	NaN
A5 keys:	1,2,3,4,5,name
A6 after delete count:	5
A7 rehash ok:	100
A8 hole len:	5	(边界二分结果)
B1 default path:	nil
B2 onlog result:	true
B3 path now:	/sdcard/XCLUA/hackv/table_access_20260826_194510.log
```

日志文件实际内容（节选，证实插桩生效）：

```
========== TABLE ACCESS LOG ENABLED ==========
[2026-08-26 19:45:10] [SET] [GENERAL] KEY:STRING:username -> VALUE:STRING(nirithy)
[2026-08-26 19:45:10] [SET] [GENERAL] KEY:INTEGER:42 -> VALUE:STRING(answer)
[2026-08-26 19:45:10] [GET] [GENERAL] KEY:STRING:notexist -> NOT_FOUND
========== TABLE ACCESS LOG DISABLED ==========
```

结论：键归一化、nil/NaN 键报错、数组/哈希遍历顺序、删除后遍历、rehash、
`#` 边界、访问日志端到端（启用→插桩→落盘→关闭）全部符合源码。
注意：默认日志路径是 Android 路径（`/sdcard/...`），Windows 下落在当前盘符根，
需目录存在，否则 `onlog(true)` 返回失败（本次验证前手工创建了 `E:\sdcard\XCLUA\hackv`）。
另：`logtable.setlogpath` 目前是**桩**（返回 false + 提示），路径不可在运行期更改；
注释声称的 `LOGTABLE_PATH` 环境变量在代码中并未实现。

---

## 七、与其他模块的关系

- 表对象字段定义在 `core/lobject.h`；GC 标记/写屏障 `luaC_barrierback` 来自 `lgc.c`。
- 元方法回退（`__index`/`__newindex`/`__len`）在 `vm/lvm.c` 完成，本文件只做裸表操作；
  `flags` 缓存位与 `ltm.h` 的 `maskflags`/`cast_byte` 协作。
- 长串键哈希经 `lstring.c` 的 `luaS_hashlongstr`；外部长串键相等经 `luaS_eqlngstr`。
- 访问日志的 Lua 封装：`utils/logtable.c`（`require("logtable")`，`linit.c` 默认注册）。
- `Table.using_next`/`is_shared`/`lock` 的消费者：命名空间解析（`utils/lnamespace.c`）
  与线程安全共享表逻辑。
