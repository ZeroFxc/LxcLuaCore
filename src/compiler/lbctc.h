#ifndef lbctc_h
#define lbctc_h

#include "lua.h"

/* ============================================================
** LBCTC (Lua Bytecode-to-C) 转译辅助函数声明
** 所有函数均为 LUA_API，由 liblxclua.a 导出，转译后的 C 代码直接调用
** ============================================================ */

/* 函数 prologue：构造变长参数表 */
LUA_API void lua_tcc_prologue(lua_State *L, int nparams, int maxstack);

/* Upvalue 表字段操作 */
LUA_API void lua_tcc_gettabup(lua_State *L, int upval, const char *k, int dest);
LUA_API void lua_tcc_settabup(lua_State *L, int upval, const char *k, int val_idx);

/* 常量加载 helper（基于字符串常量名） */
LUA_API void lua_tcc_loadk_str(lua_State *L, int dest, const char *s);
LUA_API void lua_tcc_loadk_int(lua_State *L, int dest, lua_Integer v);
LUA_API void lua_tcc_loadk_flt(lua_State *L, int dest, lua_Number v);

/* in 操作符 */
LUA_API int lua_tcc_in(lua_State *L, int val_idx, int container_idx);

/* 调用参数/结果保存 */
LUA_API void lua_tcc_push_args(lua_State *L, int start_reg, int count);
LUA_API void lua_tcc_store_results(lua_State *L, int start_reg, int count);

/* Map (原生 Map) 操作：OP_NEWMAP / OP_MAPGET / OP_MAPSET */
LUA_API void lua_tcc_newmap(lua_State *L);
LUA_API void lua_tcc_mapget(lua_State *L);
LUA_API void lua_tcc_mapset(lua_State *L);

/* Trait 操作包装 */
LUA_API void lua_tcc_settraitflag(lua_State *L, int idx);
LUA_API void lua_tcc_settraitrequire(lua_State *L, int idx, const char *name, int nparams);
LUA_API void lua_tcc_usetrait(lua_State *L, int class_idx, int trait_idx);
LUA_API void lua_tcc_staticinit(lua_State *L, int class_idx);

/* 表合并操作（LXCLUA 扩展语法） */
LUA_API void lua_tcc_merge(lua_State *L);

/* 正则字面量包装 */
LUA_API void lua_tcc_regex(lua_State *L, const char *data);

/* Async/Await 同步等待包装 */
LUA_API void lua_tcc_await(lua_State *L, int val_idx, int dest);

/* 自定义 Opcode 分发 */
LUA_API int lua_tcc_custom(lua_State *L, int opcode);

/* 接口混淆：返回乱序函数指针数组 */
LUA_API void *lua_tcc_get_interface(lua_State *L, int seed);

/* === 表/Map 通用索引包装 ===
** 对 Map 类型使用原生 Map 查找/插入，对普通 Table 使用标准 Lua C API。
** 用于 LBCTC 转译生成的 C 代码，支持通用 t[k]/t[k]=v 语法。
*/
LUA_API int  lua_tcc_gettable(lua_State *L, int idx);
LUA_API void lua_tcc_settable(lua_State *L, int idx);
LUA_API int  lua_tcc_getfield(lua_State *L, int idx, const char *k);
LUA_API void lua_tcc_setfield(lua_State *L, int idx, const char *k);
LUA_API int  lua_tcc_geti(lua_State *L, int idx, lua_Integer n);
LUA_API void lua_tcc_seti(lua_State *L, int idx, lua_Integer n);

/* Lua 模块入口（由 lxclua 标准库注册） */
int luaopen_tcc(lua_State *L);

#endif
