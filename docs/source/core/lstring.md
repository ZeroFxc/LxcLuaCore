# core/lstring.c + lstring.h — 字符串驻留表与字符串对象创建

> 职责：管理所有短字符串的全局驻留表（stringtable）、长字符串的惰性哈希、
> 字符串缓存（strcache）、userdata 分配，以及外部字符串（零拷贝）支持。

---

## 一、特性介绍

1. **短串驻留（interning）**：长度 ≤ `LUAI_MAXSHORTLEN`（默认 40 字节）的字符串全局唯一，相等性退化为指针比较（`eqshrstr`）。保留字、元方法名都必须是驻留短串（`isreserved` 依赖 `extra > 0`）。
2. **线程安全驻留**（LXCLUA 改造）：`internshrstr` 与 `luaS_resize` 全程持 `global_State` 的 `g->lock` 互斥锁——多线程共享一个 `lua_State` 场景下创建字符串不再需要外部加锁。源码注释明确依赖递归互斥（Windows CRITICAL_SECTION 天然递归）。
3. **长串惰性哈希**：长字符串创建时不算哈希（`extra == 0`），第一次用作表键时由 `luaS_hashlongstr` 计算并缓存。
4. **字符串缓存**：`luaS_new`（C 字符串入口）先查 `STRCACHE_N × STRCACHE_M` 二维缓存（按指针地址散列定位行），命中直接返回，避免重复 `strlen` + 哈希。
5. **外部字符串**（Lua 5.5 引入）：`luaS_newextlstr` 创建内容外挂的长串，对象只存 `src` 指针，可携带释放回调，用于零拷贝读文件等场景。
6. **GC 协作**：驻留查找时若命中"已死"字符串（白色但尚未回收），就地复活（`changewhite`）；`luaS_clearcache` 在 GC 后把缓存中将被回收的条目替换成 `memerrmsg`。
7. **内存错误消息预分配**：`"not enough memory"` 在 `luaS_init` 时创建并固定（`luaC_fix`），保证 OOM 时仍有消息可用。

---

## 二、关键数据结构与属性

### 2.1 常量与宏（lstring.h）

| 名称 | 值 | 说明 |
|---|---|---|
| `MEMERRMSG` | `"not enough memory"` | 预分配的 OOM 消息 |
| `LUAI_MAXSHORTLEN` | 40 | 驻留上限；不能小于最长保留字/元方法名（`__newindex` 10 字节） |
| `sizelstring(l)` | `offsetof(TString, contents) + (l+1)` | 短串对象总大小 |
| `luaS_newliteral(L, s)` | — | 字面量创建（编译期算长度） |
| `isreserved(s)` | `tt==VSHRSTR && extra>0` | 判断保留字 |
| `eqshrstr(a,b)` | `(a)==(b)` | 短串相等 = 指针相等 |

`MINSTRTABSIZE` = 128（驻留表初始桶数，2 的幂）；`MAXSTRTB` 受 `INT_MAX` 内存上限约束。
`stringtable`（定义在 `lstate.h`）：`TString **hash` + `int nuse` + `int size`。

### 2.2 哈希算法

```c
static unsigned luaS_hash(const char *str, size_t l, unsigned seed) {
  unsigned int h = seed ^ (unsigned)l;
  for (; l > 0; l--)
    h ^= ((h << 5) + (h >> 2) + (lu_byte)str[l - 1]);
  return h;
}
```

经典 Lua DJBX 变体；`seed` 是全局随机种子（抗哈希碰撞攻击，每状态一个）。

---

## 三、关键函数（准确签名）

```c
/* 长串相等：同实例 || (等长 && memcmp==0) */
int luaS_eqlngstr (TString *a, TString *b);

/* 长串哈希（惰性计算并缓存到 ts->hash，置 extra=1） */
unsigned int luaS_hashlongstr (TString *ts);

/* 驻留表改容（加锁；缩容先 rehash 再 realloc，失败回滚） */
void luaS_resize (lua_State *L, int newsize);

/* GC 后清理字符串缓存：白色条目替换为 memerrmsg */
void luaS_clearcache (global_State *g);

/* 初始化：分配 128 桶驻留表 + 预创建/固定 memerrmsg + 填充缓存 */
void luaS_init (lua_State *L);

/* 从驻留表摘除一个短串（GC sweep 阶段调用），nuse-- */
void luaS_remove (lua_State *L, TString *ts);

/* 创建/取回字符串。<=40 字节走驻留；否则直接建长串对象。
   通用入口：所有 Lua 字符串最终经此创建 */
TString *luaS_newlstr (lua_State *L, const char *str, size_t l);

/* C 字符串入口：先查字符串缓存（按指针地址 % STRCACHE_N 定位），
   未命中则 luaS_newlstr 并插入缓存行首（行内后移） */
TString *luaS_new (lua_State *L, const char *str);

/* 创建长串对象（内容未填充，由调用方写入）；hash 初始为 g->seed */
TString *luaS_createlngstrobj (lua_State *L, size_t l);

/* 创建 userdata：s 字节数据区 + nuvalue 个用户值（初始 nil），
   元表置 NULL。返回对象头，数据区用 getudatamem(u) 取 */
Udata *luaS_newudata (lua_State *L, size_t s, unsigned short nuvalue);

/* 创建外部字符串。falloc==NULL → LSTRFIX（内容永不释放，如 mmap 静态区）；
   falloc!=NULL → LSTRMEM（GC 回收时以 (ud, s, len+1, 0) 调回释放）。
   若对象分配失败且带释放回调，先释放外部内存再抛 MEM 错误 */
TString *luaS_newextlstr (lua_State *L, const char *s, size_t len,
                          lua_Alloc falloc, void *ud);

/* 外部字符串尺寸：LSTRREG 按内容算；LSTRFIX/LSTRMEM 只算头部 */
size_t luaS_sizelngstr (size_t len, int kind);

/* 归一化：外部/长串若实际 <=40 字节，转为驻留短串返回（可能换新对象） */
TString *luaS_normstr (lua_State *L, TString *ts);
```

调用方式要点：

- 创建字符串**永远**用 `luaS_newlstr`/`luaS_new`/`luaS_newliteral`，不要手工 `luaC_newobj` 字符串（会绕过驻留表导致短串重复）。
- `luaS_new` 的缓存行按 `point2uint(str) % STRCACHE_N` 定位——同一指针反复传入时命中率最高（典型：C 侧固定字面量）。
- `luaS_resize` 只允许在安全点调用（GC 阶段/启动阶段），持全局锁且可能触发 realloc。
- 外部字符串的 `falloc` 签名与 `lua_Alloc` 一致，释放时第三个参数传 `len+1`、第四个传 0。

---

## 四、内部流程

### 短串驻留（internshrstr）

```
加锁 g->lock
  h = luaS_hash(str, l, g->seed)
  桶 = strt.hash[lmod(h, size)]
  遍历桶链: 长度+memcmp 全等?
    命中 → 若已死则复活 → 解锁返回
  未命中:
    nuse >= size → growstrtab（可能 fullgc/翻倍扩容，重新取桶）
    createstrobj(LUA_VSHRSTR, h) → 拷内容 → 头插桶链 → nuse++
解锁
```

### 驻留表扩容（growstrtab）

`nuse == MAX_INT` 时先 `luaC_fullgc` 抢救，仍满则抛错；`size <= MAXSTRTB/2` 时翻倍。

---

## 五、运行验证（实测输出）

脚本 `v_lstring.lua`：

```
39==copy:	true
40==copy:	true
41==copy:	true
41~=40:	true
long key:	v60
meta:	meta-ok
gc ok, 39 still eq:	true
```

结论：39/40 字节走驻留路径、41 字节走长串路径，相等性与长串表键、元方法短串键、GC 后驻留串复活语义均正常。

---

## 六、与其他模块的关系

- 驻留表与缓存挂在 `global_State`（`lstate.h`）：`g->strt`、`g->strcache`、`g->seed`、`g->memerrmsg`、`g->lock`（锁实现见 `utils/lthread.c/h`）。
- 对象分配统一走 `lgc.c` 的 `luaC_newobj`；GC sweep 时回调 `luaS_remove`。
- `luaS_newudata` 是 `lua_newuserdatauv`（`lapi.c`）的底层。
- 外部字符串的生产者：`lauxlib.c` 的文件读取路径（整文件读入后零拷贝包装）。
- `luaS_hashlongstr` 的消费者：`ltable.c` 键哈希、`lvm.c` 长串比较前置。
