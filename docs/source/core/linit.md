# core/linit.c — 标准库与扩展库注册

> 职责：`luaL_openlibs` / `luaL_openselectedlibs` 的实现——把全部标准库与
> LXCLUA 扩展库按 `luaL_Reg` 表逐个 `luaL_requiref` 进全局表（或 PRELOAD）。

---

## 一、特性介绍

1. **双库表**：`stdlibs[]`（供 `luaL_openselectedlibs` 位掩码选择，
   与 `LUA_<name>K` 常量顺序一致）与 `loadedlibs[]`（`luaL_openlibs` 全量加载）。
   两表内容基本一致，仅排列顺序略有差异（loadedlibs 把 table 提前、debug 靠后）。
2. **库总数**：每张表 **39 项**（`process` 仅 `__linux__`，`wasmtime` 非 `__EMSCRIPTEN__`），
   覆盖标准 5.5 库 + 约 28 个扩展库。
3. **日志增强**：`luaL_openlibs` 每打开一个库输出 `LUA_LOGI` 进度
   （`[i/total] Opening/Completed library: name`），便于排查库初始化崩溃点。

---

## 二、注册库全表（从源码 `stdlibs[]` 抄录）

**标准库（经 `LUA_*LIBNAME` 宏命名）**：

| 全局名 | 打开函数 | 宏 |
|---|---|---|
| `_G` | `luaopen_base` | `LUA_GNAME` |
| `package` | `luaopen_package` | `LUA_LOADLIBNAME` |
| `coroutine` | `luaopen_coroutine` | `LUA_COLIBNAME` |
| `debug` | `luaopen_debug` | `LUA_DBLIBNAME` |
| `io` | `luaopen_io` | `LUA_IOLIBNAME` |
| `math` | `luaopen_math` | `LUA_MATHLIBNAME` |
| `patch` | `luaopen_patch` | `LUA_PATCHLIBNAME` |
| `os` | `luaopen_os` | `LUA_OSLIBNAME` |
| `string` | `luaopen_string` | `LUA_STRLIBNAME` |
| `table` | `luaopen_table` | `LUA_TABLIBNAME` |
| `utf8` | `luaopen_utf8` | `LUA_UTF8LIBNAME` |
| `bool` | `luaopen_bool` | `LUA_BOOLIBNAME` |
| `userdata` | `luaopen_userdata` | `LUA_UDATALIBNAME` |
| `vm` | `luaopen_vm` | `LUA_VMLIBNAME` |
| `bit` | `luaopen_bit` | `LUA_BITLIBNAME` |
| `ptr` | `luaopen_ptr` | `LUA_PTRLIBNAME` |
| `struct` | `luaopen_struct` | `LUA_STRUCTLIBNAME` |
| `fs` | `luaopen_fs` | `LUA_FSLIBNAME` |
| `lexer` | `luaopen_lexer` | `LUA_LEXERLIBNAME` |

**扩展库（字面量命名）**：

| 全局名 | 打开函数 | 来源文件 |
|---|---|---|
| `bit32` | `luaopen_bit` | （bit 的别名） |
| `thread` | `luaopen_thread` | stdlib/lthreadlib.c |
| `http` | `luaopen_http` | utils/libhttp.c |
| `vmprotect` | `luaopen_vmprotect` | vm/lvmpro.c |
| `tcc` | `luaopen_tcc` | （TCC 即时编译） |
| `ByteCode` | `luaopen_ByteCode` | vm/lbytecode.c |
| `wasm3` | `luaopen_wasm3` | wasm/lwasm3.c |
| `wasmtime` | `luaopen_wasmtime` | wasm/lwasmtime.c（非 Emscripten） |
| `quickjs` | `luaopen_quickjs` | bin/lquickjs 对应库 |
| `asyncio` | `luaopen_asyncio` | utils/laio.c |
| `vmcustom` | `luaopen_vmcustom` | vm/lvmustom.c |
| `nativevm` | `luaopen_nativevm` | vm/lnativevm.c |
| `nativeparser` | `luaopen_nativeparser` | vm/lnativeparser.c |
| `translator` | `luaopen_translator` | utils/ltranslator.c |
| `logtable` | `luaopen_logtable` | utils/logtable.c |
| `crypto` | `luaopen_crypto` | utils/lcrypto.c |
| `uuid` | `luaopen_uuid` | utils/luuid.c |
| `rsa` | `luaopen_rsa` | utils/lrsa.c |
| `ecc` | `luaopen_ecc` | utils/lecc.c |
| `map` | `luaopen_map` | core/lmap.c |
| `ast` | `luaopen_ast` | stdlib/lastlib.c |
| `process` | `luaopen_process` | stdlib/lproclib.c（仅 `__linux__`） |

---

## 三、关键函数（准确签名）

```c
/* 全量打开：遍历 loadedlibs，逐个 luaL_requiref 入全局表。
   带 LUA_LOGI 进度日志。无返回值。 */
LUALIB_API void luaL_openlibs (lua_State *L);

/* 选择性打开：load 位掩码命中的库立即 require；preload 位掩码命中的
   只塞进 PRELOAD 表（require 时再打开）。掩码位与 stdlibs[] 顺序对应，
   即 LUA_<libname>K 常量。 */
LUALIB_API void luaL_openselectedlibs (lua_State *L, int load, int preload);
```

调用方式要点：

- 嵌入方只需 `luaL_openlibs(L)` 即得全部能力；裁剪体积用
  `luaL_openselectedlibs` + 位掩码（`LUA_LOADLIBK` 等常量在 `lualib.h`）。
- 掩码按 `int` 逐位左移，库数超过 32 时高位库不可经此机制选择
  （当前 39 项已超 32 位，`stdlibs` 后段 7 个库无法用掩码单独选——待确认是否有意）。

---

## 四、运行验证（实测输出）

脚本 `v_linit.lua`（`run_lua.sh`，Windows 构建）：

```
PRESENT(40):	ByteCode _G ast asyncio bit bit32 bool coroutine crypto debug
 ecc fs http io lexer logtable map math nativeparser nativevm os
 package patch ptr quickjs rsa string struct table tcc thread translator
 userdata utf8 uuid vm vmcustom vmprotect wasm3 wasmtime
MISSING(1):	process
type check:	table	table	table	table
```

结论：40 个全局名在位（含 `_G` 自身与别名 `bit32`），唯一缺席的是
`process`（`#ifdef __linux__` 限定，Windows 构建不注册）；`wasmtime` 在册。
与 `stdlibs[]` 源码清单逐一吻合。

---

## 五、与其他模块的关系

- 每个 `luaopen_*` 的实现即对应库文档（见上表来源文件）。
- `luaL_requiref` 语义在 `lauxlib.c`；PRELOAD 表机制在 `loadlib.c`。
- `LUA_*LIBNAME`/`LUA_*K` 常量定义在 `lualib.h`。
