/*
** $Id: lmaplib.c $
** Map标准库 - map容器的工具函数
** 与table库完全隔离：table库函数不接收map，map库函数不接收table
** See Copyright Notice in lua.h
*/

#define LUA_LIB

#include "lprefix.h"

#include <string.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "lstate.h"
#include "lobject.h"
#include "lgc.h"
#include "lmap.h"


/*
** 辅助函数：获取栈上指定索引处值的TValue指针
** 用于访问内部API（luaM_setval等需要TValue*）
*/
static TValue *getstackval (lua_State *L, int idx) {
  if (idx > 0)
    return s2v(L->ci->func.p + idx);
  else
    return s2v(L->top.p + idx);
}


/*
** 辅助函数：获取索引处map指针，类型检查
*/
static Map *check_map (lua_State *L, int idx) {
  luaL_checktype(L, idx, LUA_TMAP);
  return mapvalue(getstackval(L, idx));
}


/*
** map.keys(map) -> 返回包含map中所有键的table
** 仅在参数为map时有效，传入table或其他类型直接报错
*/
static int lmaplib_keys (lua_State *L) {
  Map *m = check_map(L, 1);
  lua_createtable(L, m->count, 0);
  unsigned int i;
  lua_Integer idx = 1;
  for (i = 0; i < m->size; i++) {
    MapNode *node = m->buckets[i];
    while (node != NULL) {
      setobj2s(L, L->top.p, &node->key);
      L->top.p++;
      lua_rawseti(L, -2, idx++);
      node = node->next;
    }
  }
  return 1;
}


/*
** map.values(map) -> 返回包含map中所有值的table
** 仅在参数为map时有效
*/
static int lmaplib_values (lua_State *L) {
  Map *m = check_map(L, 1);
  lua_createtable(L, m->count, 0);
  unsigned int i;
  lua_Integer idx = 1;
  for (i = 0; i < m->size; i++) {
    MapNode *node = m->buckets[i];
    while (node != NULL) {
      setobj2s(L, L->top.p, &node->val);
      L->top.p++;
      lua_rawseti(L, -2, idx++);
      node = node->next;
    }
  }
  return 1;
}


/*
** map.clear(map) -> 清空map所有键值对
*/
static int lmaplib_clear (lua_State *L) {
  Map *m = check_map(L, 1);
  luaM_clearmap(L, m);
  return 0;
}


/*
** map.remove(map, key) -> 删除指定键
** 返回true表示删除成功，false表示键不存在
** 注：函数名用remove而非delete，因为delete是LXCLUA关键字
*/
static int lmaplib_remove (lua_State *L) {
  Map *m = check_map(L, 1);
  luaL_checkany(L, 2);
  const TValue *key = getstackval(L, 2);
  const TValue *val = luaM_getval(m, key);
  if (val != NULL) {
    luaM_deletekey(L, m, key);
    lua_pushboolean(L, 1);
  } else {
    lua_pushboolean(L, 0);
  }
  return 1;
}


/*
** map.copy(map) -> 返回map的深拷贝
*/
static int lmaplib_copy (lua_State *L) {
  Map *src = check_map(L, 1);
  Map *dst = luaM_copymap(L, src);
  setmapvalue2s(L, L->top.p, dst);
  L->top.p++;
  return 1;
}


/*
** map.size(map) -> 返回map中键值对数量
*/
static int lmaplib_size (lua_State *L) {
  Map *m = check_map(L, 1);
  lua_pushinteger(L, (lua_Integer)m->count);
  return 1;
}


/*
** map.has(map, key) -> 检查map中是否存在指定键
*/
static int lmaplib_has (lua_State *L) {
  Map *m = check_map(L, 1);
  luaL_checkany(L, 2);
  const TValue *key = getstackval(L, 2);
  const TValue *val = luaM_getval(m, key);
  lua_pushboolean(L, val != NULL);
  return 1;
}


/*
** map.get(map, key [, default]) -> 获取键对应的值
** 如果键不存在返回default（默认为nil）
*/
static int lmaplib_get (lua_State *L) {
  Map *m = check_map(L, 1);
  luaL_checkany(L, 2);
  const TValue *key = getstackval(L, 2);
  const TValue *val = luaM_getval(m, key);
  if (val != NULL) {
    setobj2s(L, L->top.p, val);
    L->top.p++;
  } else {
    /* 键不存在，返回默认值或nil */
    if (lua_isnone(L, 3)) {
      lua_pushnil(L);
    } else {
      lua_pushvalue(L, 3);
    }
  }
  return 1;
}


/*
** map.set(map, key, value) -> 设置键值对
** 返回map自身（支持链式调用）
*/
static int lmaplib_set (lua_State *L) {
  Map *m = check_map(L, 1);
  luaL_checkany(L, 2);
  luaL_checkany(L, 3);
  luaM_setval(L, m, getstackval(L, 2), getstackval(L, 3));
  lua_pushvalue(L, 1);  /* 返回map自身 */
  return 1;
}


/*
** map.merge(map1, map2) -> 合并两个map
** 将map2的所有键值对复制到map1中
** 返回map1（支持链式调用）
*/
static int lmaplib_merge (lua_State *L) {
  Map *dst = check_map(L, 1);
  Map *src = check_map(L, 2);
  unsigned int i;
  for (i = 0; i < src->size; i++) {
    MapNode *node = src->buckets[i];
    while (node != NULL) {
      luaM_setval(L, dst, &node->key, &node->val);
      node = node->next;
    }
  }
  lua_pushvalue(L, 1);
  return 1;
}


/* map库函数列表 */
static const luaL_Reg maplib[] = {
  {"keys", lmaplib_keys},
  {"values", lmaplib_values},
  {"clear", lmaplib_clear},
  {"remove", lmaplib_remove},
  {"copy", lmaplib_copy},
  {"size", lmaplib_size},
  {"has", lmaplib_has},
  {"get", lmaplib_get},
  {"set", lmaplib_set},
  {"merge", lmaplib_merge},
  {NULL, NULL}
};


/*
** 注册map库
** luaopen_map(L) - 被linit.c调用
*/
LUAMOD_API int luaopen_map (lua_State *L) {
  luaL_checkversion(L);
  lua_createtable(L, 0, sizeof(maplib)/sizeof(maplib[0]) - 1);
  luaL_setfuncs(L, maplib, 0);
  return 1;
}