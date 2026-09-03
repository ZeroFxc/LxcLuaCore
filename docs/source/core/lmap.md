# core/lmap.c + lmap.h — Map 容器（纯哈希，独立于 table）

> 职责：实现引擎第 15 种对外类型 `map`（`LUA_TMAP`）的容器本体——
> 纯哈希存储、任意类型键、无元表、无数组段，与 `Table` 完全解耦。

---

## 一、特性介绍

1. **与 table 完全隔离**：独立内存布局（`Map`/`MapNode`）、独立哈希算法、
   独立 GC 标记；无元表（不走元方法）、无数组段、无 `flags` 缓存。
2. **任意可哈希键**：nil、布尔、整数、浮点、字符串、以及任何可回收对象
   （table/map/userdata/函数/struct 等，按对象地址哈希）。
3. **nil 是合法值**：`m[k] = nil` **不删除**键，而是存一个 nil 值
   （实测：赋值后 `#m` 不变）。删除须用 `luaM_deletekey`（Lua 侧经 `map` 库）。
4. **链地址法 + 2 的幂桶**：初始 `MAP_INITIAL_BUCKETS=8`，负载因子
   `count*4 > size*3`（>0.75）时翻倍扩容；`h & (size-1)` 定位桶。
5. **`#m` = 键值对总数**（`luaM_maplen` 直接返回 `count`，无边界歧义）。
6. **字面量语法**：`[]` 创建空 map（`OP_NEWMAP`），读写走 `OP_MAPGET/OP_MAPSET`；
   且标准表写指令 `OP_SETTABLE/SETI/SETFIELD` 在目标是 map 时直接分派到
   `luaM_setval`（`lvm.c` 实测代码），因此 `m.field = v`、`m[1] = v` 均适用。

---

## 二、关键数据结构与属性（见 lobject.h）

```c
typedef struct MapNode {
  TValue key;             /* 任意类型键 */
  TValue val;
  struct MapNode *next;   /* 桶内冲突链 */
} MapNode;

typedef struct Map {
  CommonHeader;           /* GC 头，tt == LUA_VMAP */
  MapNode **buckets;      /* 桶数组（2 的幂） */
  unsigned int size;      /* 桶数 */
  unsigned int count;     /* 键值对总数 */
  GCObject *gclist;       /* GC 灰色链 */
} Map;
```

### 键哈希规则（`luaM_hashkey`，FNV-1a 变体，基值 `2166136261`，素数 `16777619`）

| 键类型 | 哈希来源 |
|---|---|
| `nil` | 固定值 `0xdeadbeef` |
| 布尔 | `true→1 / false→0` 后 FNV |
| 整数 | 低 32 位与高 32 位分别 FNV |
| 浮点 | **先截断为整数**再哈希（`2.0` 与 `2` 哈希不同类——但 `key_equals` 要求 raw tag 相同，`2.0` 与 `2` 是不同键） |
| 短串 | 直接取 `ts->hash` |
| 长串 | `luaS_hashlongstr` |
| 可回收对象 | 对象指针地址折叠后 FNV（身份哈希） |
| 其它 | `TValue` 自身地址 |

**键相等（`key_equals`）**：先比原始标签，再比值；短串指针相等；长串内容相等；
可回收对象比地址。**类型不同一律不等**（与 `==` 的跨类型规则无关）。

---

## 三、关键函数（准确签名）

```c
unsigned int luaM_hashkey (const TValue *key);        /* 任意键哈希 */
Map *luaM_newmap (lua_State *L);                      /* 空 map（GC 对象） */
void luaM_freemap (lua_State *L, Map *m);             /* 节点链+桶数组+本体 */

const TValue *luaM_getval (const Map *m, const TValue *key);
      /* 未命中返回 NULL（不是哨兵） */
void luaM_setval (lua_State *L, Map *m, const TValue *key, const TValue *val);
      /* 已存在则更新；否则扩容检查后头插新节点；键/值均走 GC 写屏障。
         注意：val 为 nil 时照存不误，不删除 */
lua_Unsigned luaM_maplen (lua_State *L, const Map *m); /* = count */

int luaM_mapnext (const Map *m, const TValue *key,
                  TValue *next_key, TValue *next_val);
      /* key==NULL/nil 从头；否则定位当前键的下一节点（桶序，无序保证）；
         返回 1/0 */
void luaM_deletekey (lua_State *L, Map *m, const TValue *key); /* 摘链+释放节点 */
void luaM_clearmap (lua_State *L, Map *m);            /* 全清并释放桶数组 */
Map *luaM_copymap (lua_State *L, const Map *src);     /* 深拷贝（浅拷贝键值引用） */
void luaM_getkeys (lua_State *L, const Map *m);       /* 全部键依次压栈 */
void luaM_getvalues (lua_State *L, const Map *m);     /* 全部值依次压栈 */
```

调用方式要点：

- `luaM_getval` 返回 `NULL` 即未命中；与 `luaH_get` 的 `&absentkey` 哨兵风格不同。
- `luaM_mapnext` 传回上次键做定位，**遍历期间删除当前键**会使下次定位失败
  而跳到"该桶之后"（与 `next` 对 dead key 的宽容不同），遍历中删键需谨慎。
- `luaM_getkeys/getvalues` 直接推 `L->top.p`，调用方须保证栈空间。

---

## 四、语法与运行期语义（实测）

脚本 `v_ltm_map.lua`（`run_lua.sh`）：

```
M1 type:	map
M2 get:	1	hundred	table-key
M3 len #m:	3
M4 after delete #m:	3
M5 pairs count:	3
```

- `local m = []` → `type(m) == "map"`（空方括号字面量 = map，不是空表）。
- `m["alpha"]`、`m[100]`、`m[表对象]` 三种键读写正常。
- `#m` = 3；`m["alpha"] = nil` 后 `#m` **仍为 3**（nil 是值不是删除）。
- `pairs(m)` 可用（迭代器走 `luaM_mapnext`），遍历 3 对。
- 错误路径：对非 map 用 `OP_MAPSET` 会在 VM 内报类型错误（`lvm.c` 有校验）。

---

## 五、与其他模块的关系

- 创建/读写指令：`OP_NEWMAP/OP_MAPGET/OP_MAPSET`（`vm/lvm.c`）；
  表系写指令对 map 目标也有分派分支。
- Lua 侧库：`map`（`stdlib/lmaplib.c`，提供 delete/clear/copy/keys/values 等
  用户级接口，`linit.c` 注册）。
- GC：`lgc.c` 对 `LUA_VMAP` 的标记（遍历 `gclist`，键值均为根）。
- 哈希复用：字符串键直接用 `lstring.c` 的字符串哈希。
- 注意命名：本文件函数前缀 `luaM_*` 与 `lmem` 的分配宏同名空间
  （`luaM_new`/`luaM_free` 来自 lmem，`luaM_newmap` 等来自本文件），阅读时区分。
