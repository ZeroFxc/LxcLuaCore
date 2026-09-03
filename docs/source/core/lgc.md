# core/lgc.c + lgc.h — 垃圾回收（三色标记，扩展类型全覆盖 + 全局锁）

> 职责：三色标记-清扫回收器。增量（KGC_INC）与分代（KGC_GENH/KGC_GENJ）
> 两种模式；写屏障；终结器（`__gc`）；弱表清理；所有扩展类型
> （Map/Struct/SuperStruct/Namespace/Concept/BigInt/外部长串）的
> 标记与释放；**全程以 `g->lock` 串行化**（多线程共享状态改造）。

---

## 一、特性介绍

1. **两种收集模式**：增量（状态机 `GCSpause → GCSpropagate → ...atomic →
   sweep → callfin`）与分代（survival/old1/reallyold 三代链，
   `genstep`/`fullgen`）。`luaC_changemode` 切换（对应
   `collectgarbage("incremental"/"generational")`，实测均可切换）。
2. **全局锁（LXCLUA 改造）**：`luaC_step`/`luaC_fullgc` 全程持
   `g->lock`；对象创建 `luaC_newobjdt` 挂链时加锁；`traversetable`
   持表级 `l_rwlock_rdlock`。GC 与并发创建互不竞争。
3. **扩展类型的标记**（`reallymarkobject` + traverse 族）：
   - `SuperStruct`：标记 `name` + `data[]`（`nsize*2` 个 TValue）后直接置黑；
   - `Struct`：标记 `def`/`parent` + 按 `gc_offsets[]` 标记数据块内的
     可回收字段后置黑；
   - `NUMBIG`：无引用，直接置黑（`TBigInt` 为叶对象）；
   - `MAP/NAMESPACE/CONCEPT` 与标准容器一样挂灰链延后遍历
     （`traversemap/traverseNamespace/traverseConcept`）；
   - `traversetable` 带读锁遍历数组段+哈希段。
4. **扩展类型的释放**（`freeobj`）：`Map→luaM_freemap`、
   `Namespace→luaN_free`、`Concept→sizeConcept`、
   `Struct`（区分内联/外挂数据块）、`SuperStruct→luaS_freesuperstruct`、
   **外部长串**：先调 `falloc(ud, src, len+1, 0)` 释放外挂内存再释放头部、
   `TBigInt` 按 limb 数计大小释放。
5. **内存池联动（LXCLUA）**：`luaC_fullgc` 结束前调 `luaM_poolgc`
   收缩小对象池缓存（两处调用：fullgc 与 freeallobjects 路径）。
6. **写屏障**：`luaC_barrier_`（前进式：标记白孩子，分代模式下置
   `G_OLD0`）与 `luaC_barrierback_`（后退式：把黑父重新挂灰链）。
7. **终结器**：`luaC_checkfinalizer` 把带 `__gc` 的对象移入 `finobj`；
   原子阶段 `separatetobefnz`→`GCSatomic` 后逐批调用；实测顺序为
   **注册序逆序**（u2 先于 u1）。
8. **VMP 钩点**：`luaC_step` 入口 `lgc_vmp_hook_point()`。

---

## 二、关键函数（准确签名）

```c
/* 对象创建（所有可回收对象的唯一入口） */
GCObject *luaC_newobj (lua_State *L, int tt, size_t sz);
GCObject *luaC_newobjdt (lua_State *L, int tt, size_t sz, size_t offset);
      /* offset：GCObject 头不在分配块开头时（如线程的 LX 包装） */

/* 屏障（宏包装：luaC_barrier / luaC_barrierback / luaC_objbarrier /
   luaC_barrierback(L,obj) / luaC_checkfinalizer） */
void luaC_barrier_ (lua_State *L, GCObject *o, GCObject *v);
void luaC_barrierback_ (lua_State *L, GCObject *o);
void luaC_checkfinalizer (lua_State *L, GCObject *o, GCObject *mt);

/* 步进与控制 */
void luaC_step (lua_State *L);          /* 持锁；分代走 genstep 否则 incstep */
void luaC_runtilstate (lua_State *L, int statesmask);
void luaC_fullgc (lua_State *L, int isemergency);
      /* 持锁；INC→fullinc / GEN→fullgen；末尾 luaM_poolgc */
void luaC_changemode (lua_State *L, int newmode);
void luaC_fix (lua_State *L, GCObject *o);   /* 移入 fixedgc，永不回收 */
void luaC_freeallobjects (lua_State *L);     /* lua_close 用 */

/* 条件检查宏 */
luaC_checkGC(L)   /* debt 达标则 step */
luaC_condGC(L, ...) /* 条件触发 */
```

GC 状态机阶段（`gcstate`）：`GCSpause → GCSpropagate → GCSenteratomic →
GCSatomic → GCSswpallgc → GCSswpfinobj → GCStobefnz → GCScallfin → 回 pause`。
颜色位：`WHITE0BIT/WHITE1BIT/BLACKBIT` + 年龄位（分代：`G_NEW/G_SURVIVAL/
G_OLD0/G_OLD1/G_OLD/G_TOUCHED1/G_TOUCHED2`）。

---

## 三、内部流程要点

**增量单步**（`singlestep`）按当前状态分派：`pause→restartpropagate`、
`propagate→propagatemark`（弹灰链）、`enteratomic→convergeephemerons`、
`atomic→` 标记终结器/弱表清理/`clearbyvalues`、`sweep*→sweeplist`、
`callfin→udatatostring` 逐批跑 `__gc`。

**分代**：minor 只扫年轻代根（`youngcollection`），major 全量；
`genstep` 按 `genminormul/genmajormul` 阈值选择。

**`freeobj` 大小核算**：闭包按 `sizeLclosure(nupvalues)` 等精确尺寸释放；
短串释放前从驻留表摘除（`luaS_remove`）。

---

## 四、运行验证（实测输出）

脚本 `v_lgc.lua`（`run_lua.sh`）：

```
GC1 isrunning:	true
GC2 mode default:	set-ok          -- collectgarbage("incremental")
GC3 gen mode:	set-ok             -- collectgarbage("generational")
GC4 count delta:	true	true	3715.2 KB freed   -- 2 万表回收
GC5 finalizers (newest first):	u2,u1            -- __gc 逆序执行
GC6 weak value cleared:	true                      -- __mode="v" 生效
GC7 step:	true
```

结论：模式切换、全量回收、终结器顺序、弱值清理、分步回收均正常。

---

## 五、与其他模块的关系

- 对象布局来自 `lobject.h`；`traversetable` 的读锁与 `Table.lock`
  （`utils/lthread.h` 原语）配套；`g->lock` 由 `lstate.c` 初始化。
- 释放回调各模块自带：`luaH_free`/`luaM_freemap`/`luaF_freeproto`/
  `luaE_freethread`/`luaN_free`/`luaS_freesuperstruct`。
- `collectgarbage` 绑定在 `stdlib/lbaselib.c`；`GCdebt` 记账在 `lmem.c`。
- `luaM_poolgc`（内存池收缩）定义于 `lmem.c`（当前池未接分配热路径，
  收缩为空操作）。
