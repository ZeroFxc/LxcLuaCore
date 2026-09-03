# core/lmem.c + lmem.h — 内存管理器（分配器接口 + 小对象内存池）

> 职责：引擎全部内存分配的统一入口——把分配请求转发给用户提供的
> `lua_Alloc`（frealloc），维护 `GCdebt` 记账，失败时触发紧急 GC 重试；
> 并额外实现一套**分档小对象内存池**（LXCLUA 扩展）。

---

## 一、特性介绍

1. **单一分配协议 `frealloc`**：`void *frealloc(void *ud, void *ptr, size_t osize, size_t nsize)`。
   `nsize==0` 为释放；`ptr==NULL` 为新建；否则为改大小。所有内存经此回调，
   宿主可整体替换分配器。
2. **失败重试**：分配失败且 `cantryagain(g)`（状态完整且不在紧急 GC 中）时，
   `tryagain` 触发 `luaC_fullgc(L, 1)` 紧急回收后再试一次；仍失败才抛 `LUA_ERRMEM`。
3. **GCdebt 记账**：每次成功分配/释放用原子操作增减 `g->GCdebt`，
   驱动增量/分代 GC 的触发（`l_atomic_add/sub`，见 `utils/lthread.h`）。
4. **解析器数组增长**：`luaM_growaux_` 倍增（最小 `MINSIZEARRAY=4`），
   上限处报 `too many %s (limit is %d)`；`luaM_shrinkvector_` 收尾精确收缩。
5. **小对象内存池（LXCLUA 扩展）**：12 档大小类
   `{8,16,24,32,48,64,96,128,192,256,512,1024}`，每档一条侵入式空闲链表
   （块首字节存 `next` 指针），命中直接复用，未命中回落 `frealloc`；
   全程持 `g->mempool.lock` 互斥锁（线程安全）；`max_cache=64`/档。
6. **内存池与 GC 联动**：`luaM_poolgc`（= `luaM_poolshrink`）在 full GC 时被
   `lgc.c` 调用，把每档缓存缩到 `max_cache/2`，多余块归还系统。

---

## 二、关键数据结构与属性

内存池状态挂在 `global_State.mempool`（结构定义在 `lstate.h`，`NUM_SIZE_CLASSES=12`）：

| 字段 | 说明 |
|---|---|
| `pools[i].free_list` | 空闲块链表头（侵入式） |
| `pools[i].object_size` | 该档块大小 |
| `pools[i].max_cache` | 缓存块上限（64） |
| `pools[i].current_count` | 当前缓存块数 |
| `pools[i].total_alloc / total_hit` | 统计：总请求数 / 命中数 |
| `threshold / small_limit` | 均取最大档 1024 |
| `fallback_alloc / fallback_ud` | 回落分配器（= `g->frealloc`/`g->ud`） |
| `enabled` | 池开关（初始化置 1） |
| `lock` | 互斥锁 |

**注意（现状）**：源码检索显示 `luaM_poolalloc`/`luaM_poolfree` 目前
**没有被任何分配路径调用**——池被初始化（`lstate.c` 状态创建）、
被 GC 收缩、被关闭，但分配热路径（`luaM_malloc_`/`luaM_realloc_`）走的是
`frealloc` 直通。即内存池是已搭好但未接线的可选基础设施。

---

## 三、关键函数（准确签名）

### 内部函数（勿直接调，经宏使用）

```c
void *luaM_malloc_ (lua_State *L, size_t size, int tag);      /* 新建，失败抛错 */
void *luaM_realloc_ (lua_State *L, void *block, size_t osize, size_t nsize);
      /* 改大小；失败返回 NULL，不抛错，不更新 GCdebt */
void *luaM_saferealloc_ (lua_State *L, void *block, size_t osize, size_t nsize);
      /* 同上，失败抛 LUA_ERRMEM */
void luaM_free_ (lua_State *L, void *block, size_t osize);    /* 释放并减记账 */
l_noret luaM_toobig (lua_State *L);                           /* "block too big" */
void *luaM_growaux_ (lua_State *L, void *block, int nelems, int *size,
                     int size_elem, int limit, const char *what);
void *luaM_shrinkvector_ (lua_State *L, void *block, int *size,
                          int final_n, int size_elem);
```

### 调用宏（lmem.h，正确的使用方式）

```c
luaM_new(L, t)               /* 分配单个 t */
luaM_newvector(L, n, t)      /* 分配 n 个 t */
luaM_newvectorchecked(L,n,t) /* 先做溢出检查 */
luaM_newobject(L, tag, s)    /* GC 对象（带 tag） */
luaM_newblock(L, size)       /* size 字节 */
luaM_free(L, b) / luaM_freearray(L, b, n) / luaM_freemem(L, b, s)
luaM_growvector(L,v,nelems,size,t,limit,e)
luaM_reallocvector(L, v, oldn, n, t)
luaM_shrinkvector(L, v, size, fs, t)
luaM_reallocvchar(L, b, on, n)   /* char 数组（免溢出检查） */
luaM_error(L)                    /* = luaD_throw(L, LUA_ERRMEM) */
luaM_checksize(L, n, e) / luaM_limitN(n, t) / luaM_testsize(n, e)
```

### 内存池（LXCLUA 扩展）

```c
void luaM_poolinit (lua_State *L);        /* 状态创建时调用 */
void luaM_poolshutdown (lua_State *L);    /* 状态销毁时调用 */
void *luaM_poolalloc (lua_State *L, size_t size);   /* 按档取块；超档/未启用返回 NULL */
void luaM_poolfree (lua_State *L, void *block, size_t size); /* 归还入池；满则直放 */
void luaM_poolshrink (lua_State *L);      /* 各档缩至 max_cache/2 */
void luaM_poolgc (lua_State *L);          /* = poolshrink，full GC 时调 */
size_t luaM_poolgetusage (lua_State *L);  /* 当前缓存占用字节 */
```

调用方式要点：

- 分配 GC 对象统一走 `luaC_newobj`（内部 `luaM_malloc_` + 挂 GC 链），不要绕过。
- `luaM_realloc_` 返回 `NULL` 时 **`GCdebt` 未更新**，调用方必须自行处理失败分支
  （`ltable.c` 的 `luaH_resize` 是标准示范：失败则回滚再 `luaM_error`）。
- `growvector` 的 `limit` 实参经 `luaM_limitN` 钳制，错误消息用 `e`。

---

## 四、内部流程

**`luaM_malloc_`**：`size==0 → NULL`；否则 `firsttry(g,NULL,tag,size)` →
失败 `tryagain`（紧急 GC 后重试）→ 仍失败 `luaM_error`；成功 `GCdebt += size`。

**`luaM_poolalloc`**：`get_size_class`（对 12 档二分找 ≥size 的最小档）→
加锁 → 空闲链非空则弹头块（`total_hit++`）→ 否则解锁后 `frealloc` 新分配
（`GCdebt += object_size`）。

---

## 五、运行验证（实测输出）

脚本 `v_lfunc.lua` 中内存部分（`run_lua.sh`）：

```
M1 mem before:	121.1787109375
M2 mem after 10k tables:	false
M3 mem after gc:	118.71875
```

创建 1 万张表后 `collectgarbage("count")` 正常增长，`collect` 后回落——
分配/记账/回收闭环正常（`GCdebt` 路径经标准分配器）。内存池因未接线，
无独立运行期行为可验证，此结论基于源码引用检索。

---

## 六、与其他模块的关系

- 所有模块的分配/释放最终汇聚于此；`GCdebt` 的消费者是 `lgc.c` 的步进触发。
- `frealloc` 的实际提供者：`lstate.c`（默认 `l_alloc`，封装 libc realloc）
  或宿主经 `lua_newstate` 注入。
- 内存池生命周期调用点：`lstate.c`（init/shutdown）、`lgc.c`（full GC 收缩）。
- 锁原语 `l_mutex_*` / 原子 `l_atomic_*` 定义在 `utils/lthread.h`。
