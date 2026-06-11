/*
** logtable.c
** Table access logging module - 用户友好的表访问日志库
** 提供表访问拦截和过滤功能，支持丰富的过滤选项和预设配置
**
** 主要功能:
**   - 启用/禁用表访问日志记录
**   - 按键名、值、操作类型过滤日志
**   - 按类型过滤（string/number/boolean/table/function/userdata/thread）
**   - 去重和唯一值显示
**   - 智能模式（自动过滤常见内部访问）
**   - JNI环境和userdata过滤
**   - 预设配置快速切换
**   - 批量配置一次性设置
**   - 状态查询
*/

#define logtable_c
#define LUA_LIB

#include "lprefix.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "ltable.h"

#if defined(__ANDROID__) && defined(ANDROID_NDK)
#include <android/log.h>
#define LOG_TAG "lua"
#define LOGD(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#else
#define LOGD(...) ((void)0)
#endif

/* ==============================================
 * 基础功能函数
 * ============================================== */

/**
 * 启用或禁用表访问日志记录
 * @param enable 布尔值，true启用，false禁用
 * @return 返回操作前的状态（true/false）
 */
static int logtable_onlog(lua_State *L) {
  int enable = lua_toboolean(L, 1);
  int result = luaH_enable_access_log(L, enable);
  lua_pushboolean(L, result);
  return 1;
}

/**
 * 获取当前日志文件路径
 * @return 日志文件路径字符串，未设置返回nil
 */
static int logtable_getlogpath(lua_State *L) {
  const char *path = luaH_get_log_path(L);
  if (path && path[0] != '\0') {
    lua_pushstring(L, path);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

/**
 * 设置日志文件路径
 * 必须在启用日志之前调用
 * @param path 日志文件路径字符串
 * @return 成功返回true
 */
static int logtable_setlogpath(lua_State *L) {
  /* 当前实现中路径由内部自动生成，此函数作为预留接口 */
  /* 用户可以通过设置 LOGTABLE_PATH 环境变量来影响路径 */
  (void)L;
  lua_pushboolean(L, 0);
  lua_pushstring(L, "logtable.setlogpath: 当前版本不支持动态设置日志路径，请使用环境变量或修改源码");
  return 2;
}

/* ==============================================
 * 过滤器开关函数
 * ============================================== */

/**
 * 启用/禁用过滤器总开关
 * @param enabled 布尔值，true启用过滤器，false禁用（记录所有访问）
 */
static int logtable_setfilter(lua_State *L) {
  int enabled = lua_toboolean(L, 1);
  luaH_set_access_filter_enabled(enabled);
  return 0;
}

/**
 * 清除所有过滤器设置（恢复默认：记录所有访问）
 */
static int logtable_clearfilter(lua_State *L) {
  (void)L;
  luaH_clear_access_filters();
  return 0;
}

/* ==============================================
 * 键名过滤器
 * ============================================== */

/**
 * 添加键名包含过滤器（白名单）
 * 只有匹配指定模式的键名会被记录
 * @param pattern 匹配模式字符串
 * @return 当前包含过滤器数量
 */
static int logtable_addinkey(lua_State *L) {
  const char *pattern = luaL_checkstring(L, 1);
  int count = luaH_add_include_key_filter(pattern);
  lua_pushinteger(L, count);
  return 1;
}

/**
 * 添加键名排除过滤器（黑名单）
 * 匹配指定模式的键名不会被记录
 * @param pattern 匹配模式字符串
 * @return 当前排除过滤器数量
 */
static int logtable_exckey(lua_State *L) {
  const char *pattern = luaL_checkstring(L, 1);
  int count = luaH_add_exclude_key_filter(pattern);
  lua_pushinteger(L, count);
  return 1;
}

/* ==============================================
 * 值过滤器
 * ============================================== */

/**
 * 添加值包含过滤器（白名单）
 * 只有匹配指定模式的值会被记录
 * @param pattern 匹配模式字符串
 * @return 当前包含过滤器数量
 */
static int logtable_addinval(lua_State *L) {
  const char *pattern = luaL_checkstring(L, 1);
  int count = luaH_add_include_value_filter(pattern);
  lua_pushinteger(L, count);
  return 1;
}

/**
 * 添加值排除过滤器（黑名单）
 * 匹配指定模式的值不会被记录
 * @param pattern 匹配模式字符串
 * @return 当前排除过滤器数量
 */
static int logtable_exczval(lua_State *L) {
  const char *pattern = luaL_checkstring(L, 1);
  int count = luaH_add_exclude_value_filter(pattern);
  lua_pushinteger(L, count);
  return 1;
}

/* ==============================================
 * 操作类型过滤器
 * ============================================== */

/**
 * 添加操作类型包含过滤器（白名单）
 * 操作类型示例: "get", "set", "index", "newindex"
 * @param pattern 操作类型匹配模式
 * @return 当前包含过滤器数量
 */
static int logtable_addinop(lua_State *L) {
  const char *pattern = luaL_checkstring(L, 1);
  int count = luaH_add_include_op_filter(pattern);
  lua_pushinteger(L, count);
  return 1;
}

/**
 * 添加操作类型排除过滤器（黑名单）
 * 排除指定操作类型不被记录
 * @param pattern 操作类型匹配模式
 * @return 当前排除过滤器数量
 */
static int logtable_exczop(lua_State *L) {
  const char *pattern = luaL_checkstring(L, 1);
  int count = luaH_add_exclude_op_filter(pattern);
  lua_pushinteger(L, count);
  return 1;
}

/* ==============================================
 * 整数范围过滤器
 * ============================================== */

/**
 * 设置键名整数范围过滤器
 * 只记录键名在指定整数范围内的访问
 * @param min_val 最小值（闭区间）
 * @param max_val 最大值（闭区间）
 */
static int logtable_keyrange(lua_State *L) {
  lua_Integer min_val = luaL_optinteger(L, 1, 0);
  lua_Integer max_val = luaL_optinteger(L, 2, 0);
  luaH_set_key_int_range((int)min_val, (int)max_val);
  return 0;
}

/**
 * 设置值整数范围过滤器
 * 只记录值在指定整数范围内的访问
 * @param min_val 最小值（闭区间）
 * @param max_val 最大值（闭区间）
 */
static int logtable_valrange(lua_State *L) {
  lua_Integer min_val = luaL_optinteger(L, 1, 0);
  lua_Integer max_val = luaL_optinteger(L, 2, 0);
  luaH_set_value_int_range((int)min_val, (int)max_val);
  return 0;
}

/* ==============================================
 * 去重功能
 * ============================================== */

/**
 * 启用/禁用去重功能
 * 启用后，相同的访问记录只记录一次
 * @param enabled 布尔值
 */
static int logtable_setdedup(lua_State *L) {
  int enabled = lua_toboolean(L, 1);
  luaH_set_dedup_enabled(enabled);
  return 0;
}

/**
 * 设置只显示唯一值模式
 * 只记录首次出现的访问，忽略后续相同访问
 * @param enabled 布尔值
 */
static int logtable_setunique(lua_State *L) {
  int enabled = lua_toboolean(L, 1);
  luaH_set_show_unique_only(enabled);
  return 0;
}

/**
 * 重置去重缓存
 * 清除所有已记录的访问缓存，重新开始记录
 */
static int logtable_resetdedup(lua_State *L) {
  (void)L;
  luaH_reset_dedup_cache();
  return 0;
}

/* ==============================================
 * 类型过滤器
 * ============================================== */

/**
 * 添加键名类型包含过滤器（白名单）
 * 类型名称: "string", "number", "boolean", "table", "function", "userdata", "thread"
 * @param type 类型名称字符串
 * @return 当前包含过滤器数量
 */
static int logtable_addinkeytype(lua_State *L) {
  const char *type = luaL_checkstring(L, 1);
  int count = luaH_add_include_key_type_filter(type);
  lua_pushinteger(L, count);
  return 1;
}

/**
 * 添加键名类型排除过滤器（黑名单）
 * @param type 类型名称字符串
 * @return 当前排除过滤器数量
 */
static int logtable_exckeytype(lua_State *L) {
  const char *type = luaL_checkstring(L, 1);
  int count = luaH_add_exclude_key_type_filter(type);
  lua_pushinteger(L, count);
  return 1;
}

/**
 * 添加值类型包含过滤器（白名单）
 * @param type 类型名称字符串
 * @return 当前包含过滤器数量
 */
static int logtable_addinvaltype(lua_State *L) {
  const char *type = luaL_checkstring(L, 1);
  int count = luaH_add_include_value_type_filter(type);
  lua_pushinteger(L, count);
  return 1;
}

/**
 * 添加值类型排除过滤器（黑名单）
 * @param type 类型名称字符串
 * @return 当前排除过滤器数量
 */
static int logtable_exczvaltype(lua_State *L) {
  const char *type = luaL_checkstring(L, 1);
  int count = luaH_add_exclude_value_type_filter(type);
  lua_pushinteger(L, count);
  return 1;
}

/* ==============================================
 * 智能模式
 * ============================================== */

/**
 * 启用/禁用智能模式
 * 智能模式下会自动过滤常见的内部访问、元方法调用等
 * @param enabled 布尔值
 */
static int logtable_setintelligent(lua_State *L) {
  int enabled = lua_toboolean(L, 1);
  luaH_set_intelligent_mode(enabled);
  return 0;
}

/**
 * 查询智能模式是否启用
 * @return 布尔值
 */
static int logtable_getintelligent(lua_State *L) {
  int enabled = luaH_is_intelligent_mode_enabled();
  lua_pushboolean(L, enabled);
  return 1;
}

/* ==============================================
 * JNI环境过滤
 * ============================================== */

/**
 * 启用/禁用JNI环境过滤
 * 启用后过滤JNI调用相关的表访问
 * @param enabled 布尔值
 */
static int logtable_setjnienv(lua_State *L) {
  int enabled = lua_toboolean(L, 1);
  luaH_set_filter_jnienv(enabled);
  return 0;
}

/**
 * 查询JNI环境过滤是否启用
 * @return 布尔值
 */
static int logtable_getjnienv(lua_State *L) {
  int enabled = luaH_is_filter_jnienv_enabled();
  lua_pushboolean(L, enabled);
  return 1;
}

/* ==============================================
 * Userdata过滤
 * ============================================== */

/**
 * 启用/禁用userdata过滤
 * 启用后过滤userdata类型的表访问
 * @param enabled 布尔值
 */
static int logtable_setuserdata(lua_State *L) {
  int enabled = lua_toboolean(L, 1);
  luaH_set_filter_userdata(enabled);
  return 0;
}

/**
 * 查询userdata过滤是否启用
 * @return 布尔值
 */
static int logtable_getuserdata(lua_State *L) {
  int enabled = luaH_is_filter_userdata_enabled();
  lua_pushboolean(L, enabled);
  return 1;
}

/* ==============================================
 * 高级功能：预设配置
 * ============================================== */

/**
 * 应用预设配置
 * 可用的预设:
 *   "all"      - 记录所有表访问
 *   "minimal"  - 最少记录（仅记录显式读写）
 *   "debug"    - 调试模式（启用智能模式+去重）
 *   "security" - 安全分析模式（过滤JNI和userdata）
 *   "keys"     - 只关注键名（记录所有键名，不过滤值）
 *   "values"   - 只关注值（记录所有值，不过滤键名）
 *   "changes"  - 只记录修改操作（set/newindex）
 *   "reads"    - 只记录读取操作（get/index）
 * @param name 预设名称字符串
 * @return 成功返回true
 */
static int logtable_preset(lua_State *L) {
  const char *name = luaL_checkstring(L, 1);

  /* 先清除所有过滤器 */
  luaH_clear_access_filters();

  if (strcmp(name, "all") == 0) {
    /* 记录所有：不做任何过滤 */
    luaH_set_access_filter_enabled(0);
    luaH_set_dedup_enabled(0);
    luaH_set_intelligent_mode(0);
  }
  else if (strcmp(name, "minimal") == 0) {
    /* 最少记录：只记录显式读写 */
    luaH_set_access_filter_enabled(1);
    luaH_set_intelligent_mode(1);
    luaH_set_dedup_enabled(1);
    luaH_set_show_unique_only(1);
    luaH_set_filter_jnienv(1);
    luaH_set_filter_userdata(1);
  }
  else if (strcmp(name, "debug") == 0) {
    /* 调试模式：启用智能模式+去重 */
    luaH_set_access_filter_enabled(1);
    luaH_set_intelligent_mode(1);
    luaH_set_dedup_enabled(1);
    luaH_set_show_unique_only(0);
    luaH_set_filter_jnienv(0);
    luaH_set_filter_userdata(0);
  }
  else if (strcmp(name, "security") == 0) {
    /* 安全分析：过滤JNI和userdata */
    luaH_set_access_filter_enabled(1);
    luaH_set_intelligent_mode(1);
    luaH_set_dedup_enabled(0);
    luaH_set_filter_jnienv(1);
    luaH_set_filter_userdata(1);
  }
  else if (strcmp(name, "keys") == 0) {
    /* 只关注键名 */
    luaH_set_access_filter_enabled(1);
    luaH_set_intelligent_mode(1);
    luaH_set_dedup_enabled(1);
    luaH_set_show_unique_only(1);
  }
  else if (strcmp(name, "values") == 0) {
    /* 只关注值 */
    luaH_set_access_filter_enabled(1);
    luaH_set_intelligent_mode(1);
    luaH_set_dedup_enabled(1);
    luaH_set_show_unique_only(1);
  }
  else if (strcmp(name, "changes") == 0) {
    /* 只记录修改操作 */
    luaH_set_access_filter_enabled(1);
    luaH_add_include_op_filter("set");
    luaH_add_include_op_filter("newindex");
    luaH_set_dedup_enabled(0);
    luaH_set_intelligent_mode(1);
  }
  else if (strcmp(name, "reads") == 0) {
    /* 只记录读取操作 */
    luaH_set_access_filter_enabled(1);
    luaH_add_include_op_filter("get");
    luaH_add_include_op_filter("index");
    luaH_set_dedup_enabled(0);
    luaH_set_intelligent_mode(1);
  }
  else {
    lua_pushboolean(L, 0);
    lua_pushfstring(L, "未知的预设名称: '%s'。可用预设: all, minimal, debug, security, keys, values, changes, reads", name);
    return 2;
  }

  lua_pushboolean(L, 1);
  return 1;
}

/**
 * 获取可用预设列表
 * @return 包含所有预设名称的table
 */
static int logtable_listpresets(lua_State *L) {
  lua_newtable(L);
  const char *presets[] = {
    "all", "minimal", "debug", "security",
    "keys", "values", "changes", "reads", NULL
  };
  int i = 1;
  for (const char **p = presets; *p != NULL; p++) {
    lua_pushstring(L, *p);
    lua_rawseti(L, -2, i++);
  }
  return 1;
}

/* ==============================================
 * 高级功能：批量配置
 * ============================================== */

/**
 * 批量应用配置
 * 通过一个table一次性配置所有过滤器选项
 * 配置表格式:
 * {
 *   filter = true/false,           -- 过滤器总开关
 *   intelligent = true/false,      -- 智能模式
 *   dedup = true/false,            -- 去重
 *   unique = true/false,           -- 唯一值模式
 *   jnienv = true/false,           -- JNI环境过滤
 *   userdata = true/false,         -- userdata过滤
 *   include_keys = {"pattern1", ...},  -- 键名包含模式
 *   exclude_keys = {"pattern1", ...},  -- 键名排除模式
 *   include_vals = {"pattern1", ...},  -- 值包含模式
 *   exclude_vals = {"pattern1", ...},  -- 值排除模式
 *   include_ops = {"get", "set", ...}, -- 操作包含模式
 *   exclude_ops = {"get", ...},        -- 操作排除模式
 *   key_range = {min, max},            -- 键名整数范围
 *   val_range = {min, max},            -- 值整数范围
 *   include_key_types = {"string", "number", ...},  -- 键名类型包含
 *   exclude_key_types = {"string", ...},            -- 键名类型排除
 *   include_val_types = {"string", ...},            -- 值类型包含
 *   exclude_val_types = {"string", ...},            -- 值类型排除
 * }
 * @param config 配置表
 * @return 成功返回true
 */
static int logtable_batch(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);

  /* 清除现有过滤器 */
  luaH_clear_access_filters();

  /* 过滤器总开关 */
  lua_getfield(L, 1, "filter");
  if (!lua_isnil(L, -1)) {
    luaH_set_access_filter_enabled(lua_toboolean(L, -1));
  }
  lua_pop(L, 1);

  /* 智能模式 */
  lua_getfield(L, 1, "intelligent");
  if (!lua_isnil(L, -1)) {
    luaH_set_intelligent_mode(lua_toboolean(L, -1));
  }
  lua_pop(L, 1);

  /* 去重 */
  lua_getfield(L, 1, "dedup");
  if (!lua_isnil(L, -1)) {
    luaH_set_dedup_enabled(lua_toboolean(L, -1));
  }
  lua_pop(L, 1);

  /* 唯一值模式 */
  lua_getfield(L, 1, "unique");
  if (!lua_isnil(L, -1)) {
    luaH_set_show_unique_only(lua_toboolean(L, -1));
  }
  lua_pop(L, 1);

  /* JNI环境过滤 */
  lua_getfield(L, 1, "jnienv");
  if (!lua_isnil(L, -1)) {
    luaH_set_filter_jnienv(lua_toboolean(L, -1));
  }
  lua_pop(L, 1);

  /* userdata过滤 */
  lua_getfield(L, 1, "userdata");
  if (!lua_isnil(L, -1)) {
    luaH_set_filter_userdata(lua_toboolean(L, -1));
  }
  lua_pop(L, 1);

  /* 辅助函数：处理字符串数组过滤器 */
  /* include_keys */
  lua_getfield(L, 1, "include_keys");
  if (lua_istable(L, -1)) {
    lua_Integer len = luaL_len(L, -1);
    for (lua_Integer i = 1; i <= len; i++) {
      lua_geti(L, -1, i);
      if (lua_isstring(L, -1)) {
        luaH_add_include_key_filter(lua_tostring(L, -1));
      }
      lua_pop(L, 1);
    }
  }
  lua_pop(L, 1);

  /* exclude_keys */
  lua_getfield(L, 1, "exclude_keys");
  if (lua_istable(L, -1)) {
    lua_Integer len = luaL_len(L, -1);
    for (lua_Integer i = 1; i <= len; i++) {
      lua_geti(L, -1, i);
      if (lua_isstring(L, -1)) {
        luaH_add_exclude_key_filter(lua_tostring(L, -1));
      }
      lua_pop(L, 1);
    }
  }
  lua_pop(L, 1);

  /* include_vals */
  lua_getfield(L, 1, "include_vals");
  if (lua_istable(L, -1)) {
    lua_Integer len = luaL_len(L, -1);
    for (lua_Integer i = 1; i <= len; i++) {
      lua_geti(L, -1, i);
      if (lua_isstring(L, -1)) {
        luaH_add_include_value_filter(lua_tostring(L, -1));
      }
      lua_pop(L, 1);
    }
  }
  lua_pop(L, 1);

  /* exclude_vals */
  lua_getfield(L, 1, "exclude_vals");
  if (lua_istable(L, -1)) {
    lua_Integer len = luaL_len(L, -1);
    for (lua_Integer i = 1; i <= len; i++) {
      lua_geti(L, -1, i);
      if (lua_isstring(L, -1)) {
        luaH_add_exclude_value_filter(lua_tostring(L, -1));
      }
      lua_pop(L, 1);
    }
  }
  lua_pop(L, 1);

  /* include_ops */
  lua_getfield(L, 1, "include_ops");
  if (lua_istable(L, -1)) {
    lua_Integer len = luaL_len(L, -1);
    for (lua_Integer i = 1; i <= len; i++) {
      lua_geti(L, -1, i);
      if (lua_isstring(L, -1)) {
        luaH_add_include_op_filter(lua_tostring(L, -1));
      }
      lua_pop(L, 1);
    }
  }
  lua_pop(L, 1);

  /* exclude_ops */
  lua_getfield(L, 1, "exclude_ops");
  if (lua_istable(L, -1)) {
    lua_Integer len = luaL_len(L, -1);
    for (lua_Integer i = 1; i <= len; i++) {
      lua_geti(L, -1, i);
      if (lua_isstring(L, -1)) {
        luaH_add_exclude_op_filter(lua_tostring(L, -1));
      }
      lua_pop(L, 1);
    }
  }
  lua_pop(L, 1);

  /* key_range: {min, max} */
  lua_getfield(L, 1, "key_range");
  if (lua_istable(L, -1)) {
    lua_geti(L, -1, 1);
    lua_geti(L, -2, 2);
    if (lua_isinteger(L, -2) && lua_isinteger(L, -1)) {
      luaH_set_key_int_range((int)lua_tointeger(L, -2), (int)lua_tointeger(L, -1));
    }
    lua_pop(L, 2);
  }
  lua_pop(L, 1);

  /* val_range: {min, max} */
  lua_getfield(L, 1, "val_range");
  if (lua_istable(L, -1)) {
    lua_geti(L, -1, 1);
    lua_geti(L, -2, 2);
    if (lua_isinteger(L, -2) && lua_isinteger(L, -1)) {
      luaH_set_value_int_range((int)lua_tointeger(L, -2), (int)lua_tointeger(L, -1));
    }
    lua_pop(L, 2);
  }
  lua_pop(L, 1);

  /* 类型过滤器辅助函数 */
  #define BATCH_TYPE_FILTER(field, add_func) \
    lua_getfield(L, 1, field); \
    if (lua_istable(L, -1)) { \
      lua_Integer len = luaL_len(L, -1); \
      for (lua_Integer i = 1; i <= len; i++) { \
        lua_geti(L, -1, i); \
        if (lua_isstring(L, -1)) { \
          add_func(lua_tostring(L, -1)); \
        } \
        lua_pop(L, 1); \
      } \
    } \
    lua_pop(L, 1);

  BATCH_TYPE_FILTER("include_key_types", luaH_add_include_key_type_filter);
  BATCH_TYPE_FILTER("exclude_key_types", luaH_add_exclude_key_type_filter);
  BATCH_TYPE_FILTER("include_val_types", luaH_add_include_value_type_filter);
  BATCH_TYPE_FILTER("exclude_val_types", luaH_add_exclude_value_type_filter);

  #undef BATCH_TYPE_FILTER

  lua_pushboolean(L, 1);
  return 1;
}

/* ==============================================
 * 高级功能：状态查询
 * ============================================== */

/**
 * 查询当前所有过滤器状态
 * 返回一个包含所有设置信息的table
 * @return 状态信息表
 */
static int logtable_status(lua_State *L) {
  lua_newtable(L);

  /* 日志状态 */
  lua_pushboolean(L, luaH_is_intelligent_mode_enabled()); /* 用这个检查是否启用 */
  lua_setfield(L, -2, "enabled");

  /* 日志路径 */
  const char *path = luaH_get_log_path(L);
  if (path && path[0] != '\0') {
    lua_pushstring(L, path);
  } else {
    lua_pushnil(L);
  }
  lua_setfield(L, -2, "log_path");

  /* 智能模式 */
  lua_pushboolean(L, luaH_is_intelligent_mode_enabled());
  lua_setfield(L, -2, "intelligent");

  /* JNI环境过滤 */
  lua_pushboolean(L, luaH_is_filter_jnienv_enabled());
  lua_setfield(L, -2, "filter_jnienv");

  /* userdata过滤 */
  lua_pushboolean(L, luaH_is_filter_userdata_enabled());
  lua_setfield(L, -2, "filter_userdata");

  return 1;
}

/* ==============================================
 * 高级功能：帮助信息
 * ============================================== */

/**
 * 获取帮助信息
 * 返回logtable库的完整使用说明
 * @return 帮助文本字符串
 */
static int logtable_help(lua_State *L) {
  const char *help_text =
    "=== logtable 库使用帮助 ===\n"
    "\n"
    "logtable 是一个表访问日志记录库，用于拦截和记录Lua表的读写操作。\n"
    "\n"
    "【快速开始】\n"
    "  -- 使用预设配置（推荐）\n"
    "  logtable.preset('debug')    -- 调试模式\n"
    "  logtable.onlog(true)          -- 启用日志\n"
    "\n"
    "  -- 手动配置\n"
    "  logtable.setfilter(true)      -- 启用过滤器\n"
    "  logtable.setintelligent(true) -- 启用智能模式\n"
    "  logtable.setdedup(true)       -- 启用去重\n"
    "  logtable.onlog(true)          -- 启用日志\n"
    "\n"
    "【预设配置】\n"
    "  logtable.preset('all')       -- 记录所有表访问\n"
    "  logtable.preset('minimal')   -- 最少记录\n"
    "  logtable.preset('debug')     -- 调试模式\n"
    "  logtable.preset('security')  -- 安全分析模式\n"
    "  logtable.preset('keys')      -- 只关注键名\n"
    "  logtable.preset('values')    -- 只关注值\n"
    "  logtable.preset('changes')   -- 只记录修改操作\n"
    "  logtable.preset('reads')     -- 只记录读取操作\n"
    "\n"
    "【批量配置】\n"
    "  logtable.batch({\n"
    "    intelligent = true,\n"
    "    dedup = true,\n"
    "    exclude_keys = {'_', '__'},\n"
    "    include_ops = {'get', 'set'}\n"
    "  })\n"
    "\n"
    "【过滤器函数】\n"
    "  -- 键名过滤\n"
    "  logtable.addinkey('pattern')   -- 添加键名包含模式\n"
    "  logtable.exckey('pattern')     -- 添加键名排除模式\n"
    "  logtable.addinkeytype('type')  -- 添加键名类型包含\n"
    "  logtable.exckeytype('type')    -- 添加键名类型排除\n"
    "  logtable.keyrange(min, max)    -- 设置键名整数范围\n"
    "\n"
    "  -- 值过滤\n"
    "  logtable.addinval('pattern')   -- 添加值包含模式\n"
    "  logtable.exczval('pattern')    -- 添加值排除模式\n"
    "  logtable.addinvaltype('type')  -- 添加值类型包含\n"
    "  logtable.exczvaltype('type')   -- 添加值类型排除\n"
    "  logtable.valrange(min, max)    -- 设置值整数范围\n"
    "\n"
    "  -- 操作过滤\n"
    "  logtable.addinop('get')        -- 添加操作包含模式\n"
    "  logtable.exczop('set')         -- 添加操作排除模式\n"
    "\n"
    "【查询函数】\n"
    "  logtable.status()              -- 查询当前状态\n"
    "  logtable.getlogpath()          -- 获取日志路径\n"
    "  logtable.getintelligent()      -- 查询智能模式状态\n"
    "  logtable.getjnienv()           -- 查询JNI过滤状态\n"
    "  logtable.getuserdata()         -- 查询userdata过滤状态\n"
    "  logtable.listpresets()         -- 列出可用预设\n"
    "\n"
    "  -- 使用 logtable.help() 查看此帮助信息\n";
  lua_pushstring(L, help_text);
  return 1;
}

/* ==============================================
 * 函数注册表
 * ============================================== */

static const luaL_Reg logtable_funcs[] = {
  /* 基础功能 */
  {"onlog", logtable_onlog},
  {"getlogpath", logtable_getlogpath},
  {"setlogpath", logtable_setlogpath},

  /* 过滤器开关 */
  {"setfilter", logtable_setfilter},
  {"clearfilter", logtable_clearfilter},

  /* 键名过滤 */
  {"addinkey", logtable_addinkey},
  {"exckey", logtable_exckey},

  /* 值过滤 */
  {"addinval", logtable_addinval},
  {"exczval", logtable_exczval},

  /* 操作过滤 */
  {"addinop", logtable_addinop},
  {"exczop", logtable_exczop},

  /* 范围过滤 */
  {"keyrange", logtable_keyrange},
  {"valrange", logtable_valrange},

  /* 去重功能 */
  {"setdedup", logtable_setdedup},
  {"setunique", logtable_setunique},
  {"resetdedup", logtable_resetdedup},

  /* 类型过滤 */
  {"addinkeytype", logtable_addinkeytype},
  {"exckeytype", logtable_exckeytype},
  {"addinvaltype", logtable_addinvaltype},
  {"exczvaltype", logtable_exczvaltype},

  /* 智能模式 */
  {"setintelligent", logtable_setintelligent},
  {"getintelligent", logtable_getintelligent},

  /* JNI环境 */
  {"setjnienv", logtable_setjnienv},
  {"getjnienv", logtable_getjnienv},

  /* userdata */
  {"setuserdata", logtable_setuserdata},
  {"getuserdata", logtable_getuserdata},

  /* 高级功能 */
  {"preset", logtable_preset},
  {"listpresets", logtable_listpresets},
  {"batch", logtable_batch},
  {"status", logtable_status},
  {"help", logtable_help},

  /* 友好的别名 */
  {"enable", logtable_onlog},        /* onlog 的别名 */
  {"disable", logtable_onlog},       /* 与 enable 相同，传 false */
  {"getpath", logtable_getlogpath},  /* getlogpath 的别名 */
  {"setpath", logtable_setlogpath},  /* setlogpath 的别名 */
  {"info", logtable_status},         /* status 的别名 */

  {NULL, NULL}
};

LUAMOD_API int luaopen_logtable(lua_State *L) {
  luaL_newlib(L, logtable_funcs);
  return 1;
}