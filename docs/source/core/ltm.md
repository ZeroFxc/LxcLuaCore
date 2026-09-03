# core/ltm.c + ltm.h — 元方法（tag methods）分发与变参辅助

> 职责：元方法事件的定义、查找（含 `flags` 缓存位）、调用分发；
   以及变参函数的栈调整与变参读取。是「裸运算失败 → 元方法兜底」链路的中枢。

---

## 一、特性介绍

1. **27 个元方法事件**（`TMS` 枚举 + `luaT_eventname`，顺序严格 `ORDER TM`）：
   标准 5.5 全部保留，另加两个 LXCLUA 扩展：
   - `__mindex`（`TM_MINDEX`）—— 二级索引回退：表取值未命中且元表没有
     `__index` 时，VM 改查 `__mindex`（`lvm.c` 约 1139 行）；
   - `__type`（`TM_TYPE`）—— 自定义类型名，`type()` 返回它（实测：
     `setmetatable({}, {__type=function() return "MyType" end})` 的 `type()` 返回 `"MyType"`）。
2. **fast 缓存**：`TM_INDEX..TM_EQ` 为快速访问段，`maskflags = ~(~0 << (TM_EQ+1))`。
   `fasttm` 先看表 `flags` 位（1 = 确认没有该元方法），避免每次哈希查找；
   `luaT_gettm` 查无结果时回写该位。
3. **SuperStruct 元表**：`luaT_gettmbyobj` 与 `fasttm` 均支持元表是
   `SuperStruct`（类对象）——经 `luaS_getsuperstruct_str` 查事件键。
   这是 class 实例能走元方法的原因。
4. **类型名表**：`luaT_typenames_` 覆盖全部 15+2 种类型
   （`struct/pointer/concept/namespace/superstruct/map` 均有独立名字；
   `userdata` 与 `lightuserdata` 同名 "userdata"）。
5. **yield 规则**：元方法只有在"从 Lua 代码调用"（`isLuacode(L->ci)`）时才可
   yield，否则走 `luaD_callnoyield`。
6. **变参辅助**：`luaT_adjustvarargs`（变参表模式下把固定参数搬到栈顶）、
   `luaT_getvararg`（`OP_GETVARG`：支持整数下标与 `"n"` 键）、`luaT_getvarargs`。

---

## 二、元方法事件全表（从源码抄录）

```
TM_INDEX("__index")  TM_MINDEX("__mindex")  TM_NEWINDEX("__newindex")
TM_GC("__gc")  TM_MODE("__mode")  TM_LEN("__len")  TM_EQ("__eq")   ← fast 段到此为止
TM_ADD("__add") TM_SUB("__sub") TM_MUL("__mul") TM_MOD("__mod")
TM_POW("__pow") TM_DIV("__div") TM_IDIV("__idiv")
TM_BAND("__band") TM_BOR("__bor") TM_BXOR("__bxor") TM_SHL("__shl") TM_SHR("__shr")
TM_UNM("__unm") TM_BNOT("__bnot") TM_LT("__lt") TM_LE("__le")
TM_CONCAT("__concat") TM_CALL("__call") TM_CLOSE("__close") TM_TYPE("__type")
TM_N = 27
```

关键宏：`notm(tm)` = `ttisnil(tm)`；`ttypename(x)` = `luaT_typenames_[(x)+1]`。

---

## 三、关键函数（准确签名）

```c
/* 状态初始化时调用：27 个事件名驻留并固定（永不回收） */
void luaT_init (lua_State *L);

/* 慢速查找（配合 fasttm 用）：查无则置 flags 缓存位并返回 NULL。
   仅限 event <= TM_EQ（断言）。 */
const TValue *luaT_gettm (Table *events, TMS event, TString *ename);

/* 按对象查元方法：table 先自身元表后 G->mt[LUA_TTABLE]；userdata 自身元表；
   其余类型查 G->mt[ttype]。元表可以是 Table 或 SuperStruct。无则返回 &G->nilvalue */
const TValue *luaT_gettmbyobj (lua_State *L, const TValue *o, TMS event);

/* 对象类型名：表/完整 userdata 有元表时优先 '__name' 元字段 */
const char *luaT_objtypename (lua_State *L, const TValue *o);

/* 调用元方法：3 参无返回 / 2 参 1 返回（结果搬到 res，返回结果 tag） */
void luaT_callTM (lua_State *L, const TValue *f, const TValue *p1,
                  const TValue *p2, const TValue *p3);
lu_byte luaT_callTMres (lua_State *L, const TValue *f, const TValue *p1,
                        const TValue *p2, StkId res);

/* 二元元方法兜底族：先查 p1 再查 p2；查无则报错 */
void luaT_trybinTM (lua_State *L, const TValue *p1, const TValue *p2,
                    StkId res, TMS event);   /* 算术错误文案区分位运算/算术 */
void luaT_tryconcatTM (lua_State *L);        /* 操作数在栈顶两槽 */
void luaT_trybinassocTM (lua_State *L, const TValue *p1, const TValue *p2,
                         int flip, StkId res, TMS event);  /* flip 交换实参序 */
void luaT_trybiniTM (lua_State *L, const TValue *p1, lua_Integer i2,
                     int flip, StkId res, TMS event);      /* 立即数版 */

/* 顺序元方法（__lt/__le）：返回布尔；无元方法则 ordererror */
int luaT_callorderTM (lua_State *L, const TValue *p1, const TValue *p2,
                      TMS event);
int luaT_callorderiTM (lua_State *L, const TValue *p1, int v2,
                       int flip, int isfloat, TMS event);

/* 变参 */
void luaT_adjustvarargs (lua_State *L, int nfixparams, CallInfo *ci,
                         const Proto *p);
void luaT_getvararg (lua_State *L, CallInfo *ci, StkId ra, TValue *rc);
      /* rc 为整数下标(1..n)或字符串 "n"；否则报 "invalid vararg index" */
void luaT_getvarargs (lua_State *L, CallInfo *ci, StkId where, int wanted);
      /* wanted<0 取全部并设 top；不足补 nil */
```

调用方式要点：

- 快速路径永远先 `fasttm(l, mt, e)`：`mt` 为 `Table*` 或 `SuperStruct*` 的
  `GCObject*`；命中缓存位直接得 `NULL`，省去查找。
- `luaT_trybinTM` 不返回（失败即报错），调用方无需处理失败分支。
- `luaT_callTMres` 内部 `savestack/restorestack`——元方法可能触发栈增长。

---

## 四、运行验证（实测输出）

脚本 `v_ltm_map.lua` + `v_ltm_map2.lua`（`run_lua.sh`）：

```
T1 __add:	2	4
T2 __lt:	true	false	...v_ltm_map.lua:11: attempt to compare two table values
T3 concat:	joined	len:	42	eq:	true	call:	called:hi
T3 index:	ix-abc
T3 newindex:	9
T4b err:	...v_ltm_map2.lua:3: attempt to perform arithmetic on a table value (upvalue 'typed')
T5b err:	...v_ltm_map2.lua:5: number has no integer representation
N4 type(typed):	MyType
```

结论：`__add/__lt/__concat/__len/__eq/__call/__index/__newindex` 全部生效；
无元方法的表比较报 `attempt to compare two table values`（`luaG_ordererror`）；
浮点位运算报 `number has no integer representation`（`luaG_tointerror`，
对应 `luaT_trybinTM` 的位运算分支）；`__type` 元方法改变 `type()` 返回值
（baselib 消费 `TM_TYPE`），但不影响算术错误文案（那用的是 `__name`/`luaT_objtypename`）。

---

## 五、与其他模块的关系

- 消费者：`vm/lvm.c`（各算术/比较/连接指令的回退）、`core/lobject.c`
  （`luaO_arith`）、`core/lfunc.c`（`__close`）、`core/lgc.c`（`__gc`/`__mode`）。
- `__mindex` 消费点在 `vm/lvm.c` 表索引回退路径；`__type` 由
  `stdlib/lbaselib.c` 的 `type()` 消费（已实测）。
- SuperStruct 事件查找依赖 `stdlib/lsuper.c` 的 `luaS_getsuperstruct_str`。
- 变参路径与 `lopcodes.h` 的 `OP_VARARGPREP/OP_VARARG/OP_GETVARG` 对应。
