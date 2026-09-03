# core/ldebug.c — 调试接口、错误消息与符号执行

> 职责：① 行号计算（增量 `lineinfo` + 绝对锚点 `abslineinfo`）；
> ② `lua_getstack/getinfo/getlocal/setlocal/sethook` 调试 API；
> ③ 运行时错误的格式化与抛出（含变量名反查）；
> ④ 符号执行（从字节码反推变量/函数名）；⑤ **热修补 `luaB_hotfix`**。

---

## 一、特性介绍

1. **行号双层结构**：`lineinfo[]` 存相邻指令的行增量；每隔 `MAXIWTHABS`
   条指令放一个 `ABSLINEINFO` 标记，配 `abslineinfo[]`（pc→绝对行）做锚点，
   `getbaseline` 先估后校。
2. **`lua_getinfo` 扩展选项（LXCLUA）**：
   - `'k'` → `ar->islocked`（`Proto.flag & PF_LOCKED`，只读字节码函数）；
   - `'T'` → `ar->istampered`（`bytecode_hash != 0` 时现场重算
     `luaF_hashcode` 对比，防篡改检测）；
   - `'h'` → `ar->ishotfixed`（闭包被热修补过）。
   标准选项 `S/l/u/t/n/r/L/f` 保留。
3. **热修补 `luaB_hotfix(L, oldidx, newidx)`（LXCLUA）**：把旧闭包的
   `Proto` 换成新闭包的，保留旧闭包的 upvalues。约束（实测）：
   两者必须都是函数；upvalue **数量必须相等**（`upvalue count mismatch`）；
   必须有至少 1 个 upvalue（`hotfix requires closures with upvalues`）；
   旧函数 `PF_LOCKED` 时拒绝（`attempt to hotfix a locked function`）。
   落地函数 `luaF_hotreplace`（lfunc.c），置 `ishotfixed`。
4. **错误消息变量名反查**：`varinfo` 先查 upvalue，再查当前帧寄存器，
   经符号执行给出 `(local 'x')`/`(upvalue 'y')`/`(global 'g')` 后缀；
   `funcnamefromcode` 从调用指令推断 `method/field/global/metamethod/
   for iterator` 等称呼。
5. **`luaG_errormsg` 防递归（LXCLUA 改动）**：调用 `errfunc` 前先清零
   `L->errfunc`，避免错误处理器内部再出错时无限递归；错误对象为 nil 时
   替换为 `"<no error object>"`。
6. **`OP_ERRNNIL` 配套**：`luaG_errnnil` 报 `global '%s' already defined`
   （global 重定义保护指令的错误出口）。
7. **Hook 体系**：`lua_sethook` 置 `hookmask` 并对全部活动帧置 `trap`；
   `luaG_tracecall`/`luaG_traceexec` 在 VM 内按行/计数触发；
   hook 内 yield 经 `CIST_HOOKYIELD` 标记，恢复时不重复触发。

---

## 二、关键函数（准确签名）

```c
/* 调试 API（LUA_API） */
void lua_sethook (lua_State *L, lua_Hook func, int mask, int count);
lua_Hook lua_gethook (lua_State *L);
int lua_gethookmask (lua_State *L);
int lua_gethookcount (lua_State *L);
int lua_getstack (lua_State *L, int level, lua_Debug *ar);
int lua_getinfo (lua_State *L, const char *what, lua_Debug *ar);
      /* what 以 '>' 开头：函数在栈顶（消费之）；否则用 ar->i_ci */
const char *lua_getlocal (lua_State *L, const lua_Debug *ar, int n);
      /* ar==NULL：查栈顶函数的形参（按启动时活跃性） */
const char *lua_setlocal (lua_State *L, const lua_Debug *ar, int n);
void luaB_hotfix (lua_State *L, int oldidx, int newidx);   /* LXCLUA */

/* 行号与追踪（VM 内部） */
int luaG_getfuncline (const Proto *f, int pc);
const char *luaG_findlocal (lua_State *L, CallInfo *ci, int n, StkId *pos);
      /* n<0 → 变参槽 "(vararg)"；无名槽 → "(temporary)"/"(C temporary)" */
int luaG_tracecall (lua_State *L);
int luaG_traceexec (lua_State *L, const Instruction *pc);

/* 错误族（均不返回） */
l_noret luaG_runerror (lua_State *L, const char *fmt, ...);
      /* Lua 帧内自动附加 "源:行: " 前缀（luaG_addinfo） */
l_noret luaG_errormsg (lua_State *L);
l_noret luaG_typeerror (lua_State *L, const TValue *o, const char *op);
      /* "attempt to <op> a <type> value (kind 'name')" */
l_noret luaG_callerror (lua_State *L, const TValue *o);
l_noret luaG_forerror (lua_State *L, const TValue *o, const char *what);
l_noret luaG_concaterror (lua_State *L, const TValue *p1, const TValue *p2);
l_noret luaG_opinterror (lua_State *L, const TValue *p1, const TValue *p2,
                         const char *msg);
l_noret luaG_tointerror (lua_State *L, const TValue *p1, const TValue *p2);
      /* "number... has no integer representation" */
l_noret luaG_ordererror (lua_State *L, const TValue *p1, const TValue *p2);
l_noret luaG_errnnil (lua_State *L, LClosure *cl, int k);
```

---

## 三、符号执行（错误消息里的名字从哪来）

`findsetreg(p, lastpc, reg)`：从函数头扫到 `lastpc`，记录最后一次写
`reg` 的指令；`OP_JMP` 更新 `jmptarget`（跳转目标之后的赋值才算确定），
`OP_CALL/TAILCALL/TFORCALL` 视为写 A 以上全部寄存器。
`basicgetobjname` 据此识别 `OP_MOVE`（递归追源寄存器）、`OP_GETUPVAL`
（→"upvalue"）、`OP_LOADK/LOADKX`（→"constant"）；`getobjname` 再覆盖
`GETTABUP/GETTABLE/GETI/GETFIELD/SELF`（→"global"/"field"/"method"，
`_ENV` 判定经 `isEnv`）。`funcnamefromcode` 把元方法调用指令映射到
`tmname`（去 `__` 前缀，报 "metamethod"）。

---

## 四、运行验证（实测输出）

脚本 `v_ldebug.lua`（`run_lua.sh`）：

```
G1:	Lua	0	2	false                     -- what/nups/nparams/isvararg
G2 islocked/ishotfixed/istampered:	false	false	false  -- 扩展选项 k/h/T 可用
G3:	true	true                            -- 错误消息含变量信息与 源:行 前缀
G4 metamethod name:	false                 -- 元方法体内 error 走普通路径
G5 hotfix result:	101	(期望 101)        -- 旧函数执行新码，共享 counter
G5b ishotfixed:	true                      -- getinfo 'h' 反映热修补状态
G6 mismatch rejected:	true	true           -- upvalue 数不匹配被拒
G7 traceback:	true	true
G8 getlocal:	xyz	42                     -- 挂起协程的局部变量可读
```

结论：`debug.getinfo` 的 `k/T/h` 扩展、`debug.hotfix` 的完整约束与效果、
错误定位与变量名反查、协程侧 `getlocal` 全部与源码一致。

---

## 五、与其他模块的关系

- 消费 `Proto` 的调试段（`lineinfo/abslineinfo/locvars/source`），
  由编译器写入、`ldump/lundump` 搬运（`strip` 可剥离）。
- `luaB_hotfix` 的 Lua 侧暴露在 `stdlib/ldblib.c`（`debug.hotfix`）；
  底层替换在 `lfunc.c luaF_hotreplace`；`PF_LOCKED` 由装载端设置
  （`load` 模式 `'B'` / `lundump.c` fixed 路径）。
- `luaG_runerror` 的错误流：`luaG_errormsg` → `luaD_throw`（`ldo.c`）。
- hook 机制的另一半在 `ldo.c`（`luaD_hook/hookcall`）与 `vm/lvm.c`
  （`trap` 检查点）。
