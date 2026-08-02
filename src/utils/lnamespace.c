/*
** $Id: lnamespace.c $
** Namespace Implementation
** See Copyright Notice in lua.h
*/

#define lnamespace_c
#define LUA_CORE

#include "lprefix.h"

#include <stdio.h>

#include "lua.h"

#include "lgc.h"
#include "lnamespace.h"
#include "ldo.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"

Namespace *luaN_new (lua_State *L, TString *name) {
  GCObject *o = luaC_newobj(L, LUA_VNAMESPACE, sizeof(Namespace));
  Namespace *ns = gco2ns(o);
  ns->name = name;
  ns->gclist = NULL;
  ns->data = NULL;
  ns->using_next = NULL;

  /* Anchor ns on stack to prevent collection during table allocation */
  setnsvalue(L, s2v(L->top.p), ns);
  luaD_inctop(L);

  ns->data = luaH_new(L);
  
  /* 设置 data 表的 __index 元表指向全局表，使 namespace 可访问全局变量 */
  Table *mt = luaH_new(L);
  sethvalue2s(L, L->top.p, mt);
  luaD_inctop(L);
  {
    TValue key;
    setsvalue(L, &key, G(L)->tmname[TM_INDEX]);  /* key = "__index" */
    Table *registry = hvalue(&G(L)->l_registry);
    const TValue *gt = &registry->array[LUA_RIDX_GLOBALS - 1];
    luaH_set(L, mt, &key, cast(TValue *, gt));  /* mt.__index = 全局表 */
    mt->flags &= ~cast_byte(1u << TM_INDEX);  /* 清除 TM_INDEX 缓存标志，使 fasttm 能够找到 __index */
  }
  ns->data->metatable = (GCObject *)mt;
  printf("[DEBUG] luaN_new: ns->data=%p, mt=%p, ns->data->metatable=%p, __index points to global table\n",
         (void*)ns->data, (void*)mt, (void*)ns->data->metatable);
  L->top.p--; /* 弹出 mt */
  
  /* The table is not reachable yet, but ns is anchored.
     We need to ensure barrier if ns was black (it's new so it's white). */

  L->top.p--; /* Unanchor */

  return ns;
}

void luaN_free (lua_State *L, Namespace *ns) {
  luaM_free(L, ns);
}
