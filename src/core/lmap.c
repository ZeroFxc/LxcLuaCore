/*
** $Id: lmap.c $
** Map容器类型实现 - 纯哈希存储
** 与table完全解耦：独立的内存布局、哈希算法、GC回收
** 支持任意可哈希类型作为键
** See Copyright Notice in lua.h
*/

#define LUA_CORE

#include "lprefix.h"

#include <string.h>

#include "lmap.h"
#include "lstate.h"
#include "lmem.h"
#include "lgc.h"
#include "ldo.h"
#include "lvm.h"
#include "lstring.h"


/*
** 哈希函数：对任意Lua类型的键计算哈希值
** 支持数字、字符串、布尔、table、map、userdata、函数、struct等
**
** 算法：Fowler-Noll-Vo (FNV-1a) 变体
** 对指针类型使用地址作为哈希种子
** 对值类型使用其整数值
*/
unsigned int luaM_hashkey (const TValue *key) {
  unsigned int h = 2166136261u;  /* FNV offset basis */
  const unsigned int fnv_prime = 16777619u;

  switch (ttypetag(key)) {
    case LUA_TNIL: {
      /* nil键：使用固定哈希值 */
      h = 0xdeadbeef;
      break;
    }
    case LUA_VFALSE:
    case LUA_VTRUE: {
      /* 布尔键：true->1, false->0 */
      unsigned int val = (unsigned int)(!l_isfalse(key));
      h ^= val;
      h *= fnv_prime;
      break;
    }
    case LUA_VNUMINT: {
      /* 整数键 */
      lua_Integer iv = ivalue(key);
      h ^= (unsigned int)(iv & 0xFFFFFFFFu);
      h *= fnv_prime;
      h ^= (unsigned int)((iv >> 32) & 0xFFFFFFFFu);
      h *= fnv_prime;
      break;
    }
    case LUA_VNUMFLT: {
      /* 浮点数键：取整后哈希 */
      lua_Integer iv = (lua_Integer)fltvalue(key);
      h ^= (unsigned int)(iv & 0xFFFFFFFFu);
      h *= fnv_prime;
      h ^= (unsigned int)((iv >> 32) & 0xFFFFFFFFu);
      h *= fnv_prime;
      break;
    }
    case LUA_VSHRSTR: {
      /* 短字符串键：使用字符串自带的哈希值 */
      const TString *ts = tsvalue(key);
      h = ts->hash;
      break;
    }
    case LUA_VLNGSTR: {
      /* 长字符串键：使用字符串自带的哈希值 */
      const TString *ts = tsvalue(key);
      h = luaS_hashlongstr((TString *)ts);
      break;
    }
    default: {
      /* 引用类型键：使用对象指针地址作为哈希 */
      if (iscollectable(key)) {
        const GCObject *gc = gcvalue(key);
        uintptr_t addr = (uintptr_t)gc;
        h ^= (unsigned int)(addr & 0xFFFFFFFFu);
        h *= fnv_prime;
        h ^= (unsigned int)((addr >> 16) & 0xFFFFFFFFu);
        h *= fnv_prime;
        h ^= (unsigned int)((addr >> 32) & 0xFFFFFFFFu);
        h *= fnv_prime;
      } else {
        /* 其他类型：使用类型标记+指针地址 */
        uintptr_t addr = (uintptr_t)key;
        h ^= (unsigned int)(addr & 0xFFFFFFFFu);
        h *= fnv_prime;
      }
      break;
    }
  }
  return h;
}


/*
** 比较两个键是否相等
** 支持不同类型的键比较
*/
static int key_equals (const TValue *k1, const TValue *k2) {
  int tt1 = rawtt(k1);
  int tt2 = rawtt(k2);

  if (tt1 != tt2)
    return 0;  /* 类型不同，不相等 */

  switch (tt1) {
    case LUA_TNIL:
      return 1;  /* nil == nil */
    case LUA_VFALSE:
    case LUA_VTRUE:
      return !l_isfalse(k1) == !l_isfalse(k2);
    case LUA_VNUMINT:
      return ivalue(k1) == ivalue(k2);
    case LUA_VNUMFLT:
      return fltvalue(k1) == fltvalue(k2);
    case LUA_VSHRSTR:
      /* 短字符串：内联，直接比较指针 */
      return tsvalue(k1) == tsvalue(k2);
    case LUA_VLNGSTR:
      return luaS_eqlngstr(tsvalue(k1), tsvalue(k2));
    default:
      /* 引用类型：比较指针地址 */
      if (iscollectable(k1))
        return gcvalue(k1) == gcvalue(k2);
      return 0;
  }
}


/*
** 创建新的map容器
** 使用GC追踪分配，初始化为空哈希桶
*/
Map *luaM_newmap (lua_State *L) {
  GCObject *o = luaC_newobj(L, LUA_VMAP, sizeof(Map));
  Map *m = gco2map(o);
  m->buckets = NULL;
  m->size = 0;
  m->count = 0;
  return m;
}


/*
** 释放map内存
** 先释放所有链表节点，再释放桶数组，最后释放map对象
*/
void luaM_freemap (lua_State *L, Map *m) {
  if (m->buckets != NULL) {
    unsigned int i;
    for (i = 0; i < m->size; i++) {
      MapNode *node = m->buckets[i];
      while (node != NULL) {
        MapNode *next = node->next;
        luaM_free(L, node);  /* sizeof(MapNode) */
        node = next;
      }
    }
    luaM_freearray(L, m->buckets, m->size);  /* 释放桶数组 */
  }
  luaM_free(L, m);  /* 释放map对象 */
}


/*
** 查找键对应的节点
** 返回节点指针，未找到返回NULL
** 同时返回桶索引
*/
static MapNode *find_node (Map *m, const TValue *key, unsigned int *out_bucket_idx) {
  if (m->size == 0 || m->count == 0)
    return NULL;

  unsigned int h = luaM_hashkey(key);
  unsigned int idx = h & (m->size - 1);
  if (out_bucket_idx) *out_bucket_idx = idx;

  MapNode *node = m->buckets[idx];
  while (node != NULL) {
    if (key_equals(&node->key, key))
      return node;
    node = node->next;
  }
  return NULL;
}


/*
** 扩容/缩容哈希桶
** 始终保持桶数量为2的幂
*/
static void resize_map (lua_State *L, Map *m, unsigned int new_size) {
  if (new_size == 0) {
    if (m->buckets) {
      luaM_freearray(L, m->buckets, m->size);
      m->buckets = NULL;
    }
    m->size = 0;
    return;
  }

  MapNode **new_buckets = luaM_newvector(L, new_size, MapNode *);
  unsigned int i;
  for (i = 0; i < new_size; i++)
    new_buckets[i] = NULL;

  /* 重新哈希所有节点 */
  if (m->buckets != NULL) {
    for (i = 0; i < m->size; i++) {
      MapNode *node = m->buckets[i];
      while (node != NULL) {
        MapNode *next = node->next;
        unsigned int h = luaM_hashkey(&node->key);
        unsigned int new_idx = h & (new_size - 1);
        node->next = new_buckets[new_idx];
        new_buckets[new_idx] = node;
        node = next;
      }
    }
    luaM_freearray(L, m->buckets, m->size);
  }

  m->buckets = new_buckets;
  m->size = new_size;
}


/*
** 确保map有足够的容量
** 负载因子 > 0.75 时扩容
*/
static void ensure_capacity (lua_State *L, Map *m) {
  if (m->size == 0) {
    resize_map(L, m, MAP_INITIAL_BUCKETS);
    return;
  }

  /* 负载因子 = count / size > 0.75 时扩容为2倍 */
  if (m->count * 4 > m->size * 3) {
    resize_map(L, m, m->size * 2);
  }
}


/*
** map下标读取：m[key]
** 返回键对应的值指针，未找到返回NULL
*/
const TValue *luaM_getval (const Map *m, const TValue *key) {
  MapNode *node = find_node((Map *)m, key, NULL);
  if (node != NULL)
    return &node->val;
  return NULL;
}


/*
** map下标赋值：m[key] = val
** 如果键已存在则更新值，否则创建新节点
*/
void luaM_setval (lua_State *L, Map *m, const TValue *key, const TValue *val) {
  unsigned int bucket_idx = 0;
  MapNode *node = find_node(m, key, &bucket_idx);

  if (node != NULL) {
    /* 键已存在，更新值 */
    setobj(L, &node->val, val);
    return;
  }

  /* 键不存在，创建新节点 */
  ensure_capacity(L, m);

  /* 重新计算bucket索引（扩容后可能变化） */
  unsigned int h = luaM_hashkey(key);
  bucket_idx = h & (m->size - 1);

  MapNode *new_node = luaM_new(L, MapNode);

  /* 复制键和值 */
  setobj(L, &new_node->key, key);
  setobj(L, &new_node->val, val);

  /* 插入到链表头部 */
  new_node->next = m->buckets[bucket_idx];
  m->buckets[bucket_idx] = new_node;

  m->count++;

  /* GC屏障：标记新节点引用的对象 */
  if (iscollectable(key))
    luaC_barrierback(L, obj2gco(m), key);
  if (iscollectable(val))
    luaC_barrierback(L, obj2gco(m), val);
}


/*
** 获取map长度（键值对总数）
** 与table不同：map的#直接返回总数量，无数组空洞问题
*/
lua_Unsigned luaM_maplen (lua_State *L, const Map *m) {
  UNUSED(L);
  return (lua_Unsigned)m->count;
}


/*
** map遍历：获取下一个键值对
** 参数key为当前键（首次遍历传NULL），返回下一对键值
** 返回1表示有下一对，返回0表示遍历结束
**
** 遍历顺序：按桶顺序遍历，不保证特定顺序
** 与table的next不同，map的next完全独立实现
*/
int luaM_mapnext (const Map *m, const TValue *key, TValue *next_key, TValue *next_val) {
  if (m->count == 0 || m->size == 0)
    return 0;

  unsigned int start_idx = 0;
  MapNode *start_node = NULL;

  /* nil键表示首次遍历，从头开始 */
  if (key != NULL && !ttisnil(key)) {
    /* 查找当前键所在位置，然后找下一个 */
    unsigned int h = luaM_hashkey(key);
    unsigned int idx = h & (m->size - 1);
    MapNode *node = m->buckets[idx];

    /* 在链表中找到当前键 */
    while (node != NULL) {
      if (key_equals(&node->key, key)) {
        /* 找到当前键，从下一个节点开始 */
        if (node->next != NULL) {
          start_node = node->next;
          start_idx = idx;
        } else {
          start_idx = idx + 1;
        }
        break;
      }
      node = node->next;
    }
    if (node == NULL) {
      /* 键不在map中，从下一个桶开始 */
      start_idx = idx + 1;
    }
  } else {
    /* 首次遍历：从头开始 */
    start_idx = 0;
  }

  /* 从start_idx开始查找下一个节点 */
  if (start_node == NULL) {
    for (; start_idx < m->size; start_idx++) {
      if (m->buckets[start_idx] != NULL) {
        start_node = m->buckets[start_idx];
        break;
      }
    }
  }

  if (start_node == NULL || start_idx >= m->size)
    return 0;

  /* 设置返回的键值对 */
  setobj(NULL, next_key, &start_node->key);
  setobj(NULL, next_val, &start_node->val);
  return 1;
}


/*
** 删除map中的键
** 如果键存在则删除并释放节点内存
*/
void luaM_deletekey (lua_State *L, Map *m, const TValue *key) {
  if (m->size == 0)
    return;

  unsigned int h = luaM_hashkey(key);
  unsigned int idx = h & (m->size - 1);

  MapNode *prev = NULL;
  MapNode *node = m->buckets[idx];

  while (node != NULL) {
    if (key_equals(&node->key, key)) {
      /* 从链表中移除 */
      if (prev != NULL)
        prev->next = node->next;
      else
        m->buckets[idx] = node->next;

      luaM_free(L, node);
      m->count--;
      return;
    }
    prev = node;
    node = node->next;
  }
}


/*
** 清空map所有键值对
*/
void luaM_clearmap (lua_State *L, Map *m) {
  if (m->buckets != NULL) {
    unsigned int i;
    for (i = 0; i < m->size; i++) {
      MapNode *node = m->buckets[i];
      while (node != NULL) {
        MapNode *next = node->next;
        luaM_free(L, node);
        node = next;
      }
      m->buckets[i] = NULL;
    }
    luaM_freearray(L, m->buckets, m->size);
    m->buckets = NULL;
  }
  m->size = 0;
  m->count = 0;
}


/*
** 深拷贝map
** 创建新map并复制所有键值对
*/
Map *luaM_copymap (lua_State *L, const Map *src) {
  Map *dst = luaM_newmap(L);
  if (src->count == 0)
    return dst;

  /* 预分配足够空间 */
  unsigned int new_size = MAP_INITIAL_BUCKETS;
  while (new_size * 3 < src->count * 4)
    new_size *= 2;
  resize_map(L, dst, new_size);

  /* 遍历源map所有节点 */
  unsigned int i;
  for (i = 0; i < src->size; i++) {
    MapNode *node = src->buckets[i];
    while (node != NULL) {
      luaM_setval(L, dst, &node->key, &node->val);
      node = node->next;
    }
  }

  return dst;
}


/*
** 获取map中所有键，压入栈
*/
void luaM_getkeys (lua_State *L, const Map *m) {
  unsigned int i;
  for (i = 0; i < m->size; i++) {
    MapNode *node = m->buckets[i];
    while (node != NULL) {
      setobj2s(L, L->top.p, &node->key);
      L->top.p++;
      node = node->next;
    }
  }
}


/*
** 获取map中所有值，压入栈
*/
void luaM_getvalues (lua_State *L, const Map *m) {
  unsigned int i;
  for (i = 0; i < m->size; i++) {
    MapNode *node = m->buckets[i];
    while (node != NULL) {
      setobj2s(L, L->top.p, &node->val);
      L->top.p++;
      node = node->next;
    }
  }
}