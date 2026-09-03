# core/ldo.c + ldo.h — 调用协议、栈管理、错误传播与协程

> 职责：函数调用的准备/收尾（`luaD_precall`/`luaD_poscall`）、保护执行与
> 异常机制（longjmp 链）、Lua 栈的增长/收缩、协程的 resume/yield/恢复、
> 以及受保护的编译器/加载器入口（含**加密字节码分派**）。

---

## 一、特性介绍

1. **异常模型**：`lua_longjmp` 链（`L->errorJmp`）+ `LUAI_TRY/THROW`
   （C++ 异常 / POSIX `_setjmp` / ISO `setjmp` 三选一编译期选择）。
   `luaD_throw` 逐层规则：线程有恢复点→跳；无→关闭 upvalue 后转投主线程；
   主线程也没有→`panic`→`abort`。
2. **VMP 钩点（LXCLUA 保护）**：`luaD_call` 入口调 `ldo_vmp_hook_point()`
   （`VMP_MARKER`，`__attribute__((noinline))`），配合 `utils/lobfuscate.c`
   的 VMProtect 集成。
3. **async 纯语法级直路径（LXCLUA 扩展）**：`luaD_precall` 发现目标函数
   `Proto.flag & PF_ASYNC` 且当前是主线程时，不建 Lua 帧，直接
   `lvm_async_invoke(L)`（创建协程执行体并返回 Promise）；协程内调用则按
   普通函数执行，`await` 由 `OP_AWAIT` 处理。实测：主线程调用返回
   `Promise<Fulfilled>` userdata；`async+await` 在协程内同步完成。
4. **OP_AWAIT 恢复（LXCLUA 扩展）**：`resume()` 检查 `CIST_AWAIT` 位——
   从 `savedpc-1` 取 `OP_AWAIT` 指令，把 resume 传入的 Promise 解析值写入
   `R[A]` 后继续 `luaV_execute`，无函数调用开销。
5. **Concept 可调用**：`luaD_precall`/`luaD_pretailcall` 把 `LUA_VCONCEPT`
   当 Lua 函数处理（取其 `p` 建帧）。
6. **增强 upvalue 校验（LXCLUA 加固）**：建帧前逐个检查 `Upvaldesc`——
   `instack ∈ {0,1}`；instack 时 `idx < 512`（寄存器槽），否则 `idx ≤ 255`
   （外层 upvalue 索引）；`kind ∈ [0,2]`。防篡改字节码导致越界。
7. **加密块分派（LXCLUA 扩展）**：`f_parser` 读到 `\x1b` 后续 3 字节为
   `"Enc"` 时，读 8 字节时间戳 + 16 字节 IV，`luaZ_init_decrypt` 后按解密
   流重新取首字节；否则回退 3 字节走标准路径（`\x1bLua` 二进制或文本）。
8. **加载模式扩展**：`mode` 串中 `'B'` = fixed（置 `PF_FIXED`，字节码进固定
   内存）、`'S'` = force_standard（`luaU_undump` 强制标准模式）。
9. **to-be-closed 保护关闭**：`luaD_closeprotected` 循环调 `luaF_close`，
   `__close` 自身抛错时恢复 `ci/allowhook` 再来一轮；错误路径的关闭状态经
   `CIST_RECST` 位保存在 `callstatus` 里跨 yield。
10. **栈管理**：倍增扩容（上限 `LUAI_MAXSTACK=1,000,000`），超限加
    `STACKERRSPACE=200` 错误处理空间并报 `stack overflow`；realloc 期间
    指针全部转偏移（`relstack/correctstack`，严格 ISO C 模式）且禁紧急 GC；
    `luaD_shrinkstack` 按使用量 3 倍阈值回收。

---

## 二、关键数据结构与属性

```c
typedef struct lua_longjmp {           /* 异常恢复点链 */
  struct lua_longjmp *previous;
  jmp_buf b;
  volatile TStatus status;
} lua_longjmp;

struct SParser {                       /* f_parser 上下文 */
  ZIO *z; Mbuffer buff; Dyndata dyd;
  const char *mode; const char *name;
};
```

常量：`MAXSTACK` = min(`LUAI_MAXSTACK`, `MAX_SIZET/sizeof(StackValue)-200`)；
`ERRORSTACKSIZE = MAXSTACK + 200`；`errorstatus(s)` = `s > LUA_YIELD`。

---

## 三、关键函数（准确签名，按主题）

```c
/* 异常与保护执行 */
l_noret luaD_throw (lua_State *L, TStatus errcode);
int luaD_rawrunprotected (lua_State *L, Pfunc f, void *ud);
      /* 链入新 lua_longjmp，恢复 nCcalls；返回状态 */
int luaD_pcall (lua_State *L, Pfunc func, void *u,
                ptrdiff_t old_top, ptrdiff_t ef);
      /* pcall/xpcall 底层：失败时恢复 ci/allowhook → closeprotected →
         seterrorobj → shrinkstack */
void luaD_seterrorobj (lua_State *L, TStatus errcode, StkId oldtop);
      /* ERRMEM 用预置 memerrmsg；其余移动栈顶错误对象 */
l_noret luaD_errerr (lua_State *L);    /* "error in error handling" */

/* 调用协议 */
CallInfo *luaD_precall (lua_State *L, StkId func, int nresults);
      /* 返回 NULL = C 函数已调完；返回 ci = Lua 函数待执行；
         非函数 → tryfuncTM 找 __call 后 goto retry */
int luaD_pretailcall (lua_State *L, CallInfo *ci, StkId func,
                      int narg1, int delta);   /* 尾调用复用帧 */
void luaD_call (lua_State *L, StkId func, int nResults);      /* 可 yield */
void luaD_callnoyield (lua_State *L, StkId func, int nResults);
void luaD_poscall (lua_State *L, CallInfo *ci, int nres);
      /* rethook + moveresults（0/1/MULTRET/tbc 四路特化）+ 回退 ci */
void luaD_hook (lua_State *L, int event, int line, int ftransfer, int ntransfer);
void luaD_hookcall (lua_State *L, CallInfo *ci);

/* 栈 */
int luaD_reallocstack (lua_State *L, int newsize, int raiseerror);
int luaD_growstack (lua_State *L, int n, int raiseerror);
void luaD_shrinkstack (lua_State *L);
void luaD_inctop (lua_State *L);
int luaD_checkminstack (lua_State *L);

/* 协程 */
int lua_resume (lua_State *L, lua_State *from, int nargs, int *nresults);
int lua_isyieldable (lua_State *L);
int lua_yieldk (lua_State *L, int nresults, lua_KContext ctx, lua_KFunction k);
      /* 不可 yield 时报 "attempt to yield across a C-call boundary" /
         "attempt to yield from outside a coroutine" */
TStatus luaD_closeprotected (lua_State *L, ptrdiff_t level, TStatus status);

/* 编译/加载入口 */
TStatus luaD_protectedparser (lua_State *L, ZIO *z, const char *name,
                              const char *mode);
      /* 解析期间不可 yield（incnny）；SParser 临时结构用后即清 */
```

调用方式要点：

- `luaD_call` 假设函数与参数已就位且栈有余量；内部经 `ccall` 计数
  `nCcalls`（溢出报 `C stack overflow`）。
- `luaD_precall` 返回的 `ci` 必须由调用方执行（`luaV_execute`）；
  VM 的 `Protect` 宏族在可能重入的操作前保存/恢复状态。
- `moveresults` 的 `wanted` 编码：负值经 `hastocloseCfunc` 判定携带
  to-be-closed 信息（`decodeNresults` 解码）。

---

## 四、内部流程

**错误传播链**：

```
luaG_runerror/error() → luaD_throw(errcode)
  ├─ L->errorJmp 存在 → 置 status，longjmp 回去
  └─ 无 → luaE_resetthread（关 upvalue/tbc）→
          主线程有 errorJmp → 错误对象搬过去重抛
          否则 → panic → abort
```

**pcall 失败收尾**（`luaD_pcall`）：恢复 `ci/allowhook` →
`luaD_closeprotected`（逐个跑 `__close`，可多轮）→ `luaD_seterrorobj` →
`luaD_shrinkstack`。

**协程恢复**（`resume`）：首启 `ccall` 函数体；否则按三态分派——
`CIST_AWAIT`（OP_AWAIT 写结果续跑）/ hook yield（回退 pc 续跑）/
普通 C yield（调续延函数 k 后 `luaD_poscall`）；最后 `unroll` 把剩余帧跑完；
`precover` 处理可恢复错误（沿 `CIST_YPCALL` 帧下降）。

**加载分派**（`f_parser`）：

```
首字节 == 0x1B ?
  读 3 字节: "Enc" → 读 timestamp(8)+iv(16) → luaZ_init_decrypt → 重取首字节
            其它  → zungetc x3 回退
再次判断首字节 == 0x1B → luaU_undump（'B'→fixed，'S'→force_standard）
否则 → 文本：luaY_parser
末尾：断言 nupvalues == sizeupvalues，luaF_initupvals
```

---

## 五、运行验证（实测输出）

脚本 `v_ldo.lua`（`run_lua.sh`；脚本由前期调研生成，输出为本会话实测）：

```
multi():	10	20	30	nil            -- 多值返回补 nil
截断到 2 个:	10	20
pcall error:	false	...v_ldo.lua:14: boom
pcall runtime:	false	...v_ldo.lua:19: attempt to index a nil value (local 't')
链式包装:	false	L5(L4(L3(L2(L1(bottom)))))   -- 错误逐层包装传播
xpcall:	false	[handler] got: xp                -- errfunc 生效
错误时 __close 逆序执行:	false	main-error	|	close:v close:u
__close 抛错:	false	close-fail:v2	|	close:v2 close:u2  -- close 错误顶替原错误
Lua 递归栈溢出:	false	...v_ldo.lua:76: stack overflow
resume#1:	true	101
协程内 pcall:	false	inside-co
resume#2:	true	102
resume#3 (dead):	false	cannot resume dead coroutine
catch 捕获:	...v_ldo.lua:100: attempt to index a nil value (local 't')
finally 总是执行                                  -- try/catch/finally 语法可用
try 正常路径
主线程调用 async 函数返回:	userdata	Promise<Fulfilled>
协程内 async+await:	true	42                    -- PF_ASYNC 协程内直执行
fsleep:	true
休眠期间命中数:	0                                 -- 调用被入队拦截
fwake 重放次数:	3	| 命中:	a,b,c               -- 唤醒批量重放
唤醒后直接调用:	a,b,c,d
```

结论：调用协议（补参/截断/多值）、三层嵌套保护调用、`__close` 逆序与
错误顶替、栈溢出、协程全生命周期、`try/catch/finally` 语法、
async 主线程返回 Promise、协程内 `await`、`fsleep/fwake` 调用队列机制
——全部与源码行为一致。

---

## 六、与其他模块的关系

- `lvm_async_invoke` 定义在 `vm/lvm.c`（async 直路径实现）；`OP_AWAIT`
  的挂起侧也在 `vm/lvm.c`（置 `CIST_AWAIT` 后 yield）。
- `fsleep/fwake` 的暴露层在标准库（`Proto.is_sleeping`/`call_queue`
  由 `lfunc.c` 提供数据结构）。
- `try/catch/finally` 的解析在 `compiler/lparser.c`（编译期展开为保护调用）。
- `luaY_parser`（文本）在 `compiler/lparser.c`；`luaU_undump`（二进制/加密）
  在 `lundump.c`；解密流在 `lzio.c`。
- `VMP_MARKER` 定义在 `utils/lobfuscate.c`（保护工具链）。
- `lua_lock/lua_unlock` 语义经 `lstate.c` 的 `g->lock`（线程安全）。
