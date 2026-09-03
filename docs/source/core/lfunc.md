# core/lfunc.c + lfunc.h — 函数原型、闭包与 Upvalue 管理

> 职责：创建/释放函数原型（Proto）、C/Lua 闭包、Concept 对象；管理 upvalue
> 的开（指向栈）/闭（值搬入自身）生命周期；实现 to-be-closed 变量关闭、
> 调用队列（sleep/wake）、热替换（hotfix）与字节码哈希。

---

## 一、特性介绍

1. **三类函数对象创建**：`CClosure`（C 闭包）、`LClosure`（Lua 闭包）、
   **`Concept`（概念对象，LXCLUA 扩展类型 `LUA_TCONCEPT`，布局同 Lua 闭包）**。
2. **upvalue 共享语义**：`luaF_findupval` 在 `L->openupval` 有序链上按栈层级查找，
   同一栈槽只产生一个 `UpVal`——多个闭包捕获同一局部变量时共享同一对象
   （`debug.upvalueid` 相等，实测验证）。
3. **开/闭状态**：开 = `uv->v.p` 指向栈槽（`upisopen`）；闭 = 指向自身 `u.value`。
   `luaF_closeupval` 把值从栈搬入 UpVal 并置黑（闭的 upvalue 不能是灰色）。
4. **to-be-closed**：`<close>` 变量经 `luaF_newtbcupval` 挂入 `tbclist`（delta 链，
   `MAXDELTA=65535` 超限插哑节点）；作用域结束或错误时 `luaF_close` 调 `__close`，
   错误对象作为第二参传入；`false`/`nil` 值跳过；无 `__close` 且非函数值报
   `variable '%s' got a non-closable value`。
5. **热替换（LXCLUA 扩展）**：`luaF_hotreplace` 运行期替换闭包的 `Proto`，
   保留 upvalues，置 `ishotfixed` 标志——热更新/补丁基础设施（被 `lpatchlib.c` 使用）。
6. **调用队列（LXCLUA 扩展）**：`CallQueue`/`CallNode` 为函数 sleep/wake 机制
   暂存调用参数（每节点 `MAX_CALL_ARGS=64` 个 TValue），配合 `Proto.is_sleeping`。
7. **字节码哈希（LXCLUA 扩展）**：`luaF_hashcode` 用 FNV-1a（64位，初始
   `0xCBF29CE484222325`，乘数 `0x100000001B3`）对 `code[]` 逐指令计算，
   供防篡改校验（`Proto.bytecode_hash`）。
8. **`luaF_protosize`**：统计原型占用内存；`PF_FIXED` 标志的固定内存部分
   （code/lineinfo/abslineinfo）不计入。

---

## 二、关键数据结构与属性

| 项 | 说明 |
|---|---|
| `sizeCclosure(n)` | `offsetof(CClosure, upvalue) + sizeof(TValue)*n` |
| `sizeLclosure(n)` | `offsetof(LClosure, upvals) + sizeof(UpVal*)*n` |
| `sizeConcept(n)` | 同 `sizeLclosure` 布局（Concept 与 LClosure 同构） |
| `MAXUPVAL` | 255（闭包 upvalue 上限，受寄存器宽度约束） |
| `upisopen(up)` | `up->v.p != &up->u.value` |
| `uplevel(up)` | 开 upvalue 的栈地址（断言必须为开） |
| `isintwups(L)` | `L->twups != L`，线程在"有开 upvalue 线程"链表中 |
| `CLOSEKTOP` | `LUA_ERRERR+1`，特殊关闭状态（保持栈顶） |
| `MAXMISS` | 10，原型闭包缓存的最大未命中次数 |

`CallNode`：`{ int nargs; TValue args[64]; CallNode *next; }`；
`CallQueue`：`{ CallNode *head, *tail; int size; }`。

---

## 三、关键函数（准确签名）

```c
/* 创建（GC 对象，经 luaC_newobj） */
Proto   *luaF_newproto (lua_State *L);      /* 全字段清零，含 difierline_*/sleep/vm_code_table */
CClosure *luaF_newCclosure (lua_State *L, int nupvals);
LClosure *luaF_newLclosure (lua_State *L, int nupvals);  /* upvals[] 置 NULL */
Concept  *luaF_newconcept (lua_State *L, int nupvals);   /* LXCLUA 扩展 */

/* upvalue */
void luaF_initupvals (lua_State *L, LClosure *cl);   /* 全部填充为已闭的 nil upvalue */
UpVal *luaF_findupval (lua_State *L, StkId level);   /* 按层级查/建，维护 openupval 有序链 */
void luaF_unlinkupval (UpVal *uv);                   /* 从开链摘除 */
void luaF_closeupval (lua_State *L, StkId level);    /* 关闭 >= level 的所有开 upvalue */

/* to-be-closed */
void luaF_newtbcupval (lua_State *L, StkId level);   /* false/nil 跳过；校验可关闭性 */
StkId luaF_close (lua_State *L, StkId level, TStatus status, int yy);
      /* 先关 upvalue，再沿 tbclist 逐个调 __close；yy 控制可否 yield */

/* 原型生命周期 */
lu_mem luaF_protosize (Proto *p);
void luaF_freeproto (lua_State *L, Proto *f);   /* 释放各数组 + call_queue + 本体 */
const char *luaF_getlocalname (const Proto *f, int local_number, int pc);

/* 调用队列（sleep/wake） */
CallQueue *luaF_newcallqueue (lua_State *L);
void luaF_freecallqueue (lua_State *L, CallQueue *q);
void luaF_callqueuepush (lua_State *L, CallQueue *q, int nargs);
      /* 从栈顶取 nargs 个参数入队 */
int  luaF_callqueuepop (lua_State *L, CallQueue *q, int *nargs, TValue *args);
      /* 出队到 args 缓冲；返回 1 成功 / 0 空队 */

/* LXCLUA 扩展 */
void luaF_hotreplace (lua_State *L, GCObject *cl, Proto *newproto);
      /* 仅对 LUA_VLCL 生效：换 p、置 ishotfixed，带写屏障 */
uint64_t luaF_hashcode (const Proto *p);   /* FNV-1a over code[] */
```

调用方式要点：

- 闭包创建后必须自行填 `upvals[]`（`luaF_newLclosure` 只置 NULL）；主解释器
  在 `OP_CLOSURE` 时按 `Upvaldesc` 逐个绑定。
- `luaF_close` 可能执行任意 `__close` 代码（若 `yy` 允许 yield 甚至可挂起协程），
  调用方要在栈安全点使用，并用 `savestack/restorestack` 保存位置（函数内部已示范）。
- `luaF_hotreplace` 的 `cl` 参数是 `GCObject*`，调用方需确认 `tt == LUA_VLCL`
  （函数内部会检查，非 Lua 闭包静默忽略）。

---

## 四、内部流程

**关闭序列（`luaF_close`）**：

```
savestack(level)
luaF_closeupval(level)          -- 开 upvalue 全部转闭
while tbclist.p >= level:       -- 倒序处理 <close> 变量
    tbc = tbclist.p; poptbclist
    prepcallclosemth(tbc, status, yy)   -- 按 status 准备错误对象后调 __close
    level = restorestack        -- __close 可能动栈，每次重算
```

**开 upvalue 链**：按栈层级升序的单链表，`previous` 是前驱指针的地址
（`UpVal **`），摘除/插入都是 O(1)；有开 upvalue 的线程挂入 `G(L)->twups`
链，GC 需要遍历它们。

---

## 五、运行验证（实测输出）

脚本 `v_lfunc.lua`：

```
U1 shared upval:	3
U2 upvalueid same:	true
C1 close on scope exit:	body	closed:nil
C2 close on error:	false	true
C3 err passed to __close:	true
C4 non-closable:	false	true
```

结论：跨闭包共享已闭 upvalue、`debug.upvalueid` 同一性、正常退出的
`__close(nil)`、错误路径触发 `__close` 且传递错误对象、非可关闭值报错——
均与源码行为一致。

---

## 六、与其他模块的关系

- 对象分配经 `lgc.c` 的 `luaC_newobj`；写屏障 `luaC_objbarrier/luaC_barrier`。
- `OP_CLOSURE`/`OP_CLOSE`/`OP_TBC` 的消费在 `vm/lvm.c`；`tbclist` 由 `ldo.c`
  的栈回收路径驱动（错误展开时 `luaF_close(L, level, errstatus, yy)`）。
- `__close` 元方法查询经 `ltm.c` 的 `luaT_gettmbyobj`。
- 热替换的上层使用者：`utils/lpatchlib.c`（补丁/热修复库）。
- 调用队列的消费者：`ldo.c`/VM 的 sleep/wake 路径（`Proto.is_sleeping`）。
- `luaF_hashcode` 与 `lundump.c` 的 `bytecode_hash` 校验配套。
