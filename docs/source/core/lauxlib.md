# core/lauxlib.c + lauxlib.h — 辅助库（参数检查/缓冲/引用/装载扩展）

> 职责：在公共 API 之上的标准辅助层——参数校验与错误格式化、
> 字符串缓冲、注册表引用、元表助手、模块注册，以及**装载路径的
> 两大扩展：`Nirithy==` 壳解码与 JSON 文件装载**。

---

## 一、特性介绍

1. **参数检查族**：`luaL_check*/luaL_opt*` 全部带 `bad argument #n to 'name'`
   格式化（实测格式与 5.5 一致）。
2. **装载扩展一：`Nirithy==` 壳**（`luaL_loadfilex` 与 `luaL_loadbufferx`）：
   文件/缓冲以 9 字节前缀 `Nirithy==` 开头时，按自定义 base64
   （字母表 `9876543210zyx...BA-_`）解码 → 得到
   `时间戳(8)+IV(16)+密文`，**不在本地解密**，重新包上
   `\x1bEnc` 头交给 `ldo.c f_parser → lzio.c` 的 AES-CTR 路径处理。
   解码失败（长度非 4 倍数/非法字符/≤24 字节）则回落普通装载。
3. **装载扩展二：JSON 文件**（`luaL_loadfilex`）：跳过空白后首字符为
   `{` 的文件整体读入，`json_to_lua`（`utils/json_parser.c`）转成
   "返回表构造器"的 Lua 源码再 `luaL_loadbuffer`——`loadfile("x.json")`
   得到的 chunk 执行即返回表（实测 `a=1, b[2]=20, c.name="lxclua"`）。
4. **文件读取细节**：跳过 UTF-8 BOM；首行 `#` 视为 shebang 注释
   （补 `\n` 保行号）；二进制签名触发 `freopen("rb")` 重开。
5. **缓冲系统**：`luaL_Buffer` 两级（预分配盒 `LUAL_BUFFERSIZE` + 大串直接
   压栈），`luaL_addgsub` 支持替换串里的 `%0..%9/上界` 展开。
6. **引用系统**：`luaL_ref/luaL_unref` 挂在注册表整数键，带空闲链复用。
7. **`luaL_makeseed`**：从状态地址、时间等混合出随机种子（供
   `lua_newstate`）。

---

## 二、函数清单（按组，签名从源码）

### 错误与检查

```c
void luaL_where (lua_State *L, int level);        /* 压 "源:行: " */
int luaL_error (lua_State *L, const char *fmt, ...);
int luaL_argerror (lua_State *L, int arg, const char *extramsg);
int luaL_typeerror (lua_State *L, int arg, const char *tname);
const char *luaL_checklstring (lua_State *L, int arg, size_t *len);
const char *luaL_optlstring (lua_State *L, int arg, const char *def, size_t *len);
lua_Number luaL_checknumber/luaL_optnumber;
lua_Integer luaL_checkinteger/luaL_optinteger;
int luaL_checkoption (lua_State *L, int arg, const char *def, const char *const lst[]);
void luaL_checkstack (lua_State *L, int sz, const char *msg);
void luaL_checktype (lua_State *L, int arg, int t);
void luaL_checkany (lua_State *L, int arg);
int luaL_fileresult (lua_State *L, int stat, const char *fname);
      /* 失败三元组: nil + "path: 原因" + errno（实测 io.open） */
int luaL_execresult (lua_State *L, int stat);
void luaL_traceback (lua_State *L, lua_State *L1, const char *msg, int level);
```

### 元表 / userdata

```c
int luaL_newmetatable (lua_State *L, const char *tname);
void luaL_setmetatable (lua_State *L, const char *tname);
void *luaL_testudata (lua_State *L, int ud, const char *tname);
void *luaL_checkudata (lua_State *L, int ud, const char *tname);
int luaL_getmetafield (lua_State *L, int obj, const char *e);
int luaL_callmeta (lua_State *L, int obj, const char *e);
```

### 缓冲

```c
void luaL_buffinit (lua_State *L, luaL_Buffer *B);
char *luaL_prepbuffsize (luaL_Buffer *B, size_t sz);
void luaL_addlstring/luaL_addstring/luaL_addvalue (luaL_Buffer *B, ...);
void luaL_pushresult (luaL_Buffer *B);
void luaL_pushresultsize (luaL_Buffer *B, size_t sz);
char *luaL_buffinitsize (lua_State *L, luaL_Buffer *B, size_t sz);
void luaL_addgsub (luaL_Buffer *b, const char *s, const char *p, const char *r);
const char *luaL_gsub (lua_State *L, const char *s, const char *p, const char *r);
```

### 引用 / 长度 / 字符串化

```c
int luaL_ref (lua_State *L, int t);       /* LUA_NOREF/LUA_REFNIL 语义 */
void luaL_unref (lua_State *L, int t, int ref);
lua_Integer luaL_len (lua_State *L, int index);
const char *luaL_tolstring (lua_State *L, int idx, size_t *len);
      /* __tostring → tostring 兜底；失败抛错 */
```

### 装载

```c
int luaL_loadfilex (lua_State *L, const char *filename, const char *mode);
      /* BOM/shebang → Nirithy== 壳 → JSON 探测 → 普通装载 */
int luaL_loadbufferx (lua_State *L, const char *buff, size_t size,
                      const char *name, const char *mode);
      /* 前 9 字节 == "Nirithy==" → 解码 → 包 \x1bEnc → 递归自身 */
int luaL_loadstring (lua_State *L, const char *s);
/* 宏: luaL_loadfile / luaL_loadbuffer（mode=NULL） */
```

### 模块 / 注册

```c
void luaL_setfuncs (lua_State *L, const luaL_Reg *l, int nup);
int luaL_getsubtable (lua_State *L, int idx, const char *fname);
void luaL_requiref (lua_State *L, const char *modname, lua_CFunction openf, int glb);
void luaL_pushmodule (lua_State *L, const char *modname, int sizehint);
void luaL_openlib (lua_State *L, const char *libname, const luaL_Reg *l, int nup);
      /* LUA_COMPAT_MODULE */
const char *luaL_findtable (lua_State *L, int idx, const char *fname, int szhint);
```

### 状态

```c
lua_State *luaL_newstate (void);        /* 默认分配器 + luaL_makeseed */
unsigned int luaL_makeseed (lua_State *L);
void luaL_checkversion_ (lua_State *L, lua_Number ver, size_t sz);
```

---

## 三、装载分派流程（`luaL_loadfilex`）

```
fopen → skipBOM/skipcomment（shebang 补 \n）
首字节 == \x1b → freopen "rb" 重读
首字节 == 'N' → 尝试匹配 "Nirithy=="：
   匹配 → 整文件读出 → nirithy_decode（b64）→ 校验 >24 字节
        → 包 "\x1bEnc" → luaL_loadbuffer（f_parser 再走解密）
   不匹配 → fseek 回原位
跳过空白后首字符 == '{' → 整文件读入 → json_to_lua 转 Lua 表构造器
   → luaL_loadbuffer 装载生成代码
否则 → lua_load(getF reader) 常规装载
```

---

## 四、运行验证（实测输出）

脚本 `verify_lauxlib.lua`（前期调研生成、本会话实测，`run_lua.sh`）：

```
bad argument #2 to 'string.sub' (number expected, got string)
...verify_lauxlib.lua:16: boom                    -- luaL_where 前缀
FILE* tostring:	file (0x..)                       -- luaL_tolstring + __tostring
tracemsg / stack traceback: ...                   -- luaL_traceback
dofile 结果:	hello-from-dofile	123
缺文件:	false	cannot open ...no_such_file.lua: No such file or directory
JSON a:	1	 b[2]:	20	 c.name:	lxclua          -- loadfile 直读 JSON
load 执行:	42	nil
语法错误:	nil	tokenpos: 10, Line: 1, LastToken: ''+'', description: ...
nil	...no_such_file.txt: No such file or directory	number   -- fileresult
FILE* type:	userdata	 toclose 支持:	true
require('map') 缓存同一表:	true
```

结论：参数错误格式、位置前缀、`loadfile` 的 JSON 直读、文件错误三元组、
元表注册、require 缓存全部正常。另注意到语法错误消息采用扩展格式
（`tokenpos/Line/LastToken/description`，编译器侧特性）。
`Nirithy==` 壳的端到端验证见 `ldump.md`（`string.dump` 默认带壳 + 篡改拒载）。

---

## 五、与其他模块的关系

- `luaL_loadbuffer(x)` → `lua_load` → `ldo.c f_parser`：壳解码在此层、
  `\x1bEnc` AES 解密在 `lzio.c`、分段校验在 `lundump.c`。
- `json_to_lua` 实现在 `utils/json_parser.c`。
- base64 编码对端在 `stdlib/lstrlib.c`（`string.dump` 的 `envelop`）。
- `luaL_makeseed` 供 `lstate.c lua_newstate`；引用表与注册表经 `lapi.c`。
