/**
 * @file lclass.c
 * @brief Lua object-oriented system implementation.
 */

#define lclass_c
#define LUA_CORE

#include "lprefix.h"

#include <stdio.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"
#include "lapi.h"
#include "lclass.h"
#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"
#include "lvm.h"
#include "lstruct.h"


/*
** =====================================================================
** 内部辅助函数
** =====================================================================
*/

static TValue *index2value_helper (lua_State *L, int idx) {
  CallInfo *ci = L->ci;
  if (idx > 0) {
    StkId o = ci->func.p + idx;
    api_check(L, idx <= ci->top.p - (ci->func.p + 1), "unacceptable index");
    if (o >= L->top.p) return &G(L)->nilvalue;
    else return s2v(o);
  }
  else if (idx == LUA_REGISTRYINDEX)
    return &G(L)->l_registry;
  else {
      /* assume upvalue or other pseudo indices not handled here for now in lclass */
      return &G(L)->nilvalue;
  }
}

/*
** 获取绝对栈索引
** 参数：
**   L - Lua状态机
**   idx - 栈索引（可以是负数）
** 返回值：
**   绝对栈索引
*/
static int absindex(lua_State *L, int idx) {
  if (idx > 0 || idx <= LUA_REGISTRYINDEX)
    return idx;
  return cast_int(L->top.p - L->ci->func.p) + idx;
}


/*
** 在表中设置字符串键的布尔值（使用rawset避免触发元方法）
** 参数：
**   L - Lua状态机
**   t_idx - 表在栈中的索引
**   key - 键名
**   value - 布尔值
*/
static void setboolfield(lua_State *L, int t_idx, const char *key, int value) {
  t_idx = absindex(L, t_idx);
  lua_pushstring(L, key);
  lua_pushboolean(L, value);
  lua_rawset(L, t_idx);
}


/*
** 在表中设置字符串键的字符串值（使用rawset避免触发元方法）
** 参数：
**   L - Lua状态机
**   t_idx - 表在栈中的索引
**   key - 键名
**   value - 字符串值
*/
static void setstrfield(lua_State *L, int t_idx, const char *key, const char *value) {
  t_idx = absindex(L, t_idx);
  lua_pushstring(L, key);
  lua_pushstring(L, value);
  lua_rawset(L, t_idx);
}


/*
** 检查表是否有指定的布尔标记
** 参数：
**   L - Lua状态机
**   t_idx - 表在栈中的索引
**   key - 标记键名
** 返回值：
**   标记值（0或1）
*/
static int checkflag(lua_State *L, int t_idx, const char *key) {
  int result;
  t_idx = absindex(L, t_idx);
  lua_getfield(L, t_idx, key);
  result = lua_toboolean(L, -1);
  lua_pop(L, 1);
  return result;
}


/*
** 检查表是否有指定的布尔标记（使用rawget避免触发元方法）
** 参数：
**   L - Lua状态机
**   t_idx - 表在栈中的索引
**   key - 标记键名
** 返回值：
**   标记值（0或1）
*/
static int checkflag_raw(lua_State *L, int t_idx, const char *key) {
  int result;
  t_idx = absindex(L, t_idx);
  lua_pushstring(L, key);
  lua_rawget(L, t_idx);
  result = lua_toboolean(L, -1);
  lua_pop(L, 1);
  return result;
}


/*
** 复制表的所有键值对到另一个表
** 参数：
**   L - Lua状态机
**   src_idx - 源表索引
**   dst_idx - 目标表索引
*/
static void copytable(lua_State *L, int src_idx, int dst_idx) {
  src_idx = absindex(L, src_idx);
  dst_idx = absindex(L, dst_idx);
  
  lua_pushnil(L);  /* 第一个键 */
  while (lua_next(L, src_idx) != 0) {
    /* 栈顶: value, key */
    lua_pushvalue(L, -2);  /* 复制key */
    lua_pushvalue(L, -2);  /* 复制value */
    lua_settable(L, dst_idx);
    lua_pop(L, 1);  /* 移除value，保留key用于下一次迭代 */
  }
}


/*
** =====================================================================
** 类元方法实现
** =====================================================================
*/

/*
** 类的__call元方法 - 用于创建实例
** 语法: local obj = ClassName(args...)
** 参数：
**   L - Lua状态机
** 返回值：
**   1 - 返回新创建的对象
*/

/* 前向声明 */
static int luaC_specialize(lua_State *L, int class_idx, int type_args_idx);
static int luaC_clone_wrap(lua_State *L);

static int class_call(lua_State *L) {
  int nargs = lua_gettop(L) - 1;  /* 排除类本身 */
  LUA_LOGI("[CLASS] class_call ENTRY nargs=%d top=%d", nargs, lua_gettop(L));

  /* 检查第一个参数是否是类 */
  if (!luaC_isclass(L, 1)) {
    LUA_LOGI("[CLASS] class_call: NOT a class, error");
    luaL_error(L, "attempt to call a non-class value");
    return 0;
  }

  /* 检查是否是 singleton 类，如果有缓存实例则直接返回 */
  lua_pushstring(L, CLASS_KEY_FLAGS);
  lua_rawget(L, 1);
  if (lua_isinteger(L, -1)) {
    int flags = (int)lua_tointeger(L, -1);
    if (flags & CLASS_FLAG_SINGLETON) {
      lua_pop(L, 1);  /* 弹出 flags */
      /* 检查是否有缓存的单例实例 */
      lua_pushstring(L, CLASS_KEY_SINGLETON_INST);
      lua_rawget(L, 1);
      if (!lua_isnil(L, -1)) {
        /* 已有缓存实例，直接返回 */
        return 1;
      }
      lua_pop(L, 1);  /* 弹出 nil */
      /* 创建新实例 */
      luaC_newobject(L, 1, nargs);
      /* 缓存实例 */
      lua_pushstring(L, CLASS_KEY_SINGLETON_INST);
      lua_pushvalue(L, -2);  /* 复制实例 */
      lua_rawset(L, 1);
      return 1;
    }
  }
  lua_pop(L, 1);  /* 弹出 flags */

  /* 检查是否是泛型特化调用：ClassName(Type) */
  /* 如果参数是类/表，且当前类有类型参数，则进行特化 */
  if (nargs == 1) {
    lua_pushstring(L, CLASS_KEY_TYPEPARAMS);
    lua_rawget(L, 1);
    if (lua_istable(L, -1)) {
      lua_pop(L, 1);  /* 弹出 __typeparams */
      /* 检查参数是否是类或表（类型参数） */
      if (luaC_isclass(L, 2) || lua_istable(L, 2)) {
        /* 创建类型参数列表 */
        lua_newtable(L);
        lua_pushvalue(L, 2);  /* 复制类型参数 */
        lua_rawseti(L, -2, 1);
        luaC_specialize(L, 1, lua_gettop(L));
        /* 返回特化类 */
        return 1;
      }
    } else {
      lua_pop(L, 1);  /* 弹出非表的 __typeparams */
    }
  }

  /* 创建新对象实例 */
  LUA_LOGI("[CLASS] class_call: creating object, nargs=%d", nargs);
  luaC_newobject(L, 1, nargs);
  LUA_LOGI("[CLASS] class_call: object created, top=%d", lua_gettop(L));
  return 1;
}

/*
 * 默认构造函数（当用户未自定义 static function new 时由 class_index 兜底返回的 new 函数）
 * 语义与 class_call 完全一致：直接调用类的 __call 即 luaC_newobject（使用 upvalue 中的类）。
 * 用法：ClassName.new(...) 等价于 ClassName(...)。
 * 参数：
 *   L - Lua 状态机；upvalue 1 = 类表
 *   栈: [1..n] = 构造参数
 * 返回：1 = 新创建的对象
 */
static int class_default_new(lua_State *L) {
  int nargs = lua_gettop(L);
  /* 把 upvalue 的类（upvalueindex(1) 压到栈底，参数整体右移，然后交给 luaC_newobject */
  lua_pushvalue(L, lua_upvalueindex(1));
  lua_insert(L, 1);
  if (!luaC_isclass(L, 1)) {
    luaL_error(L, "default new: corrupted class upvalue");
    return 0;
  }
  /* 检查是否是 singleton 类，如果有缓存实例则直接返回 */
  lua_pushstring(L, CLASS_KEY_FLAGS);
  lua_rawget(L, 1);
  if (lua_isinteger(L, -1)) {
    int flags = (int)lua_tointeger(L, -1);
    if (flags & CLASS_FLAG_SINGLETON) {
      lua_pop(L, 1);  /* 弹出 flags */
      lua_pushstring(L, CLASS_KEY_SINGLETON_INST);
      lua_rawget(L, 1);
      if (!lua_isnil(L, -1)) {
        return 1;
      }
      lua_pop(L, 1);  /* 弹出 nil */
      luaC_newobject(L, 1, nargs);
      /* 缓存实例 */
      lua_pushstring(L, CLASS_KEY_SINGLETON_INST);
      lua_pushvalue(L, -2);
      lua_rawset(L, 1);
      return 1;
    }
  }
  lua_pop(L, 1);  /* 弹出 flags */
  luaC_newobject(L, 1, nargs);
  return 1;
}


static const char* get_class_name_str(lua_State *L, int class_idx);

/*
** 类的__index元方法 - 用于访问类成员
** 参数：
**   L - Lua状态机
** 返回值：
**   1 - 返回找到的值或nil
*/
static int class_index(lua_State *L) {
  /* 栈: [1]=类表, [2]=键 */
  
  /* 对于 'new' 键，优先查找 STATICS（静态 new 方法），而非 METHODS（init 别名） */
  {
    const char *kstr = lua_tostring(L, 2);
    if (kstr && strcmp(kstr, CLASS_KEY_NEW) == 0) {
      /* 先查 STATICS.new */
      lua_pushstring(L, CLASS_KEY_STATICS);
      lua_rawget(L, 1);
      if (lua_istable(L, -1)) {
        lua_pushstring(L, CLASS_KEY_NEW);
        lua_rawget(L, -2);
        if (!lua_isnil(L, -1)) {
          lua_remove(L, -2);  /* 移除 STATICS 表 */
          return 1;  /* 返回静态 new 方法 */
        }
        lua_pop(L, 1);
      }
      lua_pop(L, 1);
      /* STATICS.new 不存在，继续查 METHODS.new */
    }
  }
  
  /* 首先在类自身的方法表中查找（使用rawget避免递归） */
  lua_pushstring(L, CLASS_KEY_METHODS);
  lua_rawget(L, 1);
  if (lua_istable(L, -1)) {
    lua_pushvalue(L, 2);  /* 键 */
    lua_rawget(L, -2);
    if (!lua_isnil(L, -1)) {
      return 1;  /* 找到了 */
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  
  /* 然后在静态成员表中查找 */
  lua_pushstring(L, CLASS_KEY_STATICS);
  lua_rawget(L, 1);
  if (lua_istable(L, -1)) {
    lua_pushvalue(L, 2);  /* 键 */
    lua_rawget(L, -2);
    if (!lua_isnil(L, -1)) {
      return 1;  /* 找到了 */
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);

  /*
   * 默认 new 构造函数兜底：用户访问 ClassName.new 但 METHODS/STATICS.new 都未定义时，
   * 若 METHODS.__init__ 存在，则自动返回一个默认构造闭包（等价 ClassName(...) 语法）。
   * class_newindex 和 luaC_setmethod 中已恢复 new<->__init__ 别名同步，
   * 此兜底用于处理别名同步未覆盖的边界场景（如从外部路径设置 init）。
   */
  {
    const char *kstr = lua_tostring(L, 2);
    if (kstr && strcmp(kstr, "new") == 0) {
      /* 先检查 METHODS.new 是否已存在（别名同步可能已设置），
         如果已存在则直接返回，不生成兜底闭包 */
      lua_pushstring(L, CLASS_KEY_METHODS);
      lua_rawget(L, 1);
      if (lua_istable(L, -1)) {
        lua_pushliteral(L, "new");
        lua_rawget(L, -2);
        if (!lua_isnil(L, -1)) {
          lua_remove(L, -2);  /* 移除 METHODS 表 */
          return 1;
        }
        lua_pop(L, 1);  /* 弹出 nil */
      }
      lua_pop(L, 1);  /* 弹出 METHODS 表或非表值 */

      /* 按 MRO 顺序检查 METHODS.init 或 METHODS.__init__ 是否存在 */
      int has_init = 0;
      lua_pushstring(L, CLASS_KEY_MRO);
      lua_rawget(L, 1);
      if (lua_istable(L, -1)) {
        int mro_len = (int)luaL_len(L, -1);
        for (int mi = 1; mi <= mro_len && !has_init; mi++) {
          lua_rawgeti(L, -1, mi);  /* 获取 MRO 中的第 mi 个类 */
          if (!lua_istable(L, -1)) {
            lua_pop(L, 1);
            continue;
          }
          int mro_cls = lua_gettop(L);
          lua_pushstring(L, CLASS_KEY_METHODS);
          lua_rawget(L, mro_cls);
          if (lua_istable(L, -1)) {
            /* 优先查找 init */
            lua_pushstring(L, CLASS_KEY_INIT);
            lua_rawget(L, -2);
            if (lua_isfunction(L, -1)) { has_init = 1; lua_pop(L, 3); break; }
            lua_pop(L, 1);
            /* 兼容旧键名 __init__ */
            lua_pushstring(L, CLASS_KEY_INIT_LEGACY);
            lua_rawget(L, -2);
            if (lua_isfunction(L, -1)) { has_init = 1; lua_pop(L, 3); break; }
            lua_pop(L, 1);
          }
          lua_pop(L, 2);  /* 弹出 METHODS 和 mro_cls */
        }
      }
      lua_pop(L, 1);  /* 弹出 MRO 表 */

      if (has_init) {
        /* 生成默认 new 函数：return Class(...) */
        lua_pushvalue(L, 1);  /* upvalue 1: class */
        lua_pushcclosure(L, class_default_new, 1);
        return 1;
      }
    }
  }

  /* 按 MRO 顺序在父类中查找 */
  lua_pushstring(L, CLASS_KEY_MRO);
  lua_rawget(L, 1);
  if (lua_istable(L, -1)) {
    int mro_len = (int)luaL_len(L, -1);
    /* 从 MRO 位置 2 开始，跳过类自身（已在前面检查过） */
    for (int i = 2; i <= mro_len; i++) {
      lua_rawgeti(L, -1, i);  /* 获取 MRO 中的第 i 个类 */
      if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        continue;
      }
      int mro_class_idx = lua_gettop(L);
      /* 检查该类的 METHODS 表 */
      lua_pushstring(L, CLASS_KEY_METHODS);
      lua_rawget(L, mro_class_idx);
      if (lua_istable(L, -1)) {
        lua_pushvalue(L, 2);  /* 键 */
        lua_rawget(L, -2);
        if (!lua_isnil(L, -1)) {
          /* 找到了，清理栈并返回 */
          lua_remove(L, -2);  /* 移除 METHODS 表 */
          lua_remove(L, -2);  /* 移除 mro_class */
          lua_remove(L, -2);  /* 移除 MRO 表 */
          return 1;
        }
        lua_pop(L, 1);
      }
      lua_pop(L, 2);  /* 弹出 METHODS 和 mro_class */
    }
  }
  lua_pop(L, 1);  /* 弹出 MRO 表 */

  lua_pushnil(L);
  return 1;
}


/*
** 类的__newindex元方法 - 用于设置类成员
** 参数：
**   L - Lua状态机
** 返回值：
**   0
*/
static int class_newindex(lua_State *L) {
  /* 栈: [1]=类表, [2]=键, [3]=值 */
  
  const char *kstr = lua_tostring(L, 2);
  int is_new_key = (kstr && strcmp(kstr, "new") == 0);
  int is_init_key = (kstr && (strcmp(kstr, CLASS_KEY_INIT) == 0 || strcmp(kstr, CLASS_KEY_INIT_LEGACY) == 0));
  int is_legacy_init = (kstr && strcmp(kstr, CLASS_KEY_INIT_LEGACY) == 0);

  /* 如果值是函数，设置到方法表（使用rawget/rawset避免递归） */
  if (lua_isfunction(L, 3)) {
    /* 如果有父类，检查是否在重写 final 方法 */
    lua_pushstring(L, CLASS_KEY_PARENT);
    lua_rawget(L, 1);
    if (lua_istable(L, -1)) {
      int parent_idx = lua_gettop(L);
      lua_pushstring(L, CLASS_KEY_FINALS);
      lua_rawget(L, parent_idx);
      if (lua_istable(L, -1)) {
        lua_pushvalue(L, 2);  /* 键 (method_name) */
        lua_rawget(L, -2);
        if (lua_toboolean(L, -1)) {
          const char *parent_name = get_class_name_str(L, parent_idx);
          const char *method_name = lua_tostring(L, 2);
          return luaL_error(L, "cannot override final method '%s' of class '%s'",
                     method_name ? method_name : "?", parent_name);
        }
        lua_pop(L, 1);
      }
      lua_pop(L, 1);
    }
    lua_pop(L, 1);

    lua_pushstring(L, CLASS_KEY_METHODS);
    lua_rawget(L, 1);
    if (!lua_istable(L, -1)) {
      lua_pop(L, 1);
      lua_newtable(L);
      lua_pushvalue(L, -1);
      lua_pushstring(L, CLASS_KEY_METHODS);
      lua_insert(L, -2);
      lua_rawset(L, 1);
    }
    /* 保存值副本用于后续别名同步 */
    lua_pushvalue(L, 3);  /* 复制值，栈: ... METHODS, key, value, value_copy */
    /* 写入方法表：旧键名 __init__ 统一存储为 init */
    if (is_legacy_init) {
      lua_pushstring(L, CLASS_KEY_INIT);  /* 存储为 init */
    } else {
      lua_pushvalue(L, 2);  /* 键 */
    }
    lua_pushvalue(L, 3);  /* 值 */
    lua_rawset(L, -4);    /* METHODS[key]=value，弹出 key 和 value，
                             栈: ... METHODS, value_copy */

    /* 别名同步：new <-> init
       注意：仅当目标不存在时才同步，避免覆盖已有定义
       例如 static function new 不应覆盖已定义的构造函数 init */
    if (is_new_key) {
      /* 设置 new 时同步 init：仅当 init 不存在时才同步 */
      lua_pushstring(L, CLASS_KEY_INIT);
      lua_rawget(L, -3);  /* METHODS.init */
      if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_pushstring(L, CLASS_KEY_INIT);
        lua_pushvalue(L, -2);  /* value_copy */
        lua_rawset(L, -4);
      } else {
        lua_pop(L, 1);
      }
    } else if (is_init_key) {
      /* 设置 init / __init__ 时同步 new：仅当 new 不存在时才同步 */
      lua_pushliteral(L, "new");
      lua_rawget(L, -3);  /* METHODS.new */
      if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_pushliteral(L, "new");
        lua_pushvalue(L, -2);  /* value_copy */
        lua_rawset(L, -4);
      } else {
        lua_pop(L, 1);
      }
    }

    /* 旧键名 __init__ 向后兼容：同时写入 __init__ 并输出弃用警告 */
    if (is_legacy_init) {
      lua_pushstring(L, CLASS_KEY_INIT_LEGACY);
      lua_pushvalue(L, -2);  /* value_copy */
      lua_rawset(L, -4);     /* METHODS.__init__ = value */
      fprintf(stderr, "[WARNING] class '%s': '__init__' is deprecated, use 'init' instead\n",
              get_class_name_str(L, 1));
    }

    lua_pop(L, 1);  /* 弹出 value_copy，栈: ... METHODS */
  } else {
    /* 否则设置到静态成员表 */
    lua_pushstring(L, CLASS_KEY_STATICS);
    lua_rawget(L, 1);
    if (!lua_istable(L, -1)) {
      lua_pop(L, 1);
      lua_newtable(L);
      lua_pushvalue(L, -1);
      lua_pushstring(L, CLASS_KEY_STATICS);
      lua_insert(L, -2);
      lua_rawset(L, 1);
    }
    lua_pushvalue(L, 2);  /* 键 */
    lua_pushvalue(L, 3);  /* 值 */
    lua_rawset(L, -3);

    /* 同上，移除 STATICS 表上的 new/__init__ 无条件别名同步 */
    lua_pop(L, 1);
  }
  
  return 0;
}


/*
** 类的__tostring元方法
** 参数：
**   L - Lua状态机
** 返回值：
**   1 - 返回类名字符串
*/
static int class_tostring(lua_State *L) {
  /* 使用rawget避免触发__index递归 */
  lua_pushstring(L, CLASS_KEY_NAME);
  lua_rawget(L, 1);
  if (lua_isstring(L, -1)) {
    lua_pushfstring(L, "class: %s", lua_tostring(L, -1));
  } else {
    lua_pushfstring(L, "class: %p", lua_topointer(L, 1));
  }
  return 1;
}


/*
** =====================================================================
** 对象元方法实现
** =====================================================================
*/


/*
** 获取调用栈中调用者所属的类
** 参数：
**   L - Lua状态机
**   obj_class_idx - 被访问对象的类在栈中的索引
** 返回值：
**   ACCESS_PUBLIC - 外部调用，只能访问公开成员
**   ACCESS_PROTECTED - 子类调用，可访问公开和受保护成员
**   ACCESS_PRIVATE - 同类调用，可访问所有成员
** 说明：
**   通过遍历调用栈，查找是否存在当前对象或其子类的方法调用
**   使用 self 参数来判断调用者上下文
*/
static int get_caller_access_level(lua_State *L, int obj_class_idx) {
  lua_Debug ar;
  int level = 1;  /* 从调用者开始（跳过当前函数） */
  
  obj_class_idx = absindex(L, obj_class_idx);
  
  /* 遍历调用栈 */
  while (lua_getstack(L, level, &ar)) {
    /* 获取栈帧的函数信息 */
    if (lua_getinfo(L, "nSlu", &ar) == 0) {
      level++;
      continue;
    }
    
    /* 检查第一个局部变量（通常是 self） */
    const char *name = lua_getlocal(L, &ar, 1);
    if (name != NULL) {
      /* 检查是否是 self 参数 */
      if (strcmp(name, "self") == 0 && lua_istable(L, -1)) {
        /* 获取 self 对象的类 */
        lua_pushstring(L, OBJ_KEY_CLASS);
        lua_rawget(L, -2);
        
        if (lua_istable(L, -1)) {
          int caller_class_idx = lua_gettop(L);
          
          /* 检查是否是同一个类 */
          if (lua_rawequal(L, caller_class_idx, obj_class_idx)) {
            lua_pop(L, 2);  /* 移除 caller_class 和 self */
            return ACCESS_PRIVATE;  /* 同类，可访问私有成员 */
          }
          
          /* 检查调用者类是否是目标类的子类 */
          lua_pushstring(L, CLASS_KEY_PARENT);
          lua_rawget(L, caller_class_idx);
          while (lua_istable(L, -1)) {
            if (lua_rawequal(L, -1, obj_class_idx)) {
              lua_pop(L, 3);  /* 移除 parent, caller_class, self */
              return ACCESS_PROTECTED;  /* 子类，可访问受保护成员 */
            }
            lua_pushstring(L, CLASS_KEY_PARENT);
            lua_rawget(L, -2);
            lua_remove(L, -2);
          }
          lua_pop(L, 1);  /* 移除 nil（非表值） */
          
          /* 检查目标类是否是调用者类的子类（即调用者是父类方法） */
          lua_pushstring(L, CLASS_KEY_PARENT);
          lua_rawget(L, obj_class_idx);
          while (lua_istable(L, -1)) {
            if (lua_rawequal(L, -1, caller_class_idx)) {
              lua_pop(L, 3);  /* 移除 parent, caller_class, self */
              return ACCESS_PROTECTED;  /* 父类方法访问子类对象，允许受保护访问 */
            }
            lua_pushstring(L, CLASS_KEY_PARENT);
            lua_rawget(L, -2);
            lua_remove(L, -2);
          }
          lua_pop(L, 1);  /* 移除 nil */
          
          lua_pop(L, 1);  /* 移除 caller_class */
        } else {
          lua_pop(L, 1);  /* 移除非表值 */
        }
      }
      lua_pop(L, 1);  /* 移除局部变量值 */
    }
    
    level++;
  }
  
  /* 没有找到合适的调用者上下文，视为外部调用 */
  return ACCESS_PUBLIC;
}


/*
** 检查成员存在于哪个访问级别表中
** 参数：
**   L - Lua状态机
**   class_idx - 类在栈中的索引
**   key_idx - 键在栈中的索引
** 返回值：
**   ACCESS_PUBLIC - 公开成员
**   ACCESS_PROTECTED - 受保护成员
**   ACCESS_PRIVATE - 私有成员
**   -1 - 成员不存在
** 说明：
**   检查成员在类中的访问级别
*/
static int get_member_access_level(lua_State *L, int class_idx, int key_idx) {
  class_idx = absindex(L, class_idx);
  key_idx = absindex(L, key_idx);
  
  /* 检查公开方法 */
  lua_pushstring(L, CLASS_KEY_METHODS);
  lua_rawget(L, class_idx);
  if (lua_istable(L, -1)) {
    lua_pushvalue(L, key_idx);
    lua_rawget(L, -2);
    if (!lua_isnil(L, -1)) {
      lua_pop(L, 2);
      return ACCESS_PUBLIC;
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  
  /* 检查受保护成员 */
  lua_pushstring(L, CLASS_KEY_PROTECTED);
  lua_rawget(L, class_idx);
  if (lua_istable(L, -1)) {
    lua_pushvalue(L, key_idx);
    lua_rawget(L, -2);
    if (!lua_isnil(L, -1)) {
      lua_pop(L, 2);
      return ACCESS_PROTECTED;
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  
  /* 检查私有成员 */
  lua_pushstring(L, CLASS_KEY_PRIVATES);
  lua_rawget(L, class_idx);
  if (lua_istable(L, -1)) {
    lua_pushvalue(L, key_idx);
    lua_rawget(L, -2);
    if (!lua_isnil(L, -1)) {
      lua_pop(L, 2);
      return ACCESS_PRIVATE;
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  
  return -1;  /* 成员不存在 */
}


/*
** 获取类名（用于错误消息）
** 参数：
**   L - Lua状态机
**   class_idx - 类在栈中的索引
** 返回值：
**   类名字符串，如果获取失败返回 "unknown"
** 说明：
**   使用静态缓冲区存储类名，避免弹出栈后指针悬空
**   注意：返回的字符串在下次调用本函数时会被覆盖
*/
static const char* get_class_name_str(lua_State *L, int class_idx) {
  static char classname_buf[256];  /* 静态缓冲区，存储类名 */
  class_idx = absindex(L, class_idx);
  lua_pushstring(L, CLASS_KEY_NAME);
  lua_rawget(L, class_idx);
  const char *name = lua_tostring(L, -1);
  if (name == NULL) {
    lua_pop(L, 1);
    return "unknown";
  }
  /* 将类名复制到静态缓冲区 */
  size_t len = strlen(name);
  if (len >= sizeof(classname_buf)) {
    len = sizeof(classname_buf) - 1;
  }
  memcpy(classname_buf, name, len);
  classname_buf[len] = '\0';
  lua_pop(L, 1);
  return classname_buf;
}


/*
** 对象的__index元方法 - 用于访问对象属性和方法
** 支持访问控制：public成员可自由访问，protected和private成员有限制
** 支持getter属性访问器
** 参数：
**   L - Lua状态机
** 返回值：
**   1 - 返回找到的值或nil
*/
static int object_index(lua_State *L) {
  /* 栈: [1]=对象, [2]=键 */
  
  /* 获取对象所属的类（使用rawget避免递归） */
  lua_pushstring(L, OBJ_KEY_CLASS);
  lua_rawget(L, 1);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    /* 不是对象，直接在表中查找 */
    lua_pushvalue(L, 2);
    lua_rawget(L, 1);
    return 1;
  }
  int class_idx = lua_gettop(L);
  
  /* 确定调用者的访问级别（提前获取，用于getter权限检查） */
  int caller_access = get_caller_access_level(L, class_idx);
  
  /* 检查getter（在继承链中查找，支持公开/受保护/私有getter） */
  /* 使用更安全的栈管理方式：不在循环中删除元素 */
  lua_pushvalue(L, class_idx);  /* 复制类引用到栈顶作为遍历起点 */
  int iter_idx = lua_gettop(L);
  int is_first_class_getter = 1;  /* 标记是否是对象直接所属的类 */
  
  while (lua_istable(L, iter_idx)) {
    /* 根据访问级别依次检查不同的getter表 */
    
    /* 1. 私有getter - 只有同类方法可以访问，且只在第一个类（对象直接所属类）中查找 */
    if (caller_access == ACCESS_PRIVATE && is_first_class_getter) {
      lua_pushstring(L, CLASS_KEY_PRIVATE_GETTERS);
      lua_rawget(L, iter_idx);
      if (lua_istable(L, -1)) {
        lua_pushvalue(L, 2);  /* 键 */
        lua_rawget(L, -2);
        if (lua_isfunction(L, -1)) {
          /* 找到私有getter，调用它 */
          lua_pushvalue(L, 1);  /* self */
          lua_call(L, 1, 1);
          return 1;
        }
        lua_pop(L, 1);  /* 移除查找结果 */
      }
      lua_pop(L, 1);  /* 移除私有getters表 */
    }
    
    /* 2. 受保护getter - 同类或子类方法可以访问 */
    if (caller_access == ACCESS_PRIVATE || caller_access == ACCESS_PROTECTED) {
      lua_pushstring(L, CLASS_KEY_PROTECTED_GETTERS);
      lua_rawget(L, iter_idx);
      if (lua_istable(L, -1)) {
        lua_pushvalue(L, 2);  /* 键 */
        lua_rawget(L, -2);
        if (lua_isfunction(L, -1)) {
          /* 找到受保护getter，调用它 */
          lua_pushvalue(L, 1);  /* self */
          lua_call(L, 1, 1);
          return 1;
        }
        lua_pop(L, 1);  /* 移除查找结果 */
      }
      lua_pop(L, 1);  /* 移除受保护getters表 */
    }
    
    /* 3. 公开getter - 任何人都可以访问 */
    lua_pushstring(L, CLASS_KEY_GETTERS);
    lua_rawget(L, iter_idx);
    if (lua_istable(L, -1)) {
      lua_pushvalue(L, 2);  /* 键 */
      lua_rawget(L, -2);
      if (lua_isfunction(L, -1)) {
        /* 找到公开getter，调用它 */
        lua_pushvalue(L, 1);  /* self */
        lua_call(L, 1, 1);
        return 1;
      }
      lua_pop(L, 1);  /* 移除查找结果 */
    }
    lua_pop(L, 1);  /* 移除公开getters表 */
    
    /* 继续查找父类 */
    is_first_class_getter = 0;  /* 后续都不是第一个类了 */
    lua_pushstring(L, CLASS_KEY_PARENT);
    lua_rawget(L, iter_idx);
    lua_replace(L, iter_idx);  /* 用父类替换当前迭代位置 */
  }
  lua_pop(L, 1);  /* 移除最后的非表值（nil） */
  
  /* 在对象自身查找（实例属性） */
  lua_pushvalue(L, 2);
  lua_rawget(L, 1);
  if (!lua_isnil(L, -1)) {
    /* 检查不是内部键 */
    if (lua_isstring(L, 2)) {
      const char *key = lua_tostring(L, 2);
      /* 内部键以双下划线开头，不允许外部直接访问 */
      if (key && key[0] == '_' && key[1] == '_') {
        /* 这是内部键，需要检查权限 */
        lua_pop(L, 1);
        /* 继续下面的处理流程 */
      } else {
        return 1;  /* 普通实例属性，允许访问 */
      }
    } else {
      return 1;
    }
  } else {
    lua_pop(L, 1);
  }
  
  /* 在类及其继承链中查找成员 */
  /* 使用更安全的栈管理方式：复制一份用于迭代 */
  lua_pushvalue(L, class_idx);  /* 复制类引用到栈顶 */
  int current_class = lua_gettop(L);
  int is_first_class = 1;  /* 标记是否是对象直接所属的类 */
  
  while (lua_istable(L, current_class)) {
    /* 确定当前类中成员的访问级别 */
    int member_access = get_member_access_level(L, current_class, 2);
    
    if (member_access >= 0) {
      /* 找到了成员，检查访问权限 */
      
      /* 私有成员：只有同类方法可以访问 */
      if (member_access == ACCESS_PRIVATE) {
        if (!is_first_class) {
          /* 父类的私有成员不可被子类访问 */
          goto next_class;
        }
        if (caller_access != ACCESS_PRIVATE) {
          /* 外部或子类不能访问私有成员 */
          const char *classname = get_class_name_str(L, class_idx);
          const char *key = lua_tostring(L, 2);
          return luaL_error(L, "cannot access private member '%s' of class '%s'",
                           key ? key : "?", classname);
        }
      }
      
      /* 受保护成员：只有同类或子类方法可以访问 */
      if (member_access == ACCESS_PROTECTED) {
        if (caller_access == ACCESS_PUBLIC) {
          /* 外部不能访问受保护成员 */
          const char *classname = get_class_name_str(L, class_idx);
          const char *key = lua_tostring(L, 2);
          return luaL_error(L, "cannot access protected member '%s' of class '%s'",
                           key ? key : "?", classname);
        }
      }
      
      /* 访问权限检查通过，获取成员值 */
      const char *table_key;
      if (member_access == ACCESS_PRIVATE) {
        table_key = CLASS_KEY_PRIVATES;
      } else if (member_access == ACCESS_PROTECTED) {
        table_key = CLASS_KEY_PROTECTED;
      } else {
        table_key = CLASS_KEY_METHODS;
      }
      
      lua_pushstring(L, table_key);
      lua_rawget(L, current_class);
      lua_pushvalue(L, 2);
      lua_rawget(L, -2);
      return 1;
    }
    
next_class:
    /* 继续查找父类 */
    is_first_class = 0;
    lua_pushstring(L, CLASS_KEY_PARENT);
    lua_rawget(L, current_class);
    lua_replace(L, current_class);  /* 用父类替换当前迭代位置 */
  }
  lua_pop(L, 1);  /* 移除迭代用的类引用 */
  
  /* 检查对象私有数据表（实例级别的私有数据） */
  lua_pushstring(L, OBJ_KEY_PRIVATES);
  lua_rawget(L, 1);
  if (lua_istable(L, -1)) {
    lua_pushvalue(L, 2);
    lua_rawget(L, -2);
    if (!lua_isnil(L, -1)) {
      /* 实例私有数据也需要权限检查 */
      if (caller_access != ACCESS_PRIVATE) {
        const char *classname = get_class_name_str(L, class_idx);
        const char *key = lua_tostring(L, 2);
        return luaL_error(L, "cannot access private data '%s' of '%s' object",
                         key ? key : "?", classname);
      }
      lua_remove(L, -2);
      return 1;
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  
  /* 检查静态成员 */
  lua_pushstring(L, CLASS_KEY_STATICS);
  lua_rawget(L, class_idx);
  if (lua_istable(L, -1)) {
    lua_pushvalue(L, 2);
    lua_rawget(L, -2);
    if (!lua_isnil(L, -1)) {
      return 1;
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  
  lua_pushnil(L);
  return 1;
}


/*
** 对象的__newindex元方法 - 用于设置对象属性
** 支持访问控制：防止外部修改私有/受保护成员
** 支持setter属性访问器
** 参数：
**   L - Lua状态机
** 返回值：
**   0
*/
static int object_newindex(lua_State *L) {
  /* 栈: [1]=对象, [2]=键, [3]=值 */
  
  /* 获取对象的类 */
  lua_pushstring(L, OBJ_KEY_CLASS);
  lua_rawget(L, 1);
  if (lua_istable(L, -1)) {
    int class_idx = lua_gettop(L);
    
    /* 确定调用者的访问级别（提前获取，用于setter权限检查） */
    int caller_access = get_caller_access_level(L, class_idx);
    
    /* 检查setter（在继承链中查找，支持公开/受保护/私有setter） */
    /* 使用更安全的栈管理方式：不在循环中删除元素 */
    lua_pushvalue(L, class_idx);  /* 复制类引用到栈顶作为遍历起点 */
    int iter_idx = lua_gettop(L);
    int is_first_class_setter = 1;  /* 标记是否是对象直接所属的类 */
    
    while (lua_istable(L, iter_idx)) {
      /* 根据访问级别依次检查不同的setter表 */
      
      /* 1. 私有setter - 只有同类方法可以访问，且只在第一个类中查找 */
      if (caller_access == ACCESS_PRIVATE && is_first_class_setter) {
        lua_pushstring(L, CLASS_KEY_PRIVATE_SETTERS);
        lua_rawget(L, iter_idx);
        if (lua_istable(L, -1)) {
          lua_pushvalue(L, 2);  /* 键 */
          lua_rawget(L, -2);
          if (lua_isfunction(L, -1)) {
            /* 找到私有setter，调用它 */
            lua_pushvalue(L, 1);  /* self */
            lua_pushvalue(L, 3);  /* value */
            lua_call(L, 2, 0);
            return 0;
          }
          lua_pop(L, 1);  /* 移除查找结果 */
        }
        lua_pop(L, 1);  /* 移除私有setters表 */
      }
      
      /* 2. 受保护setter - 同类或子类方法可以访问 */
      if (caller_access == ACCESS_PRIVATE || caller_access == ACCESS_PROTECTED) {
        lua_pushstring(L, CLASS_KEY_PROTECTED_SETTERS);
        lua_rawget(L, iter_idx);
        if (lua_istable(L, -1)) {
          lua_pushvalue(L, 2);  /* 键 */
          lua_rawget(L, -2);
          if (lua_isfunction(L, -1)) {
            /* 找到受保护setter，调用它 */
            lua_pushvalue(L, 1);  /* self */
            lua_pushvalue(L, 3);  /* value */
            lua_call(L, 2, 0);
            return 0;
          }
          lua_pop(L, 1);  /* 移除查找结果 */
        }
        lua_pop(L, 1);  /* 移除受保护setters表 */
      }
      
      /* 3. 公开setter - 任何人都可以访问 */
      lua_pushstring(L, CLASS_KEY_SETTERS);
      lua_rawget(L, iter_idx);
      if (lua_istable(L, -1)) {
        lua_pushvalue(L, 2);  /* 键 */
        lua_rawget(L, -2);
        if (lua_isfunction(L, -1)) {
          /* 找到公开setter，调用它 */
          lua_pushvalue(L, 1);  /* self */
          lua_pushvalue(L, 3);  /* value */
          lua_call(L, 2, 0);
          return 0;
        }
        lua_pop(L, 1);  /* 移除查找结果 */
      }
      lua_pop(L, 1);  /* 移除公开setters表 */
      
      /* 继续查找父类 */
      is_first_class_setter = 0;  /* 后续都不是第一个类了 */
      lua_pushstring(L, CLASS_KEY_PARENT);
      lua_rawget(L, iter_idx);
      lua_replace(L, iter_idx);  /* 用父类替换当前迭代位置 */
    }
    lua_pop(L, 1);  /* 移除最后的非表值（nil） */
    
    /* 检查是否尝试设置内部键 */
    if (lua_isstring(L, 2)) {
      const char *key = lua_tostring(L, 2);
      if (key && key[0] == '_' && key[1] == '_') {
        /* 尝试设置内部键，需要检查权限 */
        if (caller_access != ACCESS_PRIVATE) {
          const char *classname = get_class_name_str(L, class_idx);
          return luaL_error(L, "attempt to modify private field '%s' of '%s' object from outside",
                           key, classname);
        }
      }
    }
    
    /* 检查是否尝试覆盖类成员 */
    int member_access = get_member_access_level(L, class_idx, 2);
    
    if (member_access >= 0) {
      /* 尝试设置类成员，检查权限 */
      if (member_access == ACCESS_PRIVATE && caller_access != ACCESS_PRIVATE) {
        const char *classname = get_class_name_str(L, class_idx);
        const char *key = lua_tostring(L, 2);
        return luaL_error(L, "attempt to modify private member '%s' of class '%s' from outside",
                         key ? key : "?", classname);
      }
      
      if (member_access == ACCESS_PROTECTED && caller_access == ACCESS_PUBLIC) {
        const char *classname = get_class_name_str(L, class_idx);
        const char *key = lua_tostring(L, 2);
        return luaL_error(L, "attempt to modify protected member '%s' of class '%s' from outside",
                         key ? key : "?", classname);
      }
    }
  }
  lua_pop(L, 1);
  
  /* SEALED 检查：禁止在 sealed 类实例上动态添加新字段 */
  /* 检查对象所属类是否是 sealed */
  lua_pushstring(L, OBJ_KEY_CLASS);
  lua_rawget(L, 1);
  if (lua_istable(L, -1)) {
    lua_pushstring(L, CLASS_KEY_FLAGS);
    lua_rawget(L, -2);
    if (lua_isinteger(L, -1)) {
      int flags = (int)lua_tointeger(L, -1);
      if (flags & CLASS_FLAG_SEALED) {
        /* 构造期豁免：init 运行期间允许初始化新字段 */
        lua_pushstring(L, "__constructing");
        lua_rawget(L, 1);
        int constructing = lua_toboolean(L, -1);
        lua_pop(L, 1);
        /* 检查键是否已存在于对象自身表中 */
        lua_pushvalue(L, 2);  /* 键 */
        lua_rawget(L, 1);
        if (!constructing && lua_isnil(L, -1)) {
          /* 键不存在于对象自身，是新增字段，报错 */
          const char *classname = get_class_name_str(L, lua_gettop(L) - 2);
          const char *key = lua_tostring(L, 2);
          lua_pop(L, 4);  /* 清理栈 */
          return luaL_error(L, "cannot add new field '%s' to sealed class '%s'",
                           key ? key : "?", classname);
        }
        lua_pop(L, 1);  /* pop rawget 结果 */
      }
    }
    lua_pop(L, 1);  /* pop flags */
  }
  lua_pop(L, 1);  /* pop class */
  
  /* 权限检查通过，设置属性 */
  lua_pushvalue(L, 2);
  lua_pushvalue(L, 3);
  lua_rawset(L, 1);
  return 0;
}


/*
** 对象的__tostring元方法
** 参数：
**   L - Lua状态机
** 返回值：
**   1 - 返回对象描述字符串
*/
static int object_tostring(lua_State *L) {
  /* 首先检查对象是否有自定义的__tostring方法 */
  /* 使用rawget避免触发__index递归 */
  lua_pushstring(L, OBJ_KEY_CLASS);
  lua_rawget(L, 1);
  if (lua_istable(L, -1)) {
    /* 使用rawget访问类表 */
    lua_pushstring(L, CLASS_KEY_METHODS);
    lua_rawget(L, -2);
    if (lua_istable(L, -1)) {
      lua_pushstring(L, "__tostring");
      lua_rawget(L, -2);
      if (lua_isfunction(L, -1)) {
        lua_pushvalue(L, 1);  /* self */
        lua_call(L, 1, 1);
        return 1;
      }
      lua_pop(L, 1);
    }
    lua_pop(L, 1);
    
    /* 使用默认格式 */
    lua_pushstring(L, CLASS_KEY_NAME);
    lua_rawget(L, -2);
    if (lua_isstring(L, -1)) {
      lua_pushfstring(L, "<%s object: %p>", lua_tostring(L, -1), lua_topointer(L, 1));
      return 1;
    }
  }
  
  lua_pushfstring(L, "<object: %p>", lua_topointer(L, 1));
  return 1;
}


/*
** =====================================================================
** 类系统核心函数实现
** =====================================================================
*/

/*
** 创建新类
*/
void luaC_newclass(lua_State *L, TString *name) {
  LUA_LOGD("[CLASS] luaC_newclass START, name='%s'", getstr(name));
  /* 创建类表 */
  lua_newtable(L);
  int class_idx = lua_gettop(L);

  /* 设置类名（使用rawset避免触发元方法） */
  lua_pushstring(L, CLASS_KEY_NAME);
  lua_pushlstring(L, getstr(name), tsslen(name));
  lua_rawset(L, class_idx);

  /* 标记为类 */
  setboolfield(L, class_idx, CLASS_KEY_ISCLASS, 1);

  /* 初始化类标志为0 */
  lua_pushstring(L, CLASS_KEY_FLAGS);
  lua_pushinteger(L, 0);
  lua_rawset(L, class_idx);

  /* 创建方法表（公开成员） */
  lua_pushstring(L, CLASS_KEY_METHODS);
  lua_newtable(L);
  /* 注册内置 clone 方法到类方法表 */
  lua_pushstring(L, "clone");
  lua_pushcfunction(L, luaC_clone_wrap);
  lua_rawset(L, -3);
  lua_rawset(L, class_idx);

  /* 创建静态成员表 */
  lua_pushstring(L, CLASS_KEY_STATICS);
  lua_newtable(L);
  lua_rawset(L, class_idx);
  
  /* 创建私有成员表 */
  lua_pushstring(L, CLASS_KEY_PRIVATES);
  lua_newtable(L);
  lua_rawset(L, class_idx);
  
  /* 创建受保护成员表 */
  lua_pushstring(L, CLASS_KEY_PROTECTED);
  lua_newtable(L);
  lua_rawset(L, class_idx);
  
  /* 创建抽象方法表 */
  lua_pushstring(L, CLASS_KEY_ABSTRACTS);
  lua_newtable(L);
  lua_rawset(L, class_idx);
  
  /* 创建final方法表 */
  lua_pushstring(L, CLASS_KEY_FINALS);
  lua_newtable(L);
  lua_rawset(L, class_idx);
  
  /* 创建getter方法表（公开） */
  lua_pushstring(L, CLASS_KEY_GETTERS);
  lua_newtable(L);
  lua_rawset(L, class_idx);
  
  /* 创建setter方法表（公开） */
  lua_pushstring(L, CLASS_KEY_SETTERS);
  lua_newtable(L);
  lua_rawset(L, class_idx);
  
  /* 创建私有getter方法表 */
  lua_pushstring(L, CLASS_KEY_PRIVATE_GETTERS);
  lua_newtable(L);
  lua_rawset(L, class_idx);
  
  /* 创建私有setter方法表 */
  lua_pushstring(L, CLASS_KEY_PRIVATE_SETTERS);
  lua_newtable(L);
  lua_rawset(L, class_idx);
  
  /* 创建受保护getter方法表 */
  lua_pushstring(L, CLASS_KEY_PROTECTED_GETTERS);
  lua_newtable(L);
  lua_rawset(L, class_idx);
  
  /* 创建受保护setter方法表 */
  lua_pushstring(L, CLASS_KEY_PROTECTED_SETTERS);
  lua_newtable(L);
  lua_rawset(L, class_idx);
  
  /* 创建接口列表 */
  lua_pushstring(L, CLASS_KEY_INTERFACES);
  lua_newtable(L);
  lua_rawset(L, class_idx);
  
  /* 创建成员标志表 */
  lua_pushstring(L, CLASS_KEY_MEMBER_FLAGS);
  lua_newtable(L);
  lua_rawset(L, class_idx);
  
  /* 创建并设置类的元表 */
  lua_newtable(L);
  int mt_idx = lua_gettop(L);
  
  /* 设置__call元方法（用于实例化） */
  lua_pushcfunction(L, class_call);
  lua_setfield(L, mt_idx, "__call");
  
  /* 设置__index元方法 */
  lua_pushcfunction(L, class_index);
  lua_setfield(L, mt_idx, "__index");
  
  /* 设置__newindex元方法 */
  lua_pushcfunction(L, class_newindex);
  lua_setfield(L, mt_idx, "__newindex");
  
  /* 设置__tostring元方法 */
  lua_pushcfunction(L, class_tostring);
  lua_setfield(L, mt_idx, "__tostring");
  
  /* 应用元表 */
  lua_setmetatable(L, class_idx);
  
  /* 检查并调用静态构造函数 */
  lua_pushstring(L, CLASS_KEY_STATICS);
  lua_rawget(L, class_idx);
  if (lua_istable(L, -1)) {
    lua_pushstring(L, CLASS_KEY_INIT);
    lua_rawget(L, -2);
    if (lua_isfunction(L, -1)) {
      lua_call(L, 0, 0);  /* 调用静态 init，无参数，无返回值 */
    } else {
      lua_pop(L, 1);  /* pop 非函数值 */
    }
  }
  lua_pop(L, 1);  /* pop __statics 表 */

  /* 计算初始 MRO（无父类时 MRO = [class]），确保每个类都有 __mro */
  luaC_compute_mro(L, class_idx);

  LUA_LOGD("[CLASS] luaC_newclass END, name='%s', class_idx=%d", getstr(name), class_idx);
  /* 类表现在栈顶 */
}


/*
** 调用类的静态构造函数（__statics.init，无参）
** 由 OP_STATICINIT 在类定义完成后触发
*/
void luaC_staticinit(lua_State *L, int class_idx) {
  class_idx = absindex(L, class_idx);
  if (!lua_istable(L, class_idx)) return;
  lua_pushstring(L, CLASS_KEY_STATICS);
  lua_rawget(L, class_idx);
  if (lua_istable(L, -1)) {
    lua_pushstring(L, CLASS_KEY_INIT);
    lua_rawget(L, -2);
    if (lua_isfunction(L, -1)) {
      lua_call(L, 0, 0);  /* 调用静态构造函数，无参数 */
    } else {
      lua_pop(L, 1);
    }
  }
  lua_pop(L, 1);  /* 弹出 __statics */
}


/*
** 设置类的继承关系
** 支持final类检查、final方法检查、抽象方法继承、getter/setter继承
*/
void luaC_inherit(lua_State *L, int child_idx, int parent_idx) {
  LUA_LOGD("[CLASS] luaC_inherit START, child_idx=%d parent_idx=%d", child_idx, parent_idx);
  child_idx = absindex(L, child_idx);
  parent_idx = absindex(L, parent_idx);
  
  /* 检查父类是否是有效的类 */
  if (!luaC_isclass(L, parent_idx)) {
    luaL_error(L, "parent is not a valid class");
    return;
  }
  
  /* 检查父类是否是final类或sealed类 */
  lua_pushstring(L, CLASS_KEY_FLAGS);
  lua_rawget(L, parent_idx);
  if (lua_isinteger(L, -1)) {
    int flags = (int)lua_tointeger(L, -1);
    if (flags & CLASS_FLAG_FINAL) {
      const char *parent_name = get_class_name_str(L, parent_idx);
      luaL_error(L, "cannot inherit from final class '%s'", parent_name);
      return;
    }
    if (flags & CLASS_FLAG_SEALED) {
      const char *parent_name = get_class_name_str(L, parent_idx);
      luaL_error(L, "cannot inherit from sealed class '%s'", parent_name);
      return;
    }
    if (flags & CLASS_FLAG_SINGLETON) {
      const char *parent_name = get_class_name_str(L, parent_idx);
      luaL_error(L, "cannot inherit from singleton class '%s'", parent_name);
      return;
    }
  }
  lua_pop(L, 1);
  
  /* 设置父类引用（使用rawset避免触发__newindex） */
  lua_pushstring(L, CLASS_KEY_PARENT);
  lua_pushvalue(L, parent_idx);
  lua_rawset(L, child_idx);
  
  /* 获取子类的方法表用于检查final方法重写 */
  lua_pushstring(L, CLASS_KEY_METHODS);
  lua_rawget(L, child_idx);
  int child_methods_for_check = lua_gettop(L);
  
  /* 检查子类是否尝试重写父类的final方法 */
  if (lua_istable(L, child_methods_for_check)) {
    lua_pushnil(L);
    while (lua_next(L, child_methods_for_check) != 0) {
      lua_pop(L, 1);  /* 移除value，只需要key */
      /* 检查这个方法是否是父类的final方法 */
      if (lua_isstring(L, -1)) {
        const char *method_name = lua_tostring(L, -1);
        /* 直接在父类的finals表中查找 */
        lua_pushstring(L, CLASS_KEY_FINALS);
        lua_rawget(L, parent_idx);
        if (lua_istable(L, -1)) {
          lua_pushvalue(L, -2);  /* 方法名 */
          lua_rawget(L, -2);
          if (lua_toboolean(L, -1)) {
            const char *parent_name = get_class_name_str(L, parent_idx);
            luaL_error(L, "cannot override final method '%s' of class '%s'",
                       method_name, parent_name);
            return;
          }
          lua_pop(L, 1);
        }
        lua_pop(L, 1);
      }
    }
  }
  lua_pop(L, 1);
  
  /* 复制父类的公开方法到子类（实现继承，使用rawget访问类表） */
  lua_pushstring(L, CLASS_KEY_METHODS);
  lua_rawget(L, parent_idx);
  if (lua_istable(L, -1)) {
    int parent_methods = lua_gettop(L);
    
    lua_pushstring(L, CLASS_KEY_METHODS);
    lua_rawget(L, child_idx);
    if (!lua_istable(L, -1)) {
      lua_pop(L, 1);
      lua_newtable(L);
      lua_pushvalue(L, -1);
      lua_pushstring(L, CLASS_KEY_METHODS);
      lua_insert(L, -2);
      lua_rawset(L, child_idx);
    }
    int child_methods = lua_gettop(L);
    
    /* 复制方法（子类已有的方法不覆盖） */
    lua_pushnil(L);
    while (lua_next(L, parent_methods) != 0) {
      /* 检查子类是否已有该方法 */
      lua_pushvalue(L, -2);  /* 复制key */
      lua_rawget(L, child_methods);
      if (lua_isnil(L, -1)) {
        /* 子类没有这个方法，从父类复制 */
        lua_pop(L, 1);  /* 移除nil */
        lua_pushvalue(L, -2);  /* key */
        lua_pushvalue(L, -2);  /* value */
        lua_rawset(L, child_methods);
      } else {
        lua_pop(L, 1);  /* 移除已有的值 */
      }
      lua_pop(L, 1);  /* 移除value */
    }
    
    lua_pop(L, 1);  /* 移除child_methods */
  }
  lua_pop(L, 1);  /* 移除parent_methods */
  
  /* 复制父类的受保护成员到子类（子类可以访问，使用rawget） */
  lua_pushstring(L, CLASS_KEY_PROTECTED);
  lua_rawget(L, parent_idx);
  if (lua_istable(L, -1)) {
    int parent_protected = lua_gettop(L);
    
    lua_pushstring(L, CLASS_KEY_PROTECTED);
    lua_rawget(L, child_idx);
    if (!lua_istable(L, -1)) {
      lua_pop(L, 1);
      lua_newtable(L);
      lua_pushvalue(L, -1);
      lua_pushstring(L, CLASS_KEY_PROTECTED);
      lua_insert(L, -2);
      lua_rawset(L, child_idx);
    }
    int child_protected = lua_gettop(L);
    
    /* 复制受保护成员（子类已有的不覆盖） */
    lua_pushnil(L);
    while (lua_next(L, parent_protected) != 0) {
      lua_pushvalue(L, -2);  /* 复制key */
      lua_rawget(L, child_protected);
      if (lua_isnil(L, -1)) {
        lua_pop(L, 1);  /* 移除nil */
        lua_pushvalue(L, -2);  /* key */
        lua_pushvalue(L, -2);  /* value */
        lua_rawset(L, child_protected);
      } else {
        lua_pop(L, 1);  /* 移除已有的值 */
      }
      lua_pop(L, 1);  /* 移除value */
    }
    
    lua_pop(L, 1);  /* 移除child_protected */
  }
  lua_pop(L, 1);  /* 移除parent_protected */
  
  /* 复制父类的getter表（子类已有的不覆盖） */
  lua_pushstring(L, CLASS_KEY_GETTERS);
  lua_rawget(L, parent_idx);
  if (lua_istable(L, -1)) {
    int parent_getters = lua_gettop(L);
    
    lua_pushstring(L, CLASS_KEY_GETTERS);
    lua_rawget(L, child_idx);
    if (!lua_istable(L, -1)) {
      lua_pop(L, 1);
      lua_newtable(L);
      lua_pushvalue(L, -1);
      lua_pushstring(L, CLASS_KEY_GETTERS);
      lua_insert(L, -2);
      lua_rawset(L, child_idx);
    }
    int child_getters = lua_gettop(L);
    
    lua_pushnil(L);
    while (lua_next(L, parent_getters) != 0) {
      lua_pushvalue(L, -2);
      lua_rawget(L, child_getters);
      if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_pushvalue(L, -2);
        lua_pushvalue(L, -2);
        lua_rawset(L, child_getters);
      } else {
        lua_pop(L, 1);
      }
      lua_pop(L, 1);
    }
    
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  
  /* 复制父类的setter表（子类已有的不覆盖） */
  lua_pushstring(L, CLASS_KEY_SETTERS);
  lua_rawget(L, parent_idx);
  if (lua_istable(L, -1)) {
    int parent_setters = lua_gettop(L);
    
    lua_pushstring(L, CLASS_KEY_SETTERS);
    lua_rawget(L, child_idx);
    if (!lua_istable(L, -1)) {
      lua_pop(L, 1);
      lua_newtable(L);
      lua_pushvalue(L, -1);
      lua_pushstring(L, CLASS_KEY_SETTERS);
      lua_insert(L, -2);
      lua_rawset(L, child_idx);
    }
    int child_setters = lua_gettop(L);
    
    lua_pushnil(L);
    while (lua_next(L, parent_setters) != 0) {
      lua_pushvalue(L, -2);
      lua_rawget(L, child_setters);
      if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_pushvalue(L, -2);
        lua_pushvalue(L, -2);
        lua_rawset(L, child_setters);
      } else {
        lua_pop(L, 1);
      }
      lua_pop(L, 1);
    }
    
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  
  /* 复制父类的final方法表（合并） */
  lua_pushstring(L, CLASS_KEY_FINALS);
  lua_rawget(L, parent_idx);
  if (lua_istable(L, -1)) {
    int parent_finals = lua_gettop(L);
    
    lua_pushstring(L, CLASS_KEY_FINALS);
    lua_rawget(L, child_idx);
    if (!lua_istable(L, -1)) {
      lua_pop(L, 1);
      lua_newtable(L);
      lua_pushvalue(L, -1);
      lua_pushstring(L, CLASS_KEY_FINALS);
      lua_insert(L, -2);
      lua_rawset(L, child_idx);
    }
    int child_finals = lua_gettop(L);
    
    lua_pushnil(L);
    while (lua_next(L, parent_finals) != 0) {
      lua_pushvalue(L, -2);
      lua_pushvalue(L, -2);
      lua_rawset(L, child_finals);
      lua_pop(L, 1);
    }
    
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  
  /* 注意：私有成员不被继承 */
}

/*
** C3 线性化算法：计算类的方法解析顺序 (MRO)
** 算法：L[C] = [C] + merge(L[B1], ..., L[Bn], [B1, ..., Bn])
** 其中 merge 函数：
**   1. 取第一个列表的头元素
**   2. 如果该头元素不在任何其他列表的尾部出现，则将其从所有列表中移除并加入结果
**   3. 否则取下一个列表的头元素重复检查
**   4. 如果所有头元素都不满足条件（都在其他列表尾部出现），则 MRO 不兼容
** 参数：
**   L - Lua 状态机
**   class_idx - 类在栈中的索引
** 说明：
**   计算完成后将 MRO 数组存入 class.__mro
**   MRO 顺序：类自身在最前，然后按 C3 线性化排列父类
*/
void luaC_compute_mro(lua_State *L, int class_idx) {
  class_idx = absindex(L, class_idx);

  /* Step 1: 收集父类列表 */
  lua_pushstring(L, CLASS_KEY_PARENTS);
  lua_rawget(L, class_idx);

  int has_parents_list = lua_istable(L, -1);
  int num_parents = 0;

  if (has_parents_list) {
    num_parents = (int)luaL_len(L, -1);
  } else {
    lua_pop(L, 1);
    /* 检查单继承 */
    lua_pushstring(L, CLASS_KEY_PARENT);
    lua_rawget(L, class_idx);
    if (lua_istable(L, -1)) {
      num_parents = 1;
    } else {
      lua_pop(L, 1);
      /* 无父类，MRO = [class] */
      lua_pushstring(L, CLASS_KEY_MRO);
      lua_newtable(L);
      lua_pushvalue(L, class_idx);
      lua_rawseti(L, -2, 1);
      lua_rawset(L, class_idx);
      return;
    }
  }

  /*
  ** Step 2: 构建线性化列表
  ** linearizations = {parent1_mro, ..., parentN_mro, parents_list}
  ** 其中 parents_list = [parent1, ..., parentN]（父类声明顺序）
  */
  lua_newtable(L);  /* linearizations */
  int lin_idx = lua_gettop(L);

  lua_newtable(L);  /* parents_list */
  int parents_list_idx = lua_gettop(L);

  for (int i = 1; i <= num_parents; i++) {
    /* 获取父类 */
    if (has_parents_list) {
      lua_rawgeti(L, -3, i);  /* 从 __parents 获取 (__parents 在 linearizations 和 parents_list 下方) */
    } else {
      lua_pushstring(L, CLASS_KEY_PARENT);
      lua_rawget(L, class_idx);
    }

    int parent_idx = lua_gettop(L);

    /* 将父类加入 parents_list */
    lua_pushvalue(L, parent_idx);
    lua_rawseti(L, parents_list_idx, i);

    /* 获取或计算父类的 MRO */
    lua_pushstring(L, CLASS_KEY_MRO);
    lua_rawget(L, parent_idx);
    if (!lua_istable(L, -1)) {
      lua_pop(L, 1);
      /* 递归计算父类 MRO */
      luaC_compute_mro(L, parent_idx);
      lua_pushstring(L, CLASS_KEY_MRO);
      lua_rawget(L, parent_idx);
    }

    /* 将父类 MRO 存入 linearizations[i] */
    lua_rawseti(L, lin_idx, i);
    lua_pop(L, 1);  /* 弹出父类 */
  }

  /* 将 parents_list 作为最后一个线性化列表 */
  lua_pushvalue(L, parents_list_idx);
  lua_rawseti(L, lin_idx, num_parents + 1);

  /* 清理栈：移除 __parents/__parent 和 parents_list */
  if (has_parents_list) {
    lua_remove(L, -3);  /* 移除 __parents（在 linearizations 和 parents_list 下方） */
  } else {
    lua_remove(L, -3);  /* 移除 __parent 值 */
  }
  lin_idx -= 1;          /* linearizations 下移 */
  parents_list_idx -= 1; /* parents_list 下移 */
  lua_pop(L, 1);         /* 弹出 parents_list（已存入 linearizations） */

  /* 栈: [..., class, linearizations] */

  /*
  ** Step 3: C3 Merge 算法
  ** 使用位置追踪表来避免频繁的栈操作
  */
  int total_lists = num_parents + 1;  /* 父类 MRO + parents_list */

  /* 创建结果 MRO 表 */
  lua_pushstring(L, CLASS_KEY_MRO);
  lua_newtable(L);
  int result_idx = lua_gettop(L);

  /* 类自身在 MRO 第一位 */
  lua_pushvalue(L, class_idx);
  lua_rawseti(L, result_idx, 1);
  int result_len = 1;

  /* 创建位置追踪表：positions[i] = 列表 i 的当前游标 */
  lua_newtable(L);
  int pos_idx = lua_gettop(L);
  for (int i = 1; i <= total_lists; i++) {
    lua_pushinteger(L, 1);  /* 从位置 1 开始 */
    lua_rawseti(L, pos_idx, i);
  }

  /* 合并循环：重复直到无法找到合法的头元素 */
  int found;
  do {
    found = 0;

    for (int li = 1; li <= total_lists && !found; li++) {
      /* 获取列表 li 的当前游标 */
      lua_rawgeti(L, pos_idx, li);
      int cur_pos = lua_isinteger(L, -1) ? (int)lua_tointeger(L, -1) : 1;
      lua_pop(L, 1);

      /* 获取列表 li */
      lua_rawgeti(L, lin_idx, li);
      int list_idx = lua_gettop(L);

      if (!lua_istable(L, list_idx)) {
        lua_pop(L, 1);
        continue;
      }

      int list_len = (int)luaL_len(L, list_idx);

      if (cur_pos > list_len) {
        lua_pop(L, 1);  /* 列表已耗尽 */
        continue;
      }

      /* 获取头元素 */
      lua_rawgeti(L, list_idx, cur_pos);
      int head_idx = lua_gettop(L);
      /* 栈: ... lin, pos, MRO, result, pos_table, list_li, head */

      /* 检查 head 是否在其他列表的尾部出现 */
      int bad = 0;
      for (int lj = 1; lj <= total_lists && !bad; lj++) {
        if (lj == li) continue;

        lua_rawgeti(L, lin_idx, lj);  /* 获取其他列表 */
        int other_idx = lua_gettop(L);

        if (!lua_istable(L, other_idx)) {
          lua_pop(L, 1);
          continue;
        }

        int other_len = (int)luaL_len(L, other_idx);
        /* 检查尾部（位置 2..n） */
        for (int k = 2; k <= other_len; k++) {
          lua_rawgeti(L, other_idx, k);
          if (lua_rawequal(L, head_idx, -1)) {
            bad = 1;
            lua_pop(L, 1);
            break;
          }
          lua_pop(L, 1);
        }
        lua_pop(L, 1);  /* 弹出 other list */
      }

      if (!bad) {
        /* 合法的头元素：加入结果 */
        result_len++;
        lua_pushvalue(L, head_idx);  /* 复制 head */
        lua_rawseti(L, result_idx, result_len);

        /* 在包含此 head 的所有列表中推进游标 */
        for (int lj = 1; lj <= total_lists; lj++) {
          lua_rawgeti(L, lin_idx, lj);  /* 获取列表 lj */
          int adv_idx = lua_gettop(L);

          if (!lua_istable(L, adv_idx)) {
            lua_pop(L, 1);
            continue;
          }

          int adv_len = (int)luaL_len(L, adv_idx);

          lua_rawgeti(L, pos_idx, lj);
          int adv_pos = lua_isinteger(L, -1) ? (int)lua_tointeger(L, -1) : 1;
          lua_pop(L, 1);

          if (adv_pos <= adv_len) {
            lua_rawgeti(L, adv_idx, adv_pos);
            if (lua_rawequal(L, head_idx, -1)) {
              /* 当前元素匹配 head，推进游标 */
              lua_pushinteger(L, adv_pos + 1);
              lua_rawseti(L, pos_idx, lj);
            }
            lua_pop(L, 1);
          }

          lua_pop(L, 1);  /* 弹出 adv list */
        }

        found = 1;
      }

      lua_pop(L, 2);  /* 弹出 head 和 list_li */
    }
  } while (found);

  /* 检查是否所有线性化列表都已耗尽 */
  for (int li = 1; li <= total_lists; li++) {
    lua_rawgeti(L, lin_idx, li);
    int check_idx = lua_gettop(L);

    if (lua_istable(L, check_idx)) {
      int check_len = (int)luaL_len(L, check_idx);

      lua_rawgeti(L, pos_idx, li);
      int check_pos = lua_isinteger(L, -1) ? (int)lua_tointeger(L, -1) : 1;
      lua_pop(L, 1);

      if (check_pos <= check_len) {
        /* 有未消耗的元素：MRO 不兼容 */
        lua_pop(L, 1);  /* 弹出 check list */
        /* 清理栈并报错 */
        lua_pop(L, 1);  /* 弹出 pos */
        lua_pop(L, 1);  /* 弹出 result */
        lua_pop(L, 1);  /* 弹出 "MRO" */
        lua_pop(L, 1);  /* 弹出 linearizations */
        luaL_error(L, "incompatible MRO: cannot create a consistent method resolution order");
        return;
      }
    }
    lua_pop(L, 1);  /* 弹出 check list */
  }

  /* 清理并存储结果 */
  lua_pop(L, 1);     /* 弹出 pos */
  /* 栈: [..., class, linearizations, "MRO", result] */
  lua_remove(L, -3); /* 移除 linearizations（在 "MRO" 下方） */
  /* 栈: [..., class, "MRO", result] */
  /* 存储 MRO：class["__mro"] = result */
  lua_rawset(L, class_idx);
}

/* 操作符元方法列表，自动从METHODS表安装到对象元表 */
static const char *operator_methods[] = {
  "__add", "__sub", "__mul", "__div", "__mod", "__pow",
  "__concat", "__eq", "__lt", "__le", "__len", "__unm",
  "__band", "__bor", "__bxor", "__bnot", "__shl", "__shr",
  NULL
};

/*
** 深拷贝值的辅助函数
** 使用 seen 表处理循环引用（original -> clone 映射）
** 参数：
**   L - Lua 状态机
**   idx - 要拷贝的值在栈中的索引
**   seen_idx - seen 映射表在栈中的索引
** 说明：
**   递归拷贝表、对象、map等复合类型
**   基本类型（number、string、boolean）直接返回
*/
static void clone_value(lua_State *L, int idx, int seen_idx) {
  idx = lua_absindex(L, idx);
  seen_idx = lua_absindex(L, seen_idx);
  
  int t = lua_type(L, idx);
  
  switch (t) {
    case LUA_TNUMBER:
    case LUA_TSTRING:
    case LUA_TBOOLEAN:
    case LUA_TNIL:
      /* 基本类型直接复制 */
      lua_pushvalue(L, idx);
      return;
      
    case LUA_TTABLE: {
      /* 检查是否已经拷贝过（循环引用处理） */
      lua_pushvalue(L, idx);
      lua_rawget(L, seen_idx);
      if (!lua_isnil(L, -1)) {
        /* 已经拷贝过，返回缓存的拷贝 */
        return;
      }
      lua_pop(L, 1);  /* 弹出 nil */
      
      /* 创建新表 */
      lua_newtable(L);
      int new_idx = lua_gettop(L);
      
      /* 缓存映射：seen[原始表] = 新表 */
      lua_pushvalue(L, idx);
      lua_pushvalue(L, new_idx);
      lua_rawset(L, seen_idx);
      
      /* 共享元表（避免递归克隆类定义等复杂元表） */
      if (lua_getmetatable(L, idx)) {
        lua_setmetatable(L, new_idx);
      }
      
      /* 递归拷贝所有键值对 */
      lua_pushnil(L);
      while (lua_next(L, idx) != 0) {
        /* Stack: [..., new_table, key, value] */
        /* 保存原始 key 副本供 lua_next 下一次迭代使用 */
        lua_pushvalue(L, -2);  /* [..., new_table, key, value, key_copy] */
        
        /* 克隆 key 和 value */
        clone_value(L, -3, seen_idx);  /* 克隆 key: [..., new_table, key, value, key_copy, cloned_key] */
        clone_value(L, -3, seen_idx);  /* 克隆 value: [..., new_table, key, value, key_copy, cloned_key, cloned_value] */
        
        /* 设置到新表：new_table[cloned_key] = cloned_value */
        lua_rawset(L, new_idx);  /* 弹出 cloned_key 和 cloned_value: [..., new_table, key, value, key_copy] */
        
        /* 恢复 key 供 lua_next 继续遍历 */
        lua_pop(L, 2);  /* 弹出 value 和 key_copy: [..., new_table, key] */
      }
      
      return;
    }
    
    case LUA_TFUNCTION: {
      /* 函数直接引用（不深拷贝） */
      lua_pushvalue(L, idx);
      return;
    }
    
    default: {
      /* 其他类型（userdata、thread等）直接引用 */
      lua_pushvalue(L, idx);
      return;
    }
  }
}

/*
** 深拷贝对象
** 参数：
**   L - Lua 状态机
**   obj_idx - 对象在栈中的索引
** 说明：
**   创建对象的新副本，递归拷贝所有字段
**   自动处理循环引用
**   新对象保持与原始对象相同的类归属
*/
static int luaC_clone_wrap(lua_State *L) {
  luaL_checkany(L, 1);
  int orig_idx = 1;
  
  /* 创建 seen 映射表用于处理循环引用 */
  lua_newtable(L);
  int seen_idx = lua_gettop(L);
  
  if (lua_type(L, orig_idx) != LUA_TTABLE) {
    /* 非表类型直接克隆 */
    clone_value(L, orig_idx, seen_idx);
    return 1;
  }
  
  /* 对象是表：创建新表，共享元表，直接复制内部字段 */
  lua_newtable(L);
  int cloned_idx = lua_gettop(L);
  
  /* 缓存映射：seen[原始表] = 克隆表 */
  lua_pushvalue(L, orig_idx);
  lua_pushvalue(L, cloned_idx);
  lua_rawset(L, seen_idx);
  
  /* 共享元表 */
  if (lua_getmetatable(L, orig_idx)) {
    lua_setmetatable(L, cloned_idx);
  }
  
  /* 直接复制内部字段（不通过 clone_value，避免递归克隆类定义） */
  const char *internal_keys[] = {
    OBJ_KEY_CLASS, OBJ_KEY_ISOBJ, NULL
  };
  for (int i = 0; internal_keys[i] != NULL; i++) {
    lua_pushstring(L, internal_keys[i]);
    lua_rawget(L, orig_idx);
    if (!lua_isnil(L, -1)) {
      lua_pushstring(L, internal_keys[i]);
      lua_pushvalue(L, -2);
      lua_rawset(L, cloned_idx);
    }
    lua_pop(L, 1);  /* 弹出值或 nil */
  }
  
  /* 深拷贝私有数据表 */
  lua_pushstring(L, OBJ_KEY_PRIVATES);
  lua_rawget(L, orig_idx);
  if (lua_istable(L, -1)) {
    clone_value(L, -1, seen_idx);
    lua_pushstring(L, OBJ_KEY_PRIVATES);
    lua_pushvalue(L, -2);
    lua_rawset(L, cloned_idx);
    lua_pop(L, 2);  /* 弹出克隆的私有表和原始私有表 */
  } else {
    lua_pop(L, 1);  /* 弹出 nil */
  }
  
  /* 克隆用户字段：遍历原始表，跳过内部字段 */
  lua_pushnil(L);
  while (lua_next(L, orig_idx) != 0) {
    /* Stack: [orig, seen, cloned, key, value] */
    /* 检查是否是内部字段（以 __ 开头） */
    int is_internal = 0;
    if (lua_type(L, -2) == LUA_TSTRING) {
      const char *k = lua_tostring(L, -2);
      if (k[0] == '_' && k[1] == '_') {
        is_internal = 1;
      }
    }
    
    if (is_internal) {
      lua_pop(L, 1);  /* 弹出 value，保留 key 供 lua_next */
    } else {
      /* 保存 key 副本 */
      lua_pushvalue(L, -2);  /* [orig, seen, cloned, key, value, key_copy] */
      /* 克隆 value */
      clone_value(L, -2, seen_idx);  /* [orig, seen, cloned, key, value, key_copy, cloned_value] */
      /* 设置到克隆表：cloned[key_copy] = cloned_value */
      lua_rawset(L, cloned_idx);  /* 弹出 key_copy 和 cloned_value: [orig, seen, cloned, key, value] */
      lua_pop(L, 1);  /* 弹出 value: [orig, seen, cloned, key] */
    }
  }
  
  /* 弹出 seen 表 */
  lua_remove(L, seen_idx);
  
  return 1;
}

/*
** 创建类的实例对象
** 支持自动调用父类构造函数链
*/
void luaC_newobject(lua_State *L, int class_idx, int nargs) {
  LUA_LOGI("[NEWOBJ] luaC_newobject ENTRY class_idx=%d nargs=%d top=%d", class_idx, nargs, lua_gettop(L));
  class_idx = absindex(L, class_idx);
  LUA_LOGI("[NEWOBJ] after absindex: class_idx=%d", class_idx);
  
  /* 检查是否是有效的类 */
  if (!luaC_isclass(L, class_idx)) {
    LUA_LOGI("[NEWOBJ] ERROR: not a class!");
    luaL_error(L, "attempt to instantiate a non-class value");
    return;
  }
  LUA_LOGI("[NEWOBJ] isclass check passed");
  
  /* 检查是否是抽象类（使用rawget避免触发类的__index） */
  lua_pushstring(L, CLASS_KEY_FLAGS);
  lua_rawget(L, class_idx);
  if (lua_isinteger(L, -1)) {
    int flags = (int)lua_tointeger(L, -1);
    LUA_LOGI("[NEWOBJ] flags=%d", flags);
    if (flags & CLASS_FLAG_ABSTRACT) {
      luaL_error(L, "cannot instantiate abstract class");
      return;
    }
    if (flags & CLASS_FLAG_INTERFACE) {
      luaL_error(L, "cannot instantiate interface");
      return;
    }
  }
  lua_pop(L, 1);
  LUA_LOGI("[NEWOBJ] flags check done, calling verify_abstracts");
  
  /* 验证所有抽象方法都已实现（包括参数数量验证） */
  luaC_verify_abstracts(L, class_idx);
  LUA_LOGI("[NEWOBJ] verify_abstracts done");
  
  /* 验证所有接口方法都已正确实现（包括参数数量验证） */
  luaC_verify_interfaces(L, class_idx);
  LUA_LOGI("[NEWOBJ] verify_interfaces done");
  
  /* 验证所有trait require方法都已实现 */
  luaC_verify_trait_requires(L, class_idx);
  LUA_LOGI("[NEWOBJ] verify_trait_requires done");
  
  /* 创建对象表 */
  lua_newtable(L);
  int obj_idx = lua_gettop(L);
  LUA_LOGI("[NEWOBJ] object table created, obj_idx=%d top=%d", obj_idx, lua_gettop(L));
  
  /* 保存对类的引用（使用rawset因为对象还没有元表） */
  lua_pushstring(L, OBJ_KEY_CLASS);
  lua_pushvalue(L, class_idx);
  lua_rawset(L, obj_idx);
  
  /* 标记为对象 */
  lua_pushstring(L, OBJ_KEY_ISOBJ);
  lua_pushboolean(L, 1);
  lua_rawset(L, obj_idx);
  
  /* 创建对象私有数据表 */
  lua_pushstring(L, OBJ_KEY_PRIVATES);
  lua_newtable(L);
  lua_rawset(L, obj_idx);
  
  /* 设置 __super 字段指向父类，供 super 关键字使用 */
  lua_pushstring(L, CLASS_KEY_PARENT);
  lua_rawget(L, class_idx);
  if (lua_istable(L, -1)) {
    LUA_LOGI("[NEWOBJ] has parent, setting __super");
    lua_pushstring(L, "__super");
    lua_pushvalue(L, -2);
    lua_rawset(L, obj_idx);
  } else {
    LUA_LOGI("[NEWOBJ] no parent");
  }
  lua_pop(L, 1);  /* 弹出parent或nil */
  LUA_LOGI("[NEWOBJ] metatable setup start, top=%d", lua_gettop(L));
  
  /* 创建并设置对象的元表 */
  lua_newtable(L);
  int mt_idx = lua_gettop(L);
  
  /* 设置__index元方法 */
  lua_pushcfunction(L, object_index);
  lua_setfield(L, mt_idx, "__index");
  
  /* 设置__newindex元方法 */
  lua_pushcfunction(L, object_newindex);
  lua_setfield(L, mt_idx, "__newindex");
  
  /* 设置__tostring元方法 */
  lua_pushcfunction(L, object_tostring);
  lua_setfield(L, mt_idx, "__tostring");
  
  /* 设置 clone 方法 */
  lua_pushcfunction(L, luaC_clone_wrap);
  lua_setfield(L, mt_idx, "clone");
  
  /* 检查类是否有__gc方法，并安装操作符元方法到对象元表 */
  lua_pushstring(L, CLASS_KEY_METHODS);
  lua_rawget(L, class_idx);
  if (lua_istable(L, -1)) {
    /* 检查__gc析构方法 */
    lua_pushstring(L, CLASS_KEY_DESTRUCTOR);
    lua_rawget(L, -2);
    if (lua_isfunction(L, -1)) {
      lua_setfield(L, mt_idx, "__gc");
    } else {
      lua_pop(L, 1);
    }
    
    /* 遍历操作符元方法，自动安装到对象元表 */
    const char **op = operator_methods;
    while (*op != NULL) {
      lua_pushstring(L, *op);
      lua_rawget(L, -2);  /* 从METHODS表获取操作符方法 */
      if (lua_isfunction(L, -1)) {
        lua_setfield(L, mt_idx, *op);
      } else {
        lua_pop(L, 1);
      }
      op++;
    }
  }
  lua_pop(L, 1);
  
  /* 应用元表 */
  lua_setmetatable(L, obj_idx);
  
  /* 构造期标记：允许 sealed 类在 init 中初始化字段 */
  lua_pushstring(L, "__constructing");
  lua_pushboolean(L, 1);
  lua_rawset(L, obj_idx);
  
  /* 创建临时标记表，用于跟踪已通过super调用的构造函数，避免双重调用 */
  lua_newtable(L);
  lua_pushstring(L, OBJ_KEY_INIT_CALLED);
  lua_pushvalue(L, -2);
  lua_rawset(L, obj_idx);
  /* 标记表现在栈顶，后续遍历中会用到 */
  
  /* 按 MRO 顺序调用构造函数链 */
  lua_pushstring(L, CLASS_KEY_MRO);
  lua_rawget(L, class_idx);
  if (lua_istable(L, -1)) {
    int mro_len = (int)luaL_len(L, -1);
    
    /* 先扫描找到最派生类（MRO 中第一个有自己 init 的类） */
    int most_derived_init = -1;
    for (int si = 1; si <= mro_len; si++) {
      lua_rawgeti(L, -1, si);
      int scan_class = lua_gettop(L);
      lua_pushstring(L, CLASS_KEY_METHODS);
      lua_rawget(L, scan_class);
      if (lua_istable(L, -1)) {
        lua_pushstring(L, CLASS_KEY_INIT);
        lua_rawget(L, -2);
        if (!lua_isfunction(L, -1)) {
          lua_pop(L, 1);
          lua_pushstring(L, CLASS_KEY_INIT_LEGACY);
          lua_rawget(L, -2);
        }
        if (lua_isfunction(L, -1)) {
          most_derived_init = si;
          lua_pop(L, 2);  /* 移除init和methods */
          lua_pop(L, 1);  /* 移除scan_class */
          break;
        }
        lua_pop(L, 1);  /* 移除init/nil */
      }
      lua_pop(L, 1);  /* 移除methods/nil */
      lua_pop(L, 1);  /* 移除scan_class */
    }
    
    /* 只调用最派生类的构造函数（传递用户参数），父类构造由super()链式触发
       这避免了父类无参自动调用和super()有参调用的双重调用问题
       注意：如果子类有init但没有调用super()，父类构造不会被自动调用（Python风格） */
    if (most_derived_init > 0) {
      lua_rawgeti(L, -1, most_derived_init);
      int current_class = lua_gettop(L);
      
      lua_pushstring(L, CLASS_KEY_METHODS);
      lua_rawget(L, current_class);
      if (lua_istable(L, -1)) {
        lua_pushstring(L, CLASS_KEY_INIT);
        lua_rawget(L, -2);
        if (!lua_isfunction(L, -1)) {
          lua_pop(L, 1);
          lua_pushstring(L, CLASS_KEY_INIT_LEGACY);
          lua_rawget(L, -2);
        }
        if (lua_isfunction(L, -1)) {
          lua_pushvalue(L, obj_idx);  /* self */
          int first_arg = class_idx + 1;
          for (int j = 0; j < nargs; j++) {
            lua_pushvalue(L, first_arg + j);
          }
          lua_call(L, nargs + 1, 0);
        } else {
          lua_pop(L, 1);
        }
      }
      lua_pop(L, 1);  /* methods */
      lua_pop(L, 1);  /* current_class */
    }
  }
  lua_pop(L, 1);  /* 弹出 MRO 表 */
  
  /* 清理临时标记表，避免内存泄漏 */
  lua_pushstring(L, OBJ_KEY_INIT_CALLED);
  lua_pushnil(L);
  lua_rawset(L, obj_idx);
  lua_pop(L, 1);  /* 弹出init_called表 */
  
  /* 清除构造期标记（sealed 字段检查从此生效） */
  lua_pushstring(L, "__constructing");
  lua_pushnil(L);
  lua_rawset(L, obj_idx);
  
  /* 确保对象在栈顶 */
  lua_pushvalue(L, obj_idx);
  lua_remove(L, obj_idx);
}


/*
** 调用父类方法
** 通过当前调用帧识别正在执行的方法，沿MRO查找下一个实现，正确处理多层继承
*/
void luaC_super(lua_State *L, int obj_idx, TString *method) {
  int entry_top = lua_gettop(L);  /* 入口栈顶：此时self在entry_top位置 */
  obj_idx = absindex(L, obj_idx);
  int nargs_init = (strcmp(getstr(method), CLASS_KEY_INIT) == 0 ||
                    strcmp(getstr(method), CLASS_KEY_INIT_LEGACY) == 0);

  /* 获取当前正在执行的LClosure指针，用于MRO定位 */
  LClosure *cur_closure = NULL;
  StkId func = L->ci->func.p;
  if (func >= L->stack.p && func < L->top.p && ttisLclosure(s2v(func))) {
    cur_closure = clLvalue(s2v(func));
  }

  /* 获取对象的类（使用rawget避免触发__index递归） */
  lua_pushstring(L, OBJ_KEY_CLASS);
  lua_rawget(L, obj_idx);  /* class */
  if (!lua_istable(L, -1)) {
    lua_settop(L, entry_top);
    lua_pushnil(L);
    return;
  }

  /* 获取MRO表 */
  lua_pushstring(L, CLASS_KEY_MRO);
  lua_rawget(L, -2);  /* MRO */
  if (!lua_istable(L, -1)) {
    /* 没有MRO，回退到直接取__parent */
    lua_pop(L, 1);  /* 弹出nil MRO */
    lua_pushstring(L, CLASS_KEY_PARENT);
    lua_rawget(L, -2);  /* parent */
    if (!lua_istable(L, -1)) {
      lua_settop(L, entry_top);
      lua_pushnil(L);
      return;
    }
    /* 回退：直接在parent.methods中查找 */
    lua_pushstring(L, CLASS_KEY_METHODS);
    lua_rawget(L, -2);
    if (lua_istable(L, -1)) {
      lua_pushlstring(L, getstr(method), tsslen(method));
      lua_rawget(L, -2);
      /* 找到方法，直接放到self之上 */
      if (lua_isfunction(L, -1)) {
        lua_pushvalue(L, -1);  /* dup method */
        lua_insert(L, entry_top + 1);  /* 移到self上面 */
        lua_settop(L, entry_top + 1);  /* 截断栈到 [..., self, method] */
        return;
      }
      lua_pop(L, 1);
    }
    /* super.field 回退：父类无同名方法时读取实例自身字段 */
    lua_settop(L, entry_top);
    lua_pushlstring(L, getstr(method), tsslen(method));
    lua_rawget(L, obj_idx);
    return;
  }
  int mro_len = (int)luaL_len(L, -1);

  /* 在MRO中定位当前方法所属类 */
  int current_mro_idx = -1;
  int start_idx = 2;  /* 默认从MRO第2项（父类）开始查找 */
  if (cur_closure && mro_len > 0) {
    for (int i = 1; i <= mro_len; i++) {
      lua_rawgeti(L, -1, i);  /* mro_class */
      if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        continue;
      }
      lua_pushstring(L, CLASS_KEY_METHODS);
      lua_rawget(L, -2);  /* methods */
      if (!lua_istable(L, -1)) {
        lua_pop(L, 2);
        continue;
      }
      lua_pushlstring(L, getstr(method), tsslen(method));
      lua_rawget(L, -2);  /* candidate_method */
      if (lua_isfunction(L, -1)) {
        const LClosure *cand = clLvalue(s2v(L->top.p - 1));
        if (cand == cur_closure) {
          current_mro_idx = i;
          lua_pop(L, 3);
          break;
        }
      }
      lua_pop(L, 3);
    }
  }

  if (current_mro_idx > 0) {
    start_idx = current_mro_idx + 1;
  }

  /* 从start_idx开始查找父类方法 */
  for (int i = start_idx; i <= mro_len; i++) {
    lua_rawgeti(L, entry_top + 2, i);  /* cand_class (MRO在entry_top+2位置) */
    if (!lua_istable(L, -1)) {
      lua_pop(L, 1);
      continue;
    }
    int cand_class = lua_gettop(L);

    lua_pushstring(L, CLASS_KEY_METHODS);
    lua_rawget(L, cand_class);  /* methods */
    if (lua_istable(L, -1)) {
      lua_pushlstring(L, getstr(method), tsslen(method));
      lua_rawget(L, -2);  /* method value */
      if (lua_isfunction(L, -1)) {
        /* 如果是init方法，标记父类已被调用 */
        if (nargs_init) {
          lua_pushstring(L, OBJ_KEY_INIT_CALLED);
          lua_rawget(L, obj_idx);
          if (lua_istable(L, -1)) {
            lua_pushvalue(L, cand_class);
            lua_pushboolean(L, 1);
            lua_rawset(L, -3);
          }
          lua_pop(L, 1);
        }

        /* 找到方法：把方法放到self之上，截断栈 */
        lua_pushvalue(L, -1);  /* dup method */
        lua_insert(L, entry_top + 1);  /* 移到self上面 */
        lua_settop(L, entry_top + 1);  /* 栈: [..., self, method] */
        return;
      }
      lua_pop(L, 1);
    }
    lua_pop(L, 2);  /* methods, cand_class */
  }

  /* 没找到父类方法：super.field 回退读取实例自身字段（字段为实例共享，
     super.tag 语义等同 self.tag）；若字段也不存在则得到 nil */
  lua_settop(L, entry_top);
  lua_pushlstring(L, getstr(method), tsslen(method));
  lua_rawget(L, obj_idx);
}


/*
** 设置类方法
*/
void luaC_setmethod(lua_State *L, int class_idx, TString *name, int func_idx) {
  class_idx = absindex(L, class_idx);
  func_idx = absindex(L, func_idx);
  
  lua_pushstring(L, CLASS_KEY_PARENT);
  lua_rawget(L, class_idx);
  if (lua_istable(L, -1)) {
    int parent_idx = lua_gettop(L);
    lua_pushstring(L, CLASS_KEY_FINALS);
    lua_rawget(L, parent_idx);
    if (lua_istable(L, -1)) {
      lua_pushlstring(L, getstr(name), tsslen(name));
      lua_rawget(L, -2);
      if (lua_toboolean(L, -1)) {
        const char *parent_name = get_class_name_str(L, parent_idx);
        luaL_error(L, "cannot override final method '%s' of class '%s'",
                   getstr(name), parent_name);
        return;
      }
      lua_pop(L, 1);
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);

  /* 使用rawget/rawset访问类表避免触发元方法 */
  lua_pushstring(L, CLASS_KEY_METHODS);
  lua_rawget(L, class_idx);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_pushstring(L, CLASS_KEY_METHODS);
    lua_insert(L, -2);
    lua_rawset(L, class_idx);
  }
  
  lua_pushlstring(L, getstr(name), tsslen(name));
  lua_pushvalue(L, func_idx);
  lua_rawset(L, -3);

  /* 别名：new <-> init
     当用户定义 function new(...) 时，自动同步到 init（仅当 init 不存在时）
     当用户定义 function init(...) 时，自动同步到 new（仅当 new 不存在时）
     当用户定义 function __init__(...) 时，自动同步到 init 和 new，并输出弃用警告
     注意：不同步覆盖已有值，避免 static function new 覆盖构造函数 init */
  if (strcmp(getstr(name), "new") == 0) {
    /* 设置 new 时同步 init：仅当 init 不存在时才同步，避免覆盖已有构造函数 */
    /* 此时栈: [..., METHODS]，push key 后为 [..., METHODS, key]，故表在 -2 */
    lua_pushstring(L, CLASS_KEY_INIT);
    lua_rawget(L, -2);  /* METHODS.init */
    if (lua_isnil(L, -1)) {
      lua_pop(L, 1);
      lua_pushstring(L, CLASS_KEY_INIT);
      lua_pushvalue(L, func_idx);
      lua_rawset(L, -3);
    } else {
      lua_pop(L, 1);
    }
  } else if (strcmp(getstr(name), CLASS_KEY_INIT) == 0) {
    /* 设置 init 时同步 new：仅当 new 不存在时才同步 */
    lua_pushliteral(L, "new");
    lua_rawget(L, -2);  /* METHODS.new */
    if (lua_isnil(L, -1)) {
      lua_pop(L, 1);
      lua_pushliteral(L, "new");
      lua_pushvalue(L, func_idx);
      lua_rawset(L, -3);
    } else {
      lua_pop(L, 1);
    }
  } else if (strcmp(getstr(name), CLASS_KEY_INIT_LEGACY) == 0) {
    /* __init__ 已弃用：同时写入 init 和 new（仅当目标不存在时） */
    lua_pushstring(L, CLASS_KEY_INIT);
    lua_rawget(L, -2);  /* METHODS.init */
    if (lua_isnil(L, -1)) {
      lua_pop(L, 1);
      lua_pushstring(L, CLASS_KEY_INIT);
      lua_pushvalue(L, func_idx);
      lua_rawset(L, -3);
    } else {
      lua_pop(L, 1);
    }
    lua_pushliteral(L, "new");
    lua_rawget(L, -2);  /* METHODS.new */
    if (lua_isnil(L, -1)) {
      lua_pop(L, 1);
      lua_pushliteral(L, "new");
      lua_pushvalue(L, func_idx);
      lua_rawset(L, -3);
    } else {
      lua_pop(L, 1);
    }
    fprintf(stderr, "[WARNING] class method '%s' uses deprecated '__init__', use 'init' instead\n",
            getstr(name));
  }

  lua_pop(L, 1);
}


/*
** 设置静态成员
*/
void luaC_setstatic(lua_State *L, int class_idx, TString *name, int value_idx) {
  class_idx = absindex(L, class_idx);
  value_idx = absindex(L, value_idx);
  
  /* 使用rawget/rawset访问类表避免触发元方法 */
  lua_pushstring(L, CLASS_KEY_STATICS);
  lua_rawget(L, class_idx);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_pushstring(L, CLASS_KEY_STATICS);
    lua_insert(L, -2);
    lua_rawset(L, class_idx);
  }
  
  lua_pushlstring(L, getstr(name), tsslen(name));
  lua_pushvalue(L, value_idx);
  lua_rawset(L, -3);
  lua_pop(L, 1);
}


/*
** 获取属性（考虑继承链）
*/
void luaC_getprop(lua_State *L, int obj_idx, TString *key) {
  obj_idx = absindex(L, obj_idx);
  
  /* 首先在对象自身查找 */
  lua_pushlstring(L, getstr(key), tsslen(key));
  lua_rawget(L, obj_idx);
  if (!lua_isnil(L, -1)) {
    return;
  }
  lua_pop(L, 1);
  
  /* 在类方法中查找（使用rawget从对象获取类引用） */
  lua_pushstring(L, OBJ_KEY_CLASS);
  lua_rawget(L, obj_idx);
  while (lua_istable(L, -1)) {
    int current_class = lua_gettop(L);
    
    /* 使用rawget访问类表 */
    lua_pushstring(L, CLASS_KEY_METHODS);
    lua_rawget(L, current_class);
    if (lua_istable(L, -1)) {
      lua_pushlstring(L, getstr(key), tsslen(key));
      lua_rawget(L, -2);
      if (!lua_isnil(L, -1)) {
        lua_remove(L, -2);  /* 移除methods表 */
        lua_remove(L, -2);  /* 移除class */
        return;
      }
      lua_pop(L, 1);
    }
    lua_pop(L, 1);
    
    /* 继续查找父类（使用rawget） */
    lua_pushstring(L, CLASS_KEY_PARENT);
    lua_rawget(L, current_class);
    lua_remove(L, current_class);
  }
  
  lua_pop(L, 1);  /* 移除非表值 */
  lua_pushnil(L);
}


/*
** 设置属性
*/
void luaC_setprop(lua_State *L, int obj_idx, TString *key, int value_idx) {
  obj_idx = absindex(L, obj_idx);
  value_idx = absindex(L, value_idx);
  
  lua_pushlstring(L, getstr(key), tsslen(key));
  lua_pushvalue(L, value_idx);
  lua_rawset(L, obj_idx);
}


/*
** 检查对象是否是指定类的实例
** 修复说明：
** 1. class_idx 类型判定除了 __isclass（类），还支持 __isinterface（接口）
** 2. 继承链/接口链使用 BFS（todo 栈 + visited 集合），同时扫描 __parent 单链和 __parents 多继承数组，防环
*/
int luaC_instanceof(lua_State *L, int obj_idx, int class_idx) {
  obj_idx = absindex(L, obj_idx);
  class_idx = absindex(L, class_idx);

  if (lua_type(L, obj_idx) == LUA_TSTRUCT) {
      const TValue *o = index2value_helper(L, obj_idx);
      const TValue *c = index2value_helper(L, class_idx);
      if (structvalue(o)->def == hvalue(c)) return 1;
      return 0;
  }

  /* 检查是否是对象 */
  if (!luaC_isobject(L, obj_idx)) {
    return 0;
  }

  /* 检查class_idx是否是表（类和接口都是表），非表直接返回 0 */
  if (!lua_istable(L, class_idx)) {
    return 0;
  }

  /* 检查class_idx是否是类或接口（接口使用__flags的 CLASS_FLAG_INTERFACE 位） */
  if (!luaC_isclass(L, class_idx)) {
    /* 不是类，检查是否是接口 */
    lua_pushstring(L, CLASS_KEY_FLAGS);
    lua_rawget(L, class_idx);
    int flags = lua_isinteger(L, -1) ? (int)lua_tointeger(L, -1) : 0;
    lua_pop(L, 1);
    if (!(flags & CLASS_FLAG_INTERFACE)) {
      return 0;
    }
  }

  /* 获取对象的类（使用rawget避免触发__index递归） */
  lua_pushstring(L, OBJ_KEY_CLASS);
  lua_rawget(L, obj_idx);
  int obj_class = lua_gettop(L);

  /* ===== BFS 扫描所有类的继承链：__parent + __parents 数组 ===== */
  lua_newtable(L); int todo = lua_gettop(L);     /* todo 栈：数组 */
  lua_newtable(L); int visited = lua_gettop(L);  /* visited 集合：key=类, val=true */
  int todo_top = 0;
  int loop_limit = 10000;

  /* 初始：push obj_class 入 todo */
  lua_pushvalue(L, obj_class);
  todo_top++;
  lua_rawseti(L, todo, todo_top);

  int found = 0;
  while (todo_top > 0 && loop_limit-- > 0) {
    /* 1. 取出栈顶元素 todo[todo_top] */
    lua_rawgeti(L, todo, todo_top);
    int current = lua_gettop(L);
    /* 弹出：置 nil + 递减 */
    lua_pushnil(L);
    lua_rawseti(L, todo, todo_top);
    todo_top--;

    /* 2. 是否已访问？是则跳过 */
    lua_pushvalue(L, current);
    lua_rawget(L, visited);
    if (!lua_isnil(L, -1)) {
      lua_pop(L, 2);  /* nil + current */
      continue;
    }
    lua_pop(L, 1);  /* nil */

    /* 3. 标记 visited[current] = true */
    lua_pushvalue(L, current);
    lua_pushboolean(L, 1);
    lua_rawset(L, visited);

    /* 4. current == class_idx？找到 */
    if (lua_rawequal(L, current, class_idx)) {
      found = 1;
      break;
    }

    /* 5. 入栈 __parent（单父类） */
    lua_pushstring(L, CLASS_KEY_PARENT);
    lua_rawget(L, current);
    if (lua_istable(L, -1)) {
      todo_top++;
      lua_rawseti(L, todo, todo_top);
    } else {
      lua_pop(L, 1);
    }

    /* 6. 入栈 __parents[i]（多继承父类数组） */
    lua_pushstring(L, CLASS_KEY_PARENTS);
    lua_rawget(L, current);
    if (lua_istable(L, -1)) {
      int n = (int)lua_rawlen(L, -1);
      for (int i = 1; i <= n; i++) {
        lua_rawgeti(L, -1, i);
        if (lua_istable(L, -1)) {
          todo_top++;
          lua_rawseti(L, todo, todo_top);
        } else {
          lua_pop(L, 1);
        }
      }
      lua_pop(L, 1);  /* pop __parents 表 */
    } else {
      lua_pop(L, 1);
    }

    /* 7. 弹出 current */
    lua_pop(L, 1);
  }

  /* 清理 todo + visited + 可能残留的 current */
  lua_pop(L, lua_gettop(L) - obj_class);
  /* 此时栈上只剩 obj_class */

  if (found) {
    lua_pop(L, 1);  /* 弹 obj_class */
    return 1;
  }

  /* ===== 接口实现链检查：遍历对象类的 __interfaces 表，同样 BFS 扫描接口自身继承链 ===== */
  lua_pushstring(L, CLASS_KEY_INTERFACES);
  lua_rawget(L, obj_class);
  if (lua_istable(L, -1)) {
    int ifaces_idx = lua_gettop(L);
    int n = (int)lua_rawlen(L, ifaces_idx);

    /* 复用 BFS 栈结构，但栈已空，重建 todo2 + visited2 */
    lua_newtable(L); int todo2 = lua_gettop(L);
    lua_newtable(L); int visited2 = lua_gettop(L);
    int todo2_top = 0;

    /* 初始：把 __interfaces[i] 全部 push 入 todo2 */
    for (int i = 1; i <= n; i++) {
      lua_rawgeti(L, ifaces_idx, i);
      if (lua_istable(L, -1)) {
        todo2_top++;
        lua_rawseti(L, todo2, todo2_top);
      } else {
        lua_pop(L, 1);
      }
    }

    loop_limit = 10000;
    while (todo2_top > 0 && loop_limit-- > 0) {
      lua_rawgeti(L, todo2, todo2_top);
      int cur_iface = lua_gettop(L);
      lua_pushnil(L);
      lua_rawseti(L, todo2, todo2_top);
      todo2_top--;

      /* visited？ */
      lua_pushvalue(L, cur_iface);
      lua_rawget(L, visited2);
      if (!lua_isnil(L, -1)) {
        lua_pop(L, 2);
        continue;
      }
      lua_pop(L, 1);

      /* mark visited */
      lua_pushvalue(L, cur_iface);
      lua_pushboolean(L, 1);
      lua_rawset(L, visited2);

      /* rawequal？ */
      if (lua_rawequal(L, cur_iface, class_idx)) {
        found = 1;
        break;
      }

      /* 接口也可能 extends（__parent） 或多 extends（__parents） */
      lua_pushstring(L, CLASS_KEY_PARENT);
      lua_rawget(L, cur_iface);
      if (lua_istable(L, -1)) {
        todo2_top++;
        lua_rawseti(L, todo2, todo2_top);
      } else {
        lua_pop(L, 1);
      }
      lua_pushstring(L, CLASS_KEY_PARENTS);
      lua_rawget(L, cur_iface);
      if (lua_istable(L, -1)) {
        int n2 = (int)lua_rawlen(L, -1);
        for (int i = 1; i <= n2; i++) {
          lua_rawgeti(L, -1, i);
          if (lua_istable(L, -1)) {
            todo2_top++;
            lua_rawseti(L, todo2, todo2_top);
          } else {
            lua_pop(L, 1);
          }
        }
        lua_pop(L, 1);
      } else {
        lua_pop(L, 1);
      }

      lua_pop(L, 1);  /* pop cur_iface */
    }

    /* 清理 todo2 + visited2（弹出至 ifaces_idx 之上的都清掉） */
    lua_pop(L, lua_gettop(L) - ifaces_idx);
    /* pop ifaces_idx */
    lua_pop(L, 1);
  } else {
    /* pop 非表值 */
    lua_pop(L, 1);
  }

  /* 弹出 obj_class */
  lua_pop(L, 1);
  return found ? 1 : 0;
}


/*
** 检查值是否是一个类
** 使用rawget避免触发__index元方法
*/
int luaC_isclass(lua_State *L, int idx) {
  if (!lua_istable(L, idx)) {
    return 0;
  }
  return checkflag_raw(L, idx, CLASS_KEY_ISCLASS);
}


/*
** 检查值是否是一个对象实例
** 使用rawget避免触发__index元方法
*/
int luaC_isobject(lua_State *L, int idx) {
  if (!lua_istable(L, idx)) {
    return 0;
  }
  return checkflag_raw(L, idx, OBJ_KEY_ISOBJ);
}


/*
** 获取对象所属的类
*/
void luaC_getclass(lua_State *L, int obj_idx) {
  obj_idx = absindex(L, obj_idx);
  
  if (!luaC_isobject(L, obj_idx)) {
    lua_pushnil(L);
    return;
  }
  
  /* 使用rawget避免触发__index递归 */
  lua_pushstring(L, OBJ_KEY_CLASS);
  lua_rawget(L, obj_idx);
}


/*
** 获取类的父类
*/
void luaC_getparent(lua_State *L, int class_idx) {
  class_idx = absindex(L, class_idx);
  
  if (!luaC_isclass(L, class_idx)) {
    lua_pushnil(L);
    return;
  }
  
  /* 使用rawget避免触发__index递归 */
  lua_pushstring(L, CLASS_KEY_PARENT);
  lua_rawget(L, class_idx);
}


/*
** 获取类名
*/
const char *luaC_classname(lua_State *L, int class_idx) {
  class_idx = absindex(L, class_idx);
  
  if (!luaC_isclass(L, class_idx)) {
    return NULL;
  }
  
  /* 使用rawget避免触发__index递归 */
  lua_pushstring(L, CLASS_KEY_NAME);
  lua_rawget(L, class_idx);
  const char *name = lua_tostring(L, -1);
  lua_pop(L, 1);
  return name;
}


/*
** =====================================================================
** 接口相关函数实现
** =====================================================================
*/

/*
** 创建接口
** 参数：
**   L - Lua状态机
**   name - 接口名
**   parent_idx - 父接口在栈中的索引（-1表示无父接口）
*/
/*
** 设置接口继承关系（供 LBCTC 翻译调用）
** child_idx: 子接口，parent_idx: 父接口
*/
void lua_extendiface(lua_State *L, int child_idx, int parent_idx) {
  child_idx = absindex(L, child_idx);
  parent_idx = absindex(L, parent_idx);
  lua_pushvalue(L, parent_idx);
  lua_setfield(L, child_idx, CLASS_KEY_PARENT);
}

void luaC_newinterface(lua_State *L, TString *name, int parent_idx) {
  /* 创建接口表 */
  lua_newtable(L);
  int iface_idx = lua_gettop(L);
  
  /* 设置接口名 */
  lua_pushlstring(L, getstr(name), tsslen(name));
  lua_setfield(L, iface_idx, CLASS_KEY_NAME);
  
  /* 标记为接口 */
  lua_pushinteger(L, CLASS_FLAG_INTERFACE);
  lua_setfield(L, iface_idx, CLASS_KEY_FLAGS);
  
  setboolfield(L, iface_idx, CLASS_KEY_ISCLASS, 1);
  
  /* 创建方法表（用于声明接口方法签名） */
  lua_newtable(L);
  lua_setfield(L, iface_idx, CLASS_KEY_METHODS);
  
  /* 如果提供了父接口，设置__parent字段 */
  if (parent_idx >= 0) {
    parent_idx = absindex(L, parent_idx);
    lua_pushvalue(L, parent_idx);
    lua_setfield(L, iface_idx, CLASS_KEY_PARENT);
  }
}


/*
** 实现接口
** 递归遍历接口继承链，将接口本身及其所有父接口注册到类的接口列表中，
** 并将所有接口方法声明合并到类的抽象方法列表中
*/
void luaC_implement(lua_State *L, int class_idx, int interface_idx) {
  class_idx = absindex(L, class_idx);
  interface_idx = absindex(L, interface_idx);
  
  /* 获取或创建接口列表（使用rawget/rawset） */
  lua_pushstring(L, CLASS_KEY_INTERFACES);
  lua_rawget(L, class_idx);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_pushstring(L, CLASS_KEY_INTERFACES);
    lua_insert(L, -2);
    lua_rawset(L, class_idx);
  }
  
  /* 获取或创建抽象方法表 */
  int abstracts_made = 0;
  lua_pushstring(L, CLASS_KEY_ABSTRACTS);
  lua_rawget(L, class_idx);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_pushstring(L, CLASS_KEY_ABSTRACTS);
    lua_insert(L, -2);
    lua_rawset(L, class_idx);
    abstracts_made = 1;
  }
  /* 栈: ... interfaces_list, abstracts_table */
  int abstracts_idx = lua_gettop(L);
  
  /* 递归注册接口及其父接口，同时收集方法声明 */
  lua_pushvalue(L, interface_idx);
  while (lua_istable(L, -1)) {
    int current_iface = lua_gettop(L);
    
    /* 检查是否已注册（避免重复） */
    int n = (int)lua_rawlen(L, -3);  /* 接口列表在 abstracts_idx - 1 */
    int already = 0;
    for (int i = 1; i <= n; i++) {
      lua_rawgeti(L, abstracts_idx - 1, i);
      if (lua_rawequal(L, -1, current_iface)) {
        already = 1;
        lua_pop(L, 1);
        break;
      }
      lua_pop(L, 1);
    }
    
    if (!already) {
      lua_pushvalue(L, current_iface);
      lua_rawseti(L, abstracts_idx - 1, n + 1);
    }
    
    /* 收集当前接口的方法声明到抽象方法表 */
    lua_pushstring(L, CLASS_KEY_METHODS);
    lua_rawget(L, current_iface);
    if (lua_istable(L, -1)) {
      lua_pushnil(L);
      while (lua_next(L, -2) != 0) {
        /* 栈: ... methods_table, key(方法名), value(参数个数) */
        /* 如果抽象方法表中还没有该方法，则添加 */
        lua_pushvalue(L, -2);  /* 复制key */
        lua_rawget(L, abstracts_idx);
        if (lua_isnil(L, -1)) {
          lua_pop(L, 1);  /* 移除nil */
          lua_pushvalue(L, -1);  /* 复制key */
          lua_pushvalue(L, -3);  /* 复制value */
          lua_rawset(L, abstracts_idx);
        } else {
          lua_pop(L, 1);  /* 移除已存在的值 */
        }
        lua_pop(L, 1);  /* 移除value，保留key */
      }
    }
    lua_pop(L, 1);  /* 移除方法表 */
    
    /* 继续查找父接口 */
    lua_pushstring(L, CLASS_KEY_PARENT);
    lua_rawget(L, current_iface);
    lua_remove(L, current_iface);
  }
  lua_pop(L, 3);  /* 移除非表值、抽象方法表、接口列表 */
}


/*
** 检查类是否实现了接口
*/
int luaC_implements(lua_State *L, int class_idx, int interface_idx) {
  class_idx = absindex(L, class_idx);
  interface_idx = absindex(L, interface_idx);
  
  /* 使用rawget访问类表 */
  lua_pushstring(L, CLASS_KEY_INTERFACES);
  lua_rawget(L, class_idx);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return 0;
  }
  
  int n = (int)lua_rawlen(L, -1);
  for (int i = 1; i <= n; i++) {
    lua_rawgeti(L, -1, i);
    if (lua_rawequal(L, -1, interface_idx)) {
      lua_pop(L, 2);
      return 1;
    }
    lua_pop(L, 1);
  }
  
  lua_pop(L, 1);
  
  /* 检查父类（使用rawget） */
  lua_pushstring(L, CLASS_KEY_PARENT);
  lua_rawget(L, class_idx);
  if (lua_istable(L, -1)) {
    int result = luaC_implements(L, -1, interface_idx);
    lua_pop(L, 1);
    return result;
  }
  
  lua_pop(L, 1);
  return 0;
}


/*
** =====================================================================
** 反射 API 包装函数 - 暴露 C API 到 Lua 全局环境
** =====================================================================
*/

/* classof(obj) - 获取对象的类 */
static int luaC_classof_wrap(lua_State *L) {
  luaL_checkany(L, 1);
  luaC_getclass(L, 1);
  return 1;
}

/* getparent(class) - 获取类的父类 */
static int luaC_getparent_wrap(lua_State *L) {
  luaC_getparent(L, 1);
  return 1;
}

/* classname(class) - 获取类名 */
static int luaC_classname_wrap(lua_State *L) {
  const char *name = luaC_classname(L, 1);
  if (name) lua_pushstring(L, name);
  else lua_pushnil(L);
  return 1;
}

/* instanceof(obj, class) - 检查 obj 是否是 class 的实例 */
static int luaC_instanceof_wrap(lua_State *L) {
  int result = luaC_instanceof(L, 1, 2);
  lua_pushboolean(L, result);
  return 1;
}

/* isclass(value) - 检查是否是类 */
static int luaC_isclass_wrap(lua_State *L) {
  luaL_checkany(L, 1);
  int result = luaC_isclass(L, 1);
  lua_pushboolean(L, result);
  return 1;
}

/* isobject(value) - 检查是否是对象 */
static int luaC_isobject_wrap(lua_State *L) {
  luaL_checkany(L, 1);
  int result = luaC_isobject(L, 1);
  lua_pushboolean(L, result);
  return 1;
}

/* issubclass(child, parent) - 检查 child 是否是 parent 的子类 */
static int luaC_issubclass_wrap(lua_State *L) {
  int result = luaC_issubclass(L, 1, 2);
  lua_pushboolean(L, result);
  return 1;
}


/*
** 泛型类特化：为泛型类创建绑定类型参数的特化子类
** 参数：
**   L - Lua 状态机
**   class_idx - 泛型类在栈中的索引
**   type_args_idx - 类型参数列表（table）在栈中的索引
** 说明：
**   创建泛型类的子类，将类型参数绑定到具体类型。
**   特化后的类可以通过 ClassName<Type> 或 ClassName(Type) 创建。
**   调用后栈顶为特化后的类表，原泛型类保持不变。
*/
static int luaC_specialize(lua_State *L, int class_idx, int type_args_idx) {
  class_idx = absindex(L, class_idx);
  type_args_idx = absindex(L, type_args_idx);
  
  /* 获取泛型参数列表，验证参数数量 */
  lua_pushstring(L, CLASS_KEY_TYPEPARAMS);
  lua_rawget(L, class_idx);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    luaL_error(L, "class is not generic");
    return 0;
  }
  int num_params = (int)luaL_len(L, -1);
  lua_pop(L, 1);
  
  /* 创建特化类（作为泛型类的子类） */
  lua_newtable(L);
  int spec_idx = lua_gettop(L);
  
  /* 设置类标志 */
  lua_pushstring(L, CLASS_KEY_ISCLASS);
  lua_pushboolean(L, 1);
  lua_rawset(L, spec_idx);
  
  lua_pushstring(L, CLASS_KEY_FLAGS);
  lua_pushinteger(L, 0);
  lua_rawset(L, spec_idx);
  
  /* 设置父类为泛型基类 */
  lua_pushstring(L, CLASS_KEY_PARENT);
  lua_pushvalue(L, class_idx);
  lua_rawset(L, spec_idx);
  
  /* 设置泛型基类引用 */
  lua_pushstring(L, CLASS_KEY_GENERIC_BASE);
  lua_pushvalue(L, class_idx);
  lua_rawset(L, spec_idx);
  
  /* 设置类型参数绑定 */
  lua_pushstring(L, CLASS_KEY_TYPEARGS);
  lua_pushvalue(L, type_args_idx);
  lua_rawset(L, spec_idx);
  
  /* 设置 MRO */
  luaC_compute_mro(L, spec_idx);
  
  return 1;
}


/*
** 初始化类系统
*/
void luaC_initclass(lua_State *L) {
  /* 反射 API - 暴露到 Lua 全局环境 */
  lua_register(L, "classof",    luaC_classof_wrap);
  lua_register(L, "getparent",  luaC_getparent_wrap);
  lua_register(L, "classname",  luaC_classname_wrap);
  lua_register(L, "isinstance", luaC_instanceof_wrap);
  lua_register(L, "isclass",    luaC_isclass_wrap);
  lua_register(L, "isobject",   luaC_isobject_wrap);
  lua_register(L, "issubclass", luaC_issubclass_wrap);
  lua_register(L, "clone",      luaC_clone_wrap);
}


/*
** =====================================================================
** 访问控制相关函数实现
** =====================================================================
*/

/*
** 设置私有成员
** 参数：
**   L - Lua状态机
**   class_idx - 类在栈中的索引
**   name - 成员名
**   value_idx - 值在栈中的索引
** 说明：
**   将成员设置为私有，只有本类内部可以访问
*/
void luaC_setprivate(lua_State *L, int class_idx, TString *name, int value_idx) {
  class_idx = absindex(L, class_idx);
  value_idx = absindex(L, value_idx);
  
  /* 获取或创建私有成员表（使用rawget/rawset） */
  lua_pushstring(L, CLASS_KEY_PRIVATES);
  lua_rawget(L, class_idx);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_pushstring(L, CLASS_KEY_PRIVATES);
    lua_insert(L, -2);
    lua_rawset(L, class_idx);
  }
  
  /* 设置私有成员 */
  lua_pushlstring(L, getstr(name), tsslen(name));
  lua_pushvalue(L, value_idx);
  lua_rawset(L, -3);
  lua_pop(L, 1);
}


/*
** 设置受保护成员
** 参数：
**   L - Lua状态机
**   class_idx - 类在栈中的索引
**   name - 成员名
**   value_idx - 值在栈中的索引
** 说明：
**   将成员设置为受保护，本类和子类可以访问
*/
void luaC_setprotected(lua_State *L, int class_idx, TString *name, int value_idx) {
  class_idx = absindex(L, class_idx);
  value_idx = absindex(L, value_idx);
  
  /* 获取或创建受保护成员表（使用rawget/rawset） */
  lua_pushstring(L, CLASS_KEY_PROTECTED);
  lua_rawget(L, class_idx);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_pushstring(L, CLASS_KEY_PROTECTED);
    lua_insert(L, -2);
    lua_rawset(L, class_idx);
  }
  
  /* 设置受保护成员 */
  lua_pushlstring(L, getstr(name), tsslen(name));
  lua_pushvalue(L, value_idx);
  lua_rawset(L, -3);
  lua_pop(L, 1);
}


/*
** 检查类是否是另一个类的子类
** 参数：
**   L - Lua状态机
**   child_idx - 可能的子类在栈中的索引
**   parent_idx - 可能的父类在栈中的索引
** 返回值：
**   1 - 是子类（或同一个类）
**   0 - 不是子类
*/
int luaC_issubclass(lua_State *L, int child_idx, int parent_idx) {
  child_idx = absindex(L, child_idx);
  parent_idx = absindex(L, parent_idx);
  
  /* 检查是否是同一个类 */
  if (lua_rawequal(L, child_idx, parent_idx)) {
    return 1;
  }
  
  /* 检查是否是类 */
  if (!luaC_isclass(L, child_idx) || !luaC_isclass(L, parent_idx)) {
    return 0;
  }
  
  /* 沿继承链查找（使用rawget） */
  lua_pushstring(L, CLASS_KEY_PARENT);
  lua_rawget(L, child_idx);
  while (lua_istable(L, -1)) {
    if (lua_rawequal(L, -1, parent_idx)) {
      lua_pop(L, 1);
      return 1;
    }
    lua_pushstring(L, CLASS_KEY_PARENT);
    lua_rawget(L, -2);
    lua_remove(L, -2);  /* 移除旧的父类引用 */
  }
  
  lua_pop(L, 1);
  return 0;
}


/*
** 检查访问权限
** 参数：
**   L - Lua状态机
**   obj_idx - 对象在栈中的索引
**   key - 要访问的成员名
**   caller_class_idx - 调用者所属类的索引（0表示外部调用）
** 返回值：
**   ACCESS_PUBLIC - 可以公开访问
**   ACCESS_PROTECTED - 需要子类关系才能访问
**   ACCESS_PRIVATE - 需要同类才能访问
**   -1 - 成员不存在
*/
int luaC_checkaccess(lua_State *L, int obj_idx, TString *key, int caller_class_idx) {
  obj_idx = absindex(L, obj_idx);
  if (caller_class_idx != 0) {
    caller_class_idx = absindex(L, caller_class_idx);
  }
  
  const char *keystr = getstr(key);
  size_t keylen = tsslen(key);
  
  /* 获取对象的类（使用rawget避免触发__index递归） */
  lua_pushstring(L, OBJ_KEY_CLASS);
  lua_rawget(L, obj_idx);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return -1;
  }
  int obj_class_idx = lua_gettop(L);
  
  /* 检查公开成员（使用rawget访问类表） */
  lua_pushstring(L, CLASS_KEY_METHODS);
  lua_rawget(L, obj_class_idx);
  if (lua_istable(L, -1)) {
    lua_pushlstring(L, keystr, keylen);
    lua_rawget(L, -2);
    if (!lua_isnil(L, -1)) {
      lua_pop(L, 3);  /* 移除值、方法表、类 */
      return ACCESS_PUBLIC;
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  
  /* 检查受保护成员 */
  lua_pushstring(L, CLASS_KEY_PROTECTED);
  lua_rawget(L, obj_class_idx);
  if (lua_istable(L, -1)) {
    lua_pushlstring(L, keystr, keylen);
    lua_rawget(L, -2);
    if (!lua_isnil(L, -1)) {
      lua_pop(L, 3);  /* 移除值、受保护表、类 */
      return ACCESS_PROTECTED;
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  
  /* 检查私有成员 */
  lua_pushstring(L, CLASS_KEY_PRIVATES);
  lua_rawget(L, obj_class_idx);
  if (lua_istable(L, -1)) {
    lua_pushlstring(L, keystr, keylen);
    lua_rawget(L, -2);
    if (!lua_isnil(L, -1)) {
      lua_pop(L, 3);  /* 移除值、私有表、类 */
      return ACCESS_PRIVATE;
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  
  /* 沿继承链查找 */
  lua_pushstring(L, CLASS_KEY_PARENT);
  lua_rawget(L, obj_class_idx);
  lua_remove(L, obj_class_idx);  /* 移除对象类 */
  
  while (lua_istable(L, -1)) {
    int current_class = lua_gettop(L);
    
    /* 检查公开成员 */
    lua_pushstring(L, CLASS_KEY_METHODS);
    lua_rawget(L, current_class);
    if (lua_istable(L, -1)) {
      lua_pushlstring(L, keystr, keylen);
      lua_rawget(L, -2);
      if (!lua_isnil(L, -1)) {
        lua_pop(L, 3);  /* 移除值、方法表、类 */
        return ACCESS_PUBLIC;
      }
      lua_pop(L, 1);
    }
    lua_pop(L, 1);
    
    /* 检查受保护成员 */
    lua_pushstring(L, CLASS_KEY_PROTECTED);
    lua_rawget(L, current_class);
    if (lua_istable(L, -1)) {
      lua_pushlstring(L, keystr, keylen);
      lua_rawget(L, -2);
      if (!lua_isnil(L, -1)) {
        lua_pop(L, 3);
        return ACCESS_PROTECTED;
      }
      lua_pop(L, 1);
    }
    lua_pop(L, 1);
    
    /* 继续查找父类 */
    lua_pushstring(L, CLASS_KEY_PARENT);
    lua_rawget(L, current_class);
    lua_remove(L, current_class);
  }
  
  lua_pop(L, 1);
  return -1;  /* 成员不存在 */
}


/*
** =====================================================================
** 抽象方法和final方法相关函数实现
** =====================================================================
*/

/*
** 声明抽象方法
** 参数：
**   L - Lua状态机
**   class_idx - 类在栈中的索引
**   name - 方法名
**   nparams - 方法期望的参数个数（用于验证实现类的方法签名，-1表示不验证）
** 说明：
**   声明一个抽象方法，子类必须实现该方法
**   同时标记类为抽象类
*/
void luaC_setabstract(lua_State *L, int class_idx, TString *name, int nparams) {
  class_idx = absindex(L, class_idx);
  
  /* 获取或创建抽象方法表 */
  lua_pushstring(L, CLASS_KEY_ABSTRACTS);
  lua_rawget(L, class_idx);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_pushstring(L, CLASS_KEY_ABSTRACTS);
    lua_insert(L, -2);
    lua_rawset(L, class_idx);
  }
  
  /* 添加抽象方法名到表中（值为期望的参数个数，用于验证） */
  lua_pushlstring(L, getstr(name), tsslen(name));
  lua_pushinteger(L, nparams);  /* 存储参数个数，而不是布尔值 */
  lua_rawset(L, -3);
  lua_pop(L, 1);
  
  /* 标记类为抽象类 */
  lua_pushstring(L, CLASS_KEY_FLAGS);
  lua_rawget(L, class_idx);
  int flags = lua_isinteger(L, -1) ? (int)lua_tointeger(L, -1) : 0;
  lua_pop(L, 1);
  
  flags |= CLASS_FLAG_ABSTRACT;
  lua_pushstring(L, CLASS_KEY_FLAGS);
  lua_pushinteger(L, flags);
  lua_rawset(L, class_idx);
}


/*
** 设置final方法
** 参数：
**   L - Lua状态机
**   class_idx - 类在栈中的索引
**   name - 方法名
**   func_idx - 函数在栈中的索引
** 说明：
**   设置一个final方法，子类不能重写该方法
*/
void luaC_setfinal(lua_State *L, int class_idx, TString *name, int func_idx) {
  class_idx = absindex(L, class_idx);
  func_idx = absindex(L, func_idx);
  
  /* 先设置方法 */
  luaC_setmethod(L, class_idx, name, func_idx);
  
  /* 获取或创建final方法表 */
  lua_pushstring(L, CLASS_KEY_FINALS);
  lua_rawget(L, class_idx);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_pushstring(L, CLASS_KEY_FINALS);
    lua_insert(L, -2);
    lua_rawset(L, class_idx);
  }
  
  /* 添加final方法名到表中（值为true） */
  lua_pushlstring(L, getstr(name), tsslen(name));
  lua_pushboolean(L, 1);
  lua_rawset(L, -3);
  lua_pop(L, 1);
  
  /* 设置成员标志 */
  int flags = luaC_getmemberflags(L, class_idx, name);
  flags |= MEMBER_FINAL;
  luaC_setmemberflags(L, class_idx, name, flags);
}


/*
** 获取函数的参数个数
** 参数：
**   L - Lua状态机
**   func_idx - 函数在栈中的索引
** 返回值：
**   参数个数，如果不是函数则返回-1
*/
static int get_func_numparams(lua_State *L, int func_idx) {
  func_idx = absindex(L, func_idx);
  
  if (!lua_isfunction(L, func_idx)) {
    return -1;
  }
  
  /* 获取函数信息 */
  lua_Debug ar;
  lua_pushvalue(L, func_idx);
  if (lua_getinfo(L, ">u", &ar) == 0) {
    return -1;
  }
  
  return ar.nparams;
}


/*
** 在类的方法表中查找函数并获取其参数个数
** 参数：
**   L - Lua状态机
**   class_idx - 类在栈中的索引
**   method_name_idx - 方法名在栈中的索引
** 返回值：
**   参数个数，如果未找到返回-1
** 说明：
**   依次在 __methods, __protected, __privates 中查找
*/
static int get_method_numparams(lua_State *L, int class_idx, int method_name_idx) {
  class_idx = absindex(L, class_idx);
  method_name_idx = absindex(L, method_name_idx);
  
  /* 检查公开方法 */
  lua_pushstring(L, CLASS_KEY_METHODS);
  lua_rawget(L, class_idx);
  if (lua_istable(L, -1)) {
    lua_pushvalue(L, method_name_idx);
    lua_rawget(L, -2);
    if (lua_isfunction(L, -1)) {
      int nparams = get_func_numparams(L, -1);
      lua_pop(L, 2);
      return nparams;
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  
  /* 检查受保护方法 */
  lua_pushstring(L, CLASS_KEY_PROTECTED);
  lua_rawget(L, class_idx);
  if (lua_istable(L, -1)) {
    lua_pushvalue(L, method_name_idx);
    lua_rawget(L, -2);
    if (lua_isfunction(L, -1)) {
      int nparams = get_func_numparams(L, -1);
      lua_pop(L, 2);
      return nparams;
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  
  /* 检查私有方法 */
  lua_pushstring(L, CLASS_KEY_PRIVATES);
  lua_rawget(L, class_idx);
  if (lua_istable(L, -1)) {
    lua_pushvalue(L, method_name_idx);
    lua_rawget(L, -2);
    if (lua_isfunction(L, -1)) {
      int nparams = get_func_numparams(L, -1);
      lua_pop(L, 2);
      return nparams;
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  
  return -1;
}


/*
** 验证抽象方法是否都被实现（包括参数数量验证）
** 参数：
**   L - Lua状态机
**   class_idx - 类在栈中的索引
** 返回值：
**   1 - 所有抽象方法都已正确实现
**   0 - 存在未实现或参数不匹配的抽象方法（会产生错误）
** 说明：
**   检查类是否实现了所有继承的抽象方法，并验证参数数量是否匹配
*/
int luaC_verify_abstracts(lua_State *L, int class_idx) {
  class_idx = absindex(L, class_idx);
  
  /* 如果类本身是抽象类，不需要验证 */
  lua_pushstring(L, CLASS_KEY_FLAGS);
  lua_rawget(L, class_idx);
  if (lua_isinteger(L, -1)) {
    int flags = (int)lua_tointeger(L, -1);
    if (flags & CLASS_FLAG_ABSTRACT) {
      lua_pop(L, 1);
      return 1;  /* 抽象类不需要实现所有抽象方法 */
    }
  }
  lua_pop(L, 1);
  
  /* 收集所有需要实现的抽象方法（从继承链），value为参数个数 */
  lua_newtable(L);  /* 存储所有抽象方法 */
  int abstracts_idx = lua_gettop(L);
  
  /* 遍历继承链收集抽象方法 */
  lua_pushstring(L, CLASS_KEY_PARENT);
  lua_rawget(L, class_idx);
  while (lua_istable(L, -1)) {
    int parent_idx = lua_gettop(L);
    
    /* 获取父类的抽象方法表 */
    lua_pushstring(L, CLASS_KEY_ABSTRACTS);
    lua_rawget(L, parent_idx);
    if (lua_istable(L, -1)) {
      /* 复制所有抽象方法到收集表（key=方法名，value=参数个数） */
      lua_pushnil(L);
      while (lua_next(L, -2) != 0) {
        lua_pushvalue(L, -2);  /* 复制key */
        lua_pushvalue(L, -2);  /* 复制value（参数个数） */
        lua_rawset(L, abstracts_idx);
        lua_pop(L, 1);  /* 移除value */
      }
    }
    lua_pop(L, 1);  /* 移除抽象方法表 */
    
    /* 继续查找父类的父类 */
    lua_pushstring(L, CLASS_KEY_PARENT);
    lua_rawget(L, parent_idx);
    lua_remove(L, parent_idx);
  }
  lua_pop(L, 1);  /* 移除非表值 */
  
  /* 遍历所有需要实现的抽象方法，验证实现和参数数量 */
  lua_pushnil(L);
  while (lua_next(L, abstracts_idx) != 0) {
    /* 栈顶: value(期望参数个数), key(方法名) */
    int expected_params = lua_isinteger(L, -1) ? (int)lua_tointeger(L, -1) : -1;
    lua_pop(L, 1);  /* 移除value，保留key用于查找和迭代 */
    
    /* 获取实现方法的参数个数 */
    int actual_params = get_method_numparams(L, class_idx, lua_gettop(L));
    
    if (actual_params < 0) {
      /* 方法未实现 */
      const char *classname = get_class_name_str(L, class_idx);
      const char *methodname = lua_tostring(L, -1);
      luaL_error(L, "class '%s' must implement abstract method '%s'",
                 classname, methodname ? methodname : "?");
      return 0;
    }
    
    /* 验证参数数量是否匹配 */
    if (expected_params >= 0 && actual_params != expected_params && actual_params != expected_params + 1) {
      const char *classname = get_class_name_str(L, class_idx);
      const char *methodname = lua_tostring(L, -1);
      luaL_error(L, "method '%s' of class '%s' has mismatched parameter count: expected %d, got %d",
                 methodname ? methodname : "?", classname,
                 expected_params, actual_params);
      return 0;
    }
  }
  
  lua_pop(L, 1);  /* 移除abstracts表 */
  return 1;
}


/*
** 验证接口方法是否都被正确实现（包括参数数量验证）
** 参数：
**   L - Lua状态机
**   class_idx - 类在栈中的索引
** 返回值：
**   1 - 所有接口方法都已正确实现
**   0 - 存在未实现或参数不匹配的接口方法（会产生错误）
** 说明：
**   检查类是否实现了所有接口声明的方法，并验证参数数量
*/
int luaC_verify_interfaces(lua_State *L, int class_idx) {
  class_idx = absindex(L, class_idx);
  
  /* 如果类本身是抽象类，不需要验证接口实现 */
  lua_pushstring(L, CLASS_KEY_FLAGS);
  lua_rawget(L, class_idx);
  if (lua_isinteger(L, -1)) {
    int flags = (int)lua_tointeger(L, -1);
    if (flags & CLASS_FLAG_ABSTRACT) {
      lua_pop(L, 1);
      return 1;  /* 抽象类不需要实现所有接口方法 */
    }
  }
  lua_pop(L, 1);
  
  /* 收集所有实现的接口（包括继承链中的） */
  lua_newtable(L);  /* 存储所有接口 */
  int interfaces_collect_idx = lua_gettop(L);
  int iface_count = 0;
  
  /* 从当前类开始遍历继承链收集接口 */
  lua_pushvalue(L, class_idx);
  while (lua_istable(L, -1)) {
    int current_class = lua_gettop(L);
    
    /* 获取当前类的接口列表 */
    lua_pushstring(L, CLASS_KEY_INTERFACES);
    lua_rawget(L, current_class);
    if (lua_istable(L, -1)) {
      int ifaces_idx = lua_gettop(L);
      int n = (int)lua_rawlen(L, ifaces_idx);
      for (int i = 1; i <= n; i++) {
        lua_rawgeti(L, ifaces_idx, i);
        /* 添加到收集表（避免重复） */
        int is_dup = 0;
        for (int j = 1; j <= iface_count; j++) {
          lua_rawgeti(L, interfaces_collect_idx, j);
          if (lua_rawequal(L, -1, -2)) {
            is_dup = 1;
            lua_pop(L, 1);
            break;
          }
          lua_pop(L, 1);
        }
        if (!is_dup) {
          iface_count++;
          lua_pushvalue(L, -1);
          lua_rawseti(L, interfaces_collect_idx, iface_count);
        }
        lua_pop(L, 1);
      }
    }
    lua_pop(L, 1);  /* 移除接口列表 */
    
    /* 继续查找父类 */
    lua_pushstring(L, CLASS_KEY_PARENT);
    lua_rawget(L, current_class);
    lua_remove(L, current_class);
  }
  lua_pop(L, 1);  /* 移除非表值 */
  
  /* 遍历所有接口，验证方法实现（递归检查接口继承链） */
  for (int i = 1; i <= iface_count; i++) {
    lua_rawgeti(L, interfaces_collect_idx, i);
    int iface_idx = lua_gettop(L);
    
    /* 获取接口名（用于错误消息） */
    const char *tmp_name = get_class_name_str(L, iface_idx);
    char iface_name[256];
    if (tmp_name) {
      strncpy(iface_name, tmp_name, sizeof(iface_name));
      iface_name[sizeof(iface_name) - 1] = '\0';
    } else {
      strcpy(iface_name, "?");
    }
    
    /* 递归遍历接口及其父接口，收集所有方法并进行验证 */
    lua_pushvalue(L, iface_idx);
    while (lua_istable(L, -1)) {
      int current_iface = lua_gettop(L);
      
      /* 获取当前接口的方法表 */
      lua_pushstring(L, CLASS_KEY_METHODS);
      lua_rawget(L, current_iface);
      if (lua_istable(L, -1)) {
        int iface_methods_idx = lua_gettop(L);
        
        /* 遍历接口的所有方法 */
        lua_pushnil(L);
        while (lua_next(L, iface_methods_idx) != 0) {
          /* 栈顶: value(期望参数个数), key(方法名) */
          int expected_params = lua_isinteger(L, -1) ? (int)lua_tointeger(L, -1) : -1;
          lua_pop(L, 1);  /* 移除value，保留key用于查找 */
          
          /* 获取实现方法的参数个数 */
          int actual_params = get_method_numparams(L, class_idx, lua_gettop(L));
          
          if (actual_params < 0) {
            /* 方法未实现 */
            const char *classname = get_class_name_str(L, class_idx);
            const char *methodname = lua_tostring(L, -1);
            luaL_error(L, "class '%s' must implement method '%s' of interface '%s'",
                       classname, methodname ? methodname : "?", iface_name);
            return 0;
          }
          
          /* 验证参数数量是否匹配 */
          if (expected_params >= 0 && actual_params != expected_params && actual_params != expected_params + 1) {
            const char *classname = get_class_name_str(L, class_idx);
            const char *methodname = lua_tostring(L, -1);
            luaL_error(L, "method '%s' from interface '%s' implemented by class '%s' has mismatched parameter count: expected %d, got %d",
                       methodname ? methodname : "?", iface_name, classname,
                       expected_params, actual_params);
            return 0;
          }
        }
      }
      lua_pop(L, 1);  /* 移除接口方法表 */
      
      /* 继续查找父接口 */
      lua_pushstring(L, CLASS_KEY_PARENT);
      lua_rawget(L, current_iface);
      lua_remove(L, current_iface);
    }
    lua_pop(L, 1);  /* 移除非表值 */
    lua_pop(L, 1);  /* 移除接口 */
  }
  
  lua_pop(L, 1);  /* 移除interfaces_collect表 */
  return 1;
}


/*
** 检查方法是否可以被重写
** 参数：
**   L - Lua状态机
**   class_idx - 类在栈中的索引（父类）
**   name - 方法名
** 返回值：
**   1 - 可以重写
**   0 - 不能重写（是final方法）
*/
int luaC_can_override(lua_State *L, int class_idx, TString *name) {
  class_idx = absindex(L, class_idx);
  
  /* 检查final方法表 */
  lua_pushstring(L, CLASS_KEY_FINALS);
  lua_rawget(L, class_idx);
  if (lua_istable(L, -1)) {
    lua_pushlstring(L, getstr(name), tsslen(name));
    lua_rawget(L, -2);
    if (lua_toboolean(L, -1)) {
      lua_pop(L, 2);
      return 0;  /* 是final方法，不能重写 */
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  
  /* 递归检查父类 */
  lua_pushstring(L, CLASS_KEY_PARENT);
  lua_rawget(L, class_idx);
  if (lua_istable(L, -1)) {
    int result = luaC_can_override(L, -1, name);
    lua_pop(L, 1);
    return result;
  }
  lua_pop(L, 1);
  
  return 1;  /* 可以重写 */
}


/*
** 检查父类中是否存在指定方法（用于 override 关键字校验）
** 参数：
**   L - Lua状态机
**   class_idx - 类表索引
**   name - 方法名
** 说明：
**   递归检查继承链中是否存在该方法，如果不存在则抛出编译错误
*/
void luaC_checkoverride(lua_State *L, int class_idx, TString *name) {
  class_idx = absindex(L, class_idx);
  
  /* 获取父类 */
  lua_pushstring(L, CLASS_KEY_PARENT);
  lua_rawget(L, class_idx);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    luaL_error(L, "method '%s' declared override but class has no parent", getstr(name));
    return;
  }
  
  /* 检查父类的 METHODS 表中是否存在该方法 */
  int parent_idx = lua_gettop(L);
  lua_pushstring(L, CLASS_KEY_METHODS);
  lua_rawget(L, parent_idx);
  if (lua_istable(L, -1)) {
    lua_pushlstring(L, getstr(name), tsslen(name));
    lua_rawget(L, -2);
    if (!lua_isnil(L, -1)) {
      lua_pop(L, 3);  /* 方法存在，验证通过 */
      return;
    }
    lua_pop(L, 1);  /* 弹出 nil */
  }
  lua_pop(L, 1);  /* 弹出 methods 表 */
  
  /* 递归检查父类的父类 */
  lua_pushstring(L, CLASS_KEY_PARENT);
  lua_rawget(L, parent_idx);
  if (lua_istable(L, -1)) {
    luaC_checkoverride(L, -1, name);
    lua_pop(L, 2);  /* 弹出父类的父类和原来的父类 */
    return;
  }
  lua_pop(L, 2);  /* 弹出 nil 和父类 */
  
  luaL_error(L, "method '%s' declared override but no parent method found", getstr(name));
}


/*
** =====================================================================
** getter/setter属性访问器相关函数实现
** =====================================================================
*/

/*
** 设置getter方法
** 参数：
**   L - Lua状态机
**   class_idx - 类在栈中的索引
**   prop_name - 属性名
**   func_idx - getter函数在栈中的索引
** 说明：
**   当访问指定属性时，会调用getter函数
**   根据访问级别存储到不同的getter表中
*/
void luaC_setgetter(lua_State *L, int class_idx, TString *prop_name, int func_idx, int access_level) {
  class_idx = absindex(L, class_idx);
  func_idx = absindex(L, func_idx);
  
  /* 根据访问级别选择getter表 */
  const char *table_key;
  if (access_level == ACCESS_PRIVATE) {
    table_key = CLASS_KEY_PRIVATE_GETTERS;
  } else if (access_level == ACCESS_PROTECTED) {
    table_key = CLASS_KEY_PROTECTED_GETTERS;
  } else {
    table_key = CLASS_KEY_GETTERS;  /* 公开 */
  }
  
  /* 获取或创建getter表 */
  lua_pushstring(L, table_key);
  lua_rawget(L, class_idx);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_pushstring(L, table_key);
    lua_insert(L, -2);
    lua_rawset(L, class_idx);
  }
  
  /* 设置getter函数 */
  lua_pushlstring(L, getstr(prop_name), tsslen(prop_name));
  lua_pushvalue(L, func_idx);
  lua_rawset(L, -3);
  lua_pop(L, 1);
}


/*
** 设置setter方法
** 参数：
**   L - Lua状态机
**   class_idx - 类在栈中的索引
**   prop_name - 属性名
**   func_idx - setter函数在栈中的索引
**   access_level - 访问级别（ACCESS_PUBLIC/PROTECTED/PRIVATE）
** 说明：
**   当设置指定属性时，会调用setter函数
**   根据访问级别存储到不同的setter表中
*/
void luaC_setsetter(lua_State *L, int class_idx, TString *prop_name, int func_idx, int access_level) {
  class_idx = absindex(L, class_idx);
  func_idx = absindex(L, func_idx);
  
  /* 根据访问级别选择setter表 */
  const char *table_key;
  if (access_level == ACCESS_PRIVATE) {
    table_key = CLASS_KEY_PRIVATE_SETTERS;
  } else if (access_level == ACCESS_PROTECTED) {
    table_key = CLASS_KEY_PROTECTED_SETTERS;
  } else {
    table_key = CLASS_KEY_SETTERS;  /* 公开 */
  }
  
  /* 获取或创建setter表 */
  lua_pushstring(L, table_key);
  lua_rawget(L, class_idx);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_pushstring(L, table_key);
    lua_insert(L, -2);
    lua_rawset(L, class_idx);
  }
  
  /* 设置setter函数 */
  lua_pushlstring(L, getstr(prop_name), tsslen(prop_name));
  lua_pushvalue(L, func_idx);
  lua_rawset(L, -3);
  lua_pop(L, 1);
}


/*
** 设置成员标志
** 参数：
**   L - Lua状态机
**   class_idx - 类在栈中的索引
**   name - 成员名
**   flags - 标志位（MEMBER_*）
*/
void luaC_setmemberflags(lua_State *L, int class_idx, TString *name, int flags) {
  class_idx = absindex(L, class_idx);
  
  /* 获取或创建成员标志表 */
  lua_pushstring(L, CLASS_KEY_MEMBER_FLAGS);
  lua_rawget(L, class_idx);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_pushstring(L, CLASS_KEY_MEMBER_FLAGS);
    lua_insert(L, -2);
    lua_rawset(L, class_idx);
  }
  
  /* 设置标志 */
  lua_pushlstring(L, getstr(name), tsslen(name));
  lua_pushinteger(L, flags);
  lua_rawset(L, -3);
  lua_pop(L, 1);
}


/*
** 获取成员标志
** 参数：
**   L - Lua状态机
**   class_idx - 类在栈中的索引
**   name - 成员名
** 返回值：
**   成员标志位，不存在返回0
*/
int luaC_getmemberflags(lua_State *L, int class_idx, TString *name) {
  class_idx = absindex(L, class_idx);
  
  lua_pushstring(L, CLASS_KEY_MEMBER_FLAGS);
  lua_rawget(L, class_idx);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return 0;
  }
  
  lua_pushlstring(L, getstr(name), tsslen(name));
  lua_rawget(L, -2);
  int flags = lua_isinteger(L, -1) ? (int)lua_tointeger(L, -1) : 0;
  lua_pop(L, 2);
  return flags;
}


/*
** =====================================================================
** Trait/Mixin 系统实现
** =====================================================================
*/

/*
** 设置trait标志
** 参数：
**   L - Lua状态机
**   trait_idx - trait表在栈中的索引
** 说明：
**   将表标记为trait，trait不能实例化但可以use
*/
void luaC_settraitflag(lua_State *L, int trait_idx) {
  trait_idx = absindex(L, trait_idx);

  /* 获取当前flags */
  lua_pushstring(L, CLASS_KEY_FLAGS);
  lua_rawget(L, trait_idx);
  int flags = lua_isinteger(L, -1) ? (int)lua_tointeger(L, -1) : 0;
  lua_pop(L, 1);

  /* 设置trait标志 */
  flags |= CLASS_FLAG_TRAIT;

  lua_pushstring(L, CLASS_KEY_FLAGS);
  lua_pushinteger(L, flags);
  lua_rawset(L, trait_idx);
}

/*
** 注册trait中需要被实现的方法
** 参数：
**   L - Lua状态机
**   trait_idx - trait表在栈中的索引
**   name - 方法名
**   nparams - 期望参数个数（-1表示不验证）
** 说明：
**   require方法必须被use该trait的类实现
*/
void luaC_settraitrequire(lua_State *L, int trait_idx, TString *name, int nparams) {
  trait_idx = absindex(L, trait_idx);

  /* 获取或创建trait_requires表 */
  lua_pushstring(L, CLASS_KEY_TRAIT_REQUIRES);
  lua_rawget(L, trait_idx);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_pushstring(L, CLASS_KEY_TRAIT_REQUIRES);
    lua_insert(L, -2);
    lua_rawset(L, trait_idx);
  }

  /* 设置方法名和参数个数 */
  lua_pushlstring(L, getstr(name), tsslen(name));
  lua_pushinteger(L, nparams);
  lua_rawset(L, -3);
  lua_pop(L, 1);
}

/*
** 将trait应用到类（复制方法，跟踪require）
** 参数：
**   L - Lua状态机
**   class_idx - 类在栈中的索引
**   trait_idx - trait在栈中的索引
** 说明：
**   1. 复制trait的公开方法到类（类已有的方法不覆盖）
**   2. 收集trait的require方法到类的__trait_requires表
**   3. 记录trait到类的__traits表
*/
void luaC_usetrait(lua_State *L, int class_idx, int trait_idx) {
  class_idx = absindex(L, class_idx);
  trait_idx = absindex(L, trait_idx);

  /* 检查trait是否有效 */
  lua_pushstring(L, CLASS_KEY_FLAGS);
  lua_rawget(L, trait_idx);
  int flags = lua_isinteger(L, -1) ? (int)lua_tointeger(L, -1) : 0;
  lua_pop(L, 1);
  if (!(flags & CLASS_FLAG_TRAIT)) {
    luaL_error(L, "use target must be a trait");
    return;
  }

  /* 获取类的公开方法表（提前获取，后续代码块中复用） */
  lua_pushstring(L, CLASS_KEY_METHODS);
  lua_rawget(L, class_idx);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_pushstring(L, CLASS_KEY_METHODS);
    lua_insert(L, -2);
    lua_rawset(L, class_idx);
  }
  int class_methods = lua_gettop(L);

  /* 1. 复制trait.__methods（如果存在） */
  lua_pushstring(L, CLASS_KEY_METHODS);
  lua_rawget(L, trait_idx);
  if (lua_istable(L, -1)) {
    int trait_methods = lua_gettop(L);
    /* 复制方法（类已有的方法不覆盖） */
    lua_pushnil(L);
    while (lua_next(L, trait_methods) != 0) {
      /* 栈: key, value */
      lua_pushvalue(L, -2);  /* 复制key */
      lua_rawget(L, class_methods);
      if (lua_isnil(L, -1)) {
        /* 类中不存在此方法，复制过去 */
        lua_pop(L, 1);  /* 移除nil */
        lua_pushvalue(L, -2);  /* 复制key */
        lua_pushvalue(L, -2);  /* 复制value */
        lua_rawset(L, class_methods);
      } else {
        /* 类中已存在此方法，跳过 */
        lua_pop(L, 1);  /* 移除已有的值 */
      }
      lua_pop(L, 1);  /* 移除value，保留key供lua_next使用 */
    }
    lua_pop(L, 1);  /* 移除trait.__methods表 */
  }
  lua_pop(L, 1);  /* 移除trait.__methods 或 nil */

  /* 2. 复制trait表上的直接函数字段（兼容编译器直接将方法存储到trait表的简化实现） */
  lua_pushnil(L);
  while (lua_next(L, trait_idx) != 0) {
    /* 栈: key, value */
    if (lua_isfunction(L, -1)) {
      /* 跳过内部键（以 __ 开头的键） */
      int skip = 0;
      if (lua_isstring(L, -2)) {
        const char *k = lua_tostring(L, -2);
        if (k && k[0] == '_' && k[1] == '_') {
          skip = 1;
        }
      }
      if (!skip) {
        /* 检查类是否已有此方法 */
        lua_pushvalue(L, -2);  /* 复制key */
        lua_rawget(L, class_methods);
        if (lua_isnil(L, -1)) {
          /* 类中不存在此方法，复制过去 */
          lua_pop(L, 1);  /* 移除nil */
          lua_pushvalue(L, -2);  /* 复制key */
          lua_pushvalue(L, -2);  /* 复制value */
          lua_rawset(L, class_methods);
        } else {
          /* 类中已存在此方法，跳过 */
          lua_pop(L, 1);  /* 移除已有的值 */
        }
      }
    }
    lua_pop(L, 1);  /* 移除value，保留key供lua_next使用 */
  }

  /* 3. 收集trait的require方法到类的__trait_requires表 */
  lua_pushstring(L, CLASS_KEY_TRAIT_REQUIRES);
  lua_rawget(L, trait_idx);
  if (lua_istable(L, -1)) {
    int trait_requires = lua_gettop(L);

    /* 获取或创建类的trait_requires表 */
    lua_pushstring(L, CLASS_KEY_TRAIT_REQUIRES);
    lua_rawget(L, class_idx);
    if (!lua_istable(L, -1)) {
      lua_pop(L, 1);
      lua_newtable(L);
      lua_pushvalue(L, -1);
      lua_pushstring(L, CLASS_KEY_TRAIT_REQUIRES);
      lua_insert(L, -2);
      lua_rawset(L, class_idx);
    }
    int class_requires = lua_gettop(L);

    /* 复制所有require方法 */
    lua_pushnil(L);
    while (lua_next(L, trait_requires) != 0) {
      lua_pushvalue(L, -2);  /* key */
      lua_pushvalue(L, -2);  /* value */
      lua_rawset(L, class_requires);
      lua_pop(L, 1);  /* 移除value */
    }

    lua_pop(L, 1);  /* 移除class_requires */
  }
  lua_pop(L, 1);  /* 移除trait_requires */

  /* 3. 记录trait到类的__traits表 */
  lua_pushstring(L, CLASS_KEY_TRAITS);
  lua_rawget(L, class_idx);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_pushstring(L, CLASS_KEY_TRAITS);
    lua_insert(L, -2);
    lua_rawset(L, class_idx);
  }

  /* 添加trait到列表 */
  int n = (int)lua_rawlen(L, -1);
  lua_pushvalue(L, trait_idx);
  lua_rawseti(L, -2, n + 1);
  lua_pop(L, 1);
}


/*
** 验证所有trait require方法是否都被类实现
** 参数：
**   L - Lua状态机
**   class_idx - 类在栈中的索引
** 返回值：
**   1 - 所有require方法都已实现
**   0 - 存在未实现的方法（会产生错误）
** 说明：
**   遍历类的__trait_requires表，验证每个方法是否在类中实现
*/
int luaC_verify_trait_requires(lua_State *L, int class_idx) {
  class_idx = absindex(L, class_idx);

  /* 如果类本身是抽象类，不需要验证 */
  lua_pushstring(L, CLASS_KEY_FLAGS);
  lua_rawget(L, class_idx);
  if (lua_isinteger(L, -1)) {
    int flags = (int)lua_tointeger(L, -1);
    if (flags & CLASS_FLAG_ABSTRACT) {
      lua_pop(L, 1);
      return 1;
    }
  }
  lua_pop(L, 1);

  /* 获取trait_requires表 */
  lua_pushstring(L, CLASS_KEY_TRAIT_REQUIRES);
  lua_rawget(L, class_idx);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return 1;  /* 没有require方法需要验证 */
  }
  int requires_idx = lua_gettop(L);

  /* 遍历所有require方法 */
  lua_pushnil(L);
  while (lua_next(L, requires_idx) != 0) {
    /* 栈顶: value(期望参数个数), key(方法名) */
    int expected_params = lua_isinteger(L, -1) ? (int)lua_tointeger(L, -1) : -1;
    lua_pop(L, 1);  /* 移除value，保留key用于查找 */

    /* 获取实现方法的参数个数 */
    int actual_params = get_method_numparams(L, class_idx, lua_gettop(L));

    if (actual_params < 0) {
      /* 方法未实现 */
      const char *classname = get_class_name_str(L, class_idx);
      const char *methodname = lua_tostring(L, -1);
      luaL_error(L, "class '%s' must implement required method '%s' from trait",
                 classname, methodname ? methodname : "?");
      return 0;
    }

    /* 验证参数数量是否匹配 */
    if (expected_params >= 0 && actual_params != expected_params && actual_params != expected_params + 1) {
      const char *classname = get_class_name_str(L, class_idx);
      const char *methodname = lua_tostring(L, -1);
      luaL_error(L, "method '%s' of class '%s' does not match trait requirement: expected %d parameters, got %d",
                 methodname ? methodname : "?", classname,
                 expected_params, actual_params);
      return 0;
    }
  }

  lua_pop(L, 1);  /* 移除requires表 */
  return 1;
}
