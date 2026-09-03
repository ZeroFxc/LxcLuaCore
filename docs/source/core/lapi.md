# core/lapi.c + lua.h — 公共 C API 实现

> 职责：宿主与引擎之间全部 `lua_*` 公共接口的实现——栈操作、值存取、
> 类型查询、表/元表操作、调用与保护调用、加载/导出、协程、调试、引用，
> 以及 LXCLUA 的全部扩展 API（OOP/命名空间/切片/指针/外部字符串/
> 锁表/热修补/字节码混淆导出/TCC 运行时桥）。

---

## 一、特性介绍

1. **标准 5.5 API 全保留**，语义一致（索引规则：正数自帧底、负数自栈顶、
   `LUA_REGISTRYINDEX` 注册表、更负为 C 闭包 upvalue）。
2. **共享表锁**（LXCLUA）：`lua_gettable/getfield/geti/rawget*/
   lua_settable/setfield/seti/rawset*` 等对表快速路径均先持
   `h->lock` 读/写锁；另提供显式 `lua_locktable/lua_unlocktable`。
3. **扩展类型贯穿 API**：`lua_type` 可返回 `LUA_TSTRUCT/TPOINTER/
   TCONCEPT/TNAMESPACE/TSUPERSTRUCT/TMAP`；`lua_getmetatable` 的元表
   可能是 `SuperStruct`（压栈为 superstruct 值）；map 无元表（实测
   `getmetatable(map) == nil`，`setmetatable(map)` 报
   `table expected, got map`）。
4. **`lua_rawlen`** 覆盖字符串（短/长）、userdata、表（`#`）。
5. **VMP 钩点**：`lapi_vmp_hook_point()`。

---

## 二、API 全清单（按功能分组，★ = LXCLUA 扩展）

### 状态与线程

```c
lua_State *lua_newstate (lua_Alloc f, void *ud, unsigned seed);
void lua_close (lua_State *L);
lua_State *lua_newthread (lua_State *L);
int lua_closethread (lua_State *L, lua_State *from);
int lua_resetthread (lua_State *L);            /* 废弃别名 */
lua_CFunction lua_atpanic (lua_State *L, lua_CFunction panicf);
lua_Number lua_version (lua_State *L);
void lua_locktable (lua_State *L, int idx);    /* ★ 显式表锁 */
void lua_unlocktable (lua_State *L, int idx);  /* ★ */
```

### 栈操作

```c
int lua_absindex; int lua_gettop; void lua_settop; void lua_pushvalue;
void lua_rotate (lua_State *L, int idx, int n);
void lua_rotate_multi (lua_State *L, int idx, int n);   /* ★ 仅正索引，0/±1 空操作 */
void lua_copy (lua_State *L, int fromidx, int toidx);
int lua_checkstack (lua_State *L, int n);
void lua_xmove (lua_State *from, lua_State *to, int n);
void lua_closeslot (lua_State *L, int idx);
```

### 类型查询 / 值读取

```c
int lua_type; const char *lua_typename;
int lua_isnumber/lua_isstring/lua_iscfunction/lua_isinteger/lua_isuserdata;
lua_Number lua_tonumberx; lua_Integer lua_tointegerx;
lua_Integer lua_tointeger_safe (lua_State *L, int idx, int *isnum, int *overflow);
      /* ★ Windows 构建下数字转换失败标 overflow */
int lua_toboolean; const char *lua_tolstring; lua_Unsigned lua_rawlen;
lua_CFunction lua_tocfunction; void *lua_touserdata;  /* 含 pointer 类型 */
lua_State *lua_tothread; const void *lua_topointer;
int lua_rawequal; int lua_compare (LUA_OPEQ/OPLT/OPLE);
void lua_arith (lua_State *L, int op);   /* LUA_OPADD..LUA_OPBNOT */
unsigned lua_numbertocstring (lua_State *L, int idx, char *buff);  /* ★ */
size_t lua_stringtonumber (lua_State *L, const char *s);           /* ★ 支持 0x/0b/0o */
```

### 压栈

```c
void lua_pushnil/lua_pushnumber/lua_pushinteger/lua_pushboolean;
void lua_pushlightuserdata; void lua_pushpointer (lua_State *L, void *p); /* ★ */
const char *lua_pushlstring/lua_pushstring/lua_pushfstring;
const char *lua_pushexternalstring (lua_State *L, const char *s, size_t len,
                                    lua_Alloc falloc, void *ud);   /* ★ 零拷贝 */
void lua_pushcclosure (lua_State *L, lua_CFunction fn, int n);  /* n=0 为 light */
int lua_pushthread (lua_State *L);
```

### 表 / 元表 / userdata

```c
int lua_getglobal/lua_gettable/lua_getfield/lua_geti;
int lua_rawget/lua_rawgeti/lua_rawgetp;
void lua_setglobal/lua_settable/lua_setfield/lua_seti;
void lua_rawset/lua_rawseti/lua_rawsetp;
void lua_createtable (lua_State *L, int narr, int nrec);
int lua_getmetatable (lua_State *L, int objindex);  /* 可压出 superstruct */
int lua_setmetatable (lua_State *L, int objindex);  /* 消费栈顶表或 superstruct */
void *lua_newuserdatauv (lua_State *L, size_t sz, int nuvalue);
int lua_getiuservalue/lua_setiuservalue (lua_State *L, int idx, int n);
void lua_table_iextend (lua_State *L, int idx, int n);  /* ★ 数组段预扩 */
```

### 调用 / 加载 / 协程

```c
void lua_callk (lua_State *L, int nargs, int nresults, lua_KContext ctx, lua_KFunction k);
int lua_pcallk (lua_State *L, int nargs, int nresults, int errfunc, lua_KContext ctx, lua_KFunction k);
int lua_load (lua_State *L, lua_Reader reader, void *dt, const char *chunkname, const char *mode);
int lua_dump (lua_State *L, lua_Writer writer, void *data, int strip);
int lua_dump_obfuscated (lua_State *L, lua_Writer writer, void *data, ...); /* ★ 带混淆 */
int lua_yieldk/lua_resume/lua_status/lua_isyieldable;
int lua_error (lua_State *L);            /* 消费栈顶错误对象 */
int lua_next (lua_State *L, int idx);
void lua_concat (lua_State *L, int n); void lua_len (lua_State *L, int idx);
```

### GC / 内存

```c
int lua_gc (lua_State *L, int what, ...);   /* LUA_GCSTOP/RESTART/COLLECT/COUNT/
                                               STEP/ISRUNNING/INC/GEN 等 */
size_t lua_getmemoryusage (lua_State *L);   /* ★ */
void lua_gc_force (lua_State *L);          /* ★ */
lua_Alloc lua_getallocf (lua_State *L, void **ud);
void lua_setallocf (lua_State *L, lua_Alloc f, void *ud);
```

### OOP / 命名空间 / 运算符扩展（★，全部对应一条指令语义）

```c
void lua_newclass (lua_State *L, const char *name);
void lua_inherit (lua_State *L, int child_idx, int parent_idx);
void lua_newobject (lua_State *L, int class_idx, int nargs);
void lua_setmethod (lua_State *L, int class_idx, const char *name, int func_idx);
void lua_checkoverride (lua_State *L, int class_idx, const char *name);
void lua_setstatic (lua_State *L, int class_idx, const char *name, int value_idx);
void lua_getprop / lua_setprop (lua_State *L, int obj_idx, const char *key, ...);
int lua_instanceof (lua_State *L, int obj_idx, int class_idx);
void lua_implement (lua_State *L, int class_idx, int interface_idx);
void lua_getsuper (lua_State *L, int obj_idx, const char *name);
void lua_compute_mro (lua_State *L, int class_idx);
void lua_setifaceflag (lua_State *L, int idx);
void lua_addmethod (lua_State *L, int idx, const char *name, int nparams);
void lua_newnamespace (lua_State *L, const char *name);
void lua_linknamespace (lua_State *L, int idx1, int idx2);
void lua_newsuperstruct (lua_State *L, const char *name);
void lua_setsuper (lua_State *L, int idx, int key_idx, int val_idx);
void lua_slice (lua_State *L, int idx, int start_idx, int end_idx, int step_idx);
int lua_spaceship (lua_State *L, int idx1, int idx2);  /* <=> 返回 -1/0/1 */
int lua_is (lua_State *L, int idx, const char *type_name);
void lua_checktype (lua_State *L, int idx, const char *type_name);
void lua_getcmds (lua_State *L); void lua_getops (lua_State *L);
void lua_errnnil (lua_State *L, int idx, const char *msg);
```

### TCC 运行时桥（★，`lua_tcc_*`）

供"字节码 → C"产物（`lbctc`）链接的指令级运行时：`lua_tcc_prologue`
（建帧）、`lua_tcc_gettabup/settabup`（upvalue 表读写）、
`lua_tcc_loadk_str/loadk_int/loadk_flt`（常量装载到寄存器索引）、
`lua_tcc_in`（`in` 运算）、`lua_tcc_push_args/store_results`（调用搬运）、
`lua_tcc_decrypt_string`（时间戳 XOR 字符串解密）。

### 其它

```c
void luaB_hotfix (lua_State *L, int oldidx, int newidx);  /* ★ 见 ldebug.md */
void lua_toclose (lua_State *L, int idx);
void lua_setwarnf/lua_warning;
int lua_setcstacklimit (lua_State *L, unsigned int limit);  /* 空实现 */
```

---

## 三、运行验证（实测输出）

脚本 `verify_lapi.lua`（`run_lua.sh`，前期调研生成、本会话实测）：

```
== 1. 基础类型名 ==  nil boolean number number string table function / thread
== 2. LXCLUA 新类型 ==
struct 实例 type:	struct
superstruct type:	superstruct
namespace type:	namespace	call:	hi
map type:	map	get:	100	#m:	2
== 3 ==  superstruct 遍历: a=1 b=2   map 遍历: k1=100 k2=200
== 4 ==  mt 生效:	42	 getmetatable 同一性:	true
getmetatable(map):	nil
setmetatable(map) 出错:	false	bad argument #1 to 'setmetatable' (table expected, got map)
== 5 ==  pcall(error):	false	boom    pcall 表错误对象:	false	table
== 6 ==  tostring: 123 / 1.5 / true / nil / table: 0x.. / function: 0x..
== 7 ==  userdata: file (0x..)	isuserdata: true
== 7b == pointer type:	pointer	ptr.sub 差值:	4
== 8 ==  GC 控制正常，collect 后内存回落
== 9 ==  原实例 p.x:	3	 副本 p2.x:	99     -- struct 赋值深拷贝
```

结论：扩展类型经 `type()` 正确暴露；map/superstruct/namespace 的创建与
遍历正常；map 无元表约束生效；struct 值语义（赋值深拷贝）在语言层成立。

---

## 四、与其他模块的关系

- 栈协议与 `lstate.h`（`CallInfo/StackValue`）；调用经 `ldo.c`；
  索引/类型/算术错误由 `ldebug.c` 抛出。
- 表操作底层 `ltable.c`（含锁）；map 操作底层 `lmap.c`；
  OOP API 底层 `stdlib/lclass.c` + `stdlib/lsuper.c`；
  命名空间 `utils/lnamespace.c`；外部长串 `lstring.c luaS_newextlstr`。
- `lua_dump_obfuscated` → `ldump.c luaU_dump_obfuscated`。
- `lua_tcc_*` 的消费方是 `compiler/lbctc.c` 生成的 C 代码。
- 辅助层（`luaL_*`）在 `lauxlib.c`。
