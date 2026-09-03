# core/lstate.c + lstate.h — 全局状态与线程状态（引擎的中枢结构）

> 职责：定义并创建/销毁引擎的两层状态——`global_State`（一个状态机共享：
> 内存账本、字符串表、GC 状态、元方法名、锁、内存池、保护码表等）与
> `lua_State`（每线程：栈、CallInfo 链、upvalue、hook、错误恢复点）；
> 以及 `CallInfo` 结构与调用状态位。

---

## 一、特性介绍

1. **两级状态**：`lua_newstate` 一次分配 `LG = { LX(主线程+EXTRASPACE), global_State }`
   整块；`lua_newthread` 只新建 `LX`（线程），共享同一 `global_State`。
2. **全局互斥锁（LXCLUA 扩展）**：`global_State.lock`（`l_mutex_t`）保护共享资源
   （字符串驻留、注册表等），`lua_lock/lua_unlock` 语义下所有线程安全路径均经此锁。
3. **共享注册表**：`init_registry` 把注册表与自定义操作码引用表的 `is_shared` 置 1，
   配合 `Table.lock` 读写锁实现跨线程安全访问。
4. **GCdebt 原子化**：`GCdebt` 为 `_Atomic l_mem`，分配记账用原子操作
   （多线程共享状态的配套改造）。
5. **自定义操作码基础设施**：`custom_op_handlers[256]`（C 函数指针表）+
   `custom_op_reftable`（Lua 函数引用表）+ 计数，支撑 `OP_CUSTOM` 扩展。
6. **关键字注册表**：`keyword_registry`（`KeywordRegEntry{name, Proto*}`）
   支持 `$name` 语法在编译期直接引用预编译函数。
7. **VM 保护码表链**：`vm_code_list`（`VMCodeTable*` 头指针）。
8. **警告强制可见（LXCLUA 改动）**：`luaE_warning` 无条件 `fprintf(stderr, ...)`，
   因此编译器警告（如 `unused local variable`）无需 `warn` 函数也可见——
   运行脚本时看到的告警即来源于此（实测）。
9. **Android 调试日志层**（`#ifdef __ANDROID__`）：`lua_log_init` 依次尝试
   `/data/local/tmp/a.log` 等 5 个路径；`LUA_LOGI/LUA_LOGE` 宏贯穿状态创建/
   销毁与库打开流程，便于定位嵌入式启动崩溃。
10. **ARM64 安全字符串比较**（Android）：`safe_strcmp/safe_strncmp` 纯字节实现，
    规避 bionic `strcmp` NEON 向量化对未对齐 `TString` 数据的 bus error。

---

## 二、关键数据结构与属性

### 2.1 `global_State`（字段级，按源码顺序）

| 字段 | 说明 |
|---|---|
| `frealloc / ud` | 用户分配器及其数据 |
| `GCtotalbytes / GCdebt(_Atomic) / GCestimate / lastatomic` | 内存账本与 GC 估算 |
| `lock` | **全局互斥锁（扩展）** |
| `strt` | 字符串驻留表（`stringtable{hash,nuse,size}`） |
| `l_registry / nilvalue` | 注册表 TValue / 全局 nil |
| `seed` | 哈希随机种子（状态创建时传入） |
| `gcparams[] / currentwhite / gcstate / gckind / gcstp / gcstopem / gcemergency` | GC 参数与状态机 |
| `gcpause / gcstepmul / gcstepsize / genminormul / genmajormul` | 增量/分代 GC 控制 |
| `allgc / sweepgc / finobj / gray / grayagain / weak / ephemeron / allweak / tobefnz / fixedgc` | GC 对象链 |
| `survival / old1 / reallyold / firstold1 / finobjsur / finobjold1 / finobjrold` | 分代 GC 分代链 |
| `twups` | 有开 upvalue 的线程链 |
| `panic / mainthread / memerrmsg / tmname[TM_N] / mt[LUA_NUMTYPES]` | 恐慌函数/主线程/预置串/元方法名/基础类型元表 |
| `strcache[53][2]` | API 字符串缓存（`STRCACHE_N/M`） |
| `warnf / ud_warn` | 警告函数 |
| `mempool` | **小对象内存池（扩展，`MemPoolArena`）** |
| `vm_code_list` | **VM 保护码表链（扩展）** |
| `keyword_registry / kwreg_size / kwreg_count` | **$关键字注册表（扩展）** |
| `custom_op_handlers[256] / custom_op_reftable / custom_op_count` | **自定义 opcode 处理器（扩展）** |

### 2.2 `lua_State`（每线程）

`CommonHeader` + `status / allowhook / nci` + `top`（首空槽）+ `l_G`（指向全局态）
+ `ci`（当前 CallInfo）+ `stack_last / stack` + `openupval`（开 upvalue 链）
+ `tbclist`（to-be-closed 链）+ `gclist` + `twups` + `errorJmp`（longjmp 恢复点链）
+ `base_ci`（第 0 层 CallInfo，内嵌）+ `hook / errfunc / nCcalls / oldpc /
basehookcount / hookcount / hookmask`。

`nCcalls` 双半字：低 16 位 C 递归深度，高 16 位不可 yield 调用数
（`yieldable(L)` = 高半字为 0；`LUAI_MAXCCALLS` 到达报 `C stack overflow`）。

### 2.3 `CallInfo`（调用帧）

`func / top`（StkIdRel 指针-偏移二态）+ `previous/next` 双链 + 联合体
`u.l`（Lua 帧：`savedpc / trap / nextraargs`）与 `u.c`（C 帧：`k 续延 /
old_errfunc / ctx`）+ `u2`（funcidx / nyield / nres / transferinfo）+
`nresults`（期望结果数，上限 `MAXRESULTS=250`）+ `callstatus` 位字段。

状态位（重点）：`CIST_C`（C 函数）、`CIST_FRESH`（新 `luaV_execute` 帧）、
`CIST_HOOKED`、`CIST_YPCALL`（可 yield 保护调用）、`CIST_TAIL`、
`CIST_FIN`（正在跑终结器）、`CIST_CLSRET`（正在关 tbc）、
`CIST_RECST`（10-12 位，恢复状态）、**`CIST_AWAIT (1<<14)`（LXCLUA：
`OP_AWAIT` 语法级 yield，不走函数调用）**。`MAX_CCMT` 说明 8-12 位计数
`__call` 元方法嵌套（最多 30 个对象）。

`isLua(ci)` = 非 C 调用；`isLuacode(ci)` = 非 C 且非 hook（元方法可否
yield 的判断依据，见 `ltm.c`）。

---

## 三、关键函数（准确签名）

```c
/* 状态生命周期（公共 API） */
lua_State *lua_newstate (lua_Alloc f, void *ud, unsigned seed);
      /* 分配 LG；失败返回 NULL。f_luaopen 在保护模式下完成栈/注册表/
         字符串/元方法名/词法保留字初始化；失败则清理返回 NULL */
void lua_close (lua_State *L);          /* 仅主线程可关；先跑终结器再全回收 */
lua_State *lua_newthread (lua_State *L);/* 新协程：继承 hook 设置与 extraspace */
int lua_closethread (lua_State *L, lua_State *from); /* 复位线程（跑 __close） */
int lua_resetthread (lua_State *L);     /* 已废弃别名 */
int lua_setcstacklimit (lua_State *L, unsigned int limit);
      /* 本实现为空操作，恒返回 LUAI_MAXCCALLS */

/* 内部（LUAI_FUNC / lstate.h 声明） */
void luaE_setdebt (global_State *g, l_mem debt);  /* 保持总字节不变式 */
CallInfo *luaE_extendCI (lua_State *L);   /* ci 链尾增长 */
void luaE_shrinkCI (lua_State *L);        /* 隔一个释放一个空闲 ci */
void luaE_checkcstack (lua_State *L);     /* C 栈溢出/二次溢出检查 */
void luaE_incCstack (lua_State *L);       /* 深度+1 并检查 */
void luaE_freethread (lua_State *L, lua_State *L1);
int luaE_resetthread (lua_State *L, int status);
void luaE_warning (lua_State *L, const char *msg, int tocont);
      /* 扩展：无条件写 stderr + 调 warnf */
void luaE_warnerror (lua_State *L, const char *where);
void luaE_lock (lua_State *L); / void luaE_unlock (lua_State *L);
      /* g->lock 加解锁封装（扩展） */
```

### 初始化时序（`lua_newstate` → `f_luaopen`）

```
分配 LG → preinit_thread → GC 参数/链表清零 →
vm_code_list/keyword_registry/custom_op_handlers 初始化 →
luaM_poolinit → l_mutex_init(g->lock) →
f_luaopen（保护模式）:
  stack_init → init_registry(registry.is_shared=1 + custom_op_reftable)
  → luaS_init → luaT_init → luaX_init → gcstp=0 → setnilvalue(nilvalue)
     （nilvalue 置 nil = 状态完整的标志，见 completestate）
```

---

## 四、运行验证（实测输出）

脚本 `v_lstate.lua`：

```
co status:	suspended
co status after close:	dead
warnings go to stderr even without warn function (see above)
```

结论：`coroutine.close`（走 `lua_closethread → luaE_resetthread`）把挂起线程
置回死亡态；此前各验证脚本中出现的 `warning: unused local variable [unused]`
即 `luaE_warning` 无条件 stderr 输出的直接证据（本会话未安装任何 `warn` 函数）。

---

## 五、与其他模块的关系

- `lstate.h` 被所有核心模块包含；`GCUnion` + `gco2*` 宏是全部对象指针转换的
  唯一合法途径。
- 字符串表/缓存操作在 `lstring.c`；GC 状态机在 `lgc.c`（消费 `gcstate/gckind`
  与全部分代链字段）；锁与原子原语定义在 `utils/lthread.h`。
- `custom_op_handlers` 的注册/分派：`vm/lvmustom.c`；`vm_code_list` 由
  `vm/lvmpro.c` 管理；`keyword_registry` 由编译器（`$keyword` 语法）消费。
- `EXTRA_STACK=5`、`BASIC_STACK_SIZE=2*LUA_MINSTACK` 是栈扩容（`ldo.c`）
  与元方法调用（`ltm.c`）的空间假设。
