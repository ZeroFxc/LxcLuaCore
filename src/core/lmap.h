/*
** $Id: lmap.h $
** Map容器类型 - 纯哈希存储，独立于table
** 特性：无元表、无数组段、支持任意类型键
** See Copyright Notice in lua.h
*/

#ifndef lmap_h
#define lmap_h

#include "lobject.h"


/* 前向声明 */
struct global_State;


/*
** map哈希函数：支持任意可哈希类型作为键
** 数字、字符串、布尔、table、map、userdata、函数均可作为键
*/
LUAI_FUNC unsigned int luaM_hashkey (const TValue *key);

/* 创建新的map容器 */
LUAI_FUNC Map *luaM_newmap (lua_State *L);

/* 释放map内存 */
LUAI_FUNC void luaM_freemap (lua_State *L, Map *m);

/* map下标读取：m[key] */
LUAI_FUNC const TValue *luaM_getval (const Map *m, const TValue *key);

/* map下标赋值：m[key] = val */
LUAI_FUNC void luaM_setval (lua_State *L, Map *m, const TValue *key, const TValue *val);

/* 获取map长度（键值对总数） */
LUAI_FUNC lua_Unsigned luaM_maplen (lua_State *L, const Map *m);

/* map遍历：获取下一个键值对 */
LUAI_FUNC int luaM_mapnext (const Map *m, const TValue *key, TValue *next_key, TValue *next_val);

/* 删除map中的键 */
LUAI_FUNC void luaM_deletekey (lua_State *L, Map *m, const TValue *key);

/* 清空map所有键值对 */
LUAI_FUNC void luaM_clearmap (lua_State *L, Map *m);

/* 深拷贝map */
LUAI_FUNC Map *luaM_copymap (lua_State *L, const Map *src);

/* 获取map所有键 */
LUAI_FUNC void luaM_getkeys (lua_State *L, const Map *m);

/* 获取map所有值 */
LUAI_FUNC void luaM_getvalues (lua_State *L, const Map *m);

#endif