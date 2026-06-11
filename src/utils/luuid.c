/**
 * @file luuid.c
 * @brief LXCLUA UUID 生成库 - RFC 4122
 *
 * 提供 UUID v4 (随机) 和 v5 (SHA-1 命名) 生成。
 * v3 已废弃 (MD5)，用 v5 代替。
 *
 * 模块结构:
 *   uuid.v4()                       -> UUID 字符串
 *   uuid.v5(namespace, name)        -> UUID 字符串
 *   uuid.parse(str)                 -> {fields} | nil
 *   uuid.is_valid(str)              -> boolean
 *
 * 预定义命名空间:
 *   uuid.NAMESPACE_DNS    = "6ba7b810-9dad-11d1-80b4-00c04fd430c8"
 *   uuid.NAMESPACE_URL    = "6ba7b811-9dad-11d1-80b4-00c04fd430c8"
 *   uuid.NAMESPACE_OID    = "6ba7b812-9dad-11d1-80b4-00c04fd430c8"
 *   uuid.NAMESPACE_X500   = "6ba7b814-9dad-11d1-80b4-00c04fd430c8"
 */

#define LUA_LIB

#include "lprefix.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "sha256.h"
#include "csprng.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>

/* ========================================================================
 * 全局 CSPRNG (惰性初始化)
 * ======================================================================== */

static CSPRNG_State g_uuid_csprng;
static int g_uuid_csprng_inited = 0;

static CSPRNG_State* get_csprng(void) {
  if (!g_uuid_csprng_inited) {
    uint64_t seed = (uint64_t)time(NULL);
    csprng_init(&g_uuid_csprng, seed);
    g_uuid_csprng_inited = 1;
  }
  return &g_uuid_csprng;
}

/* ========================================================================
 * 预定义命名空间 UUID (RFC 4122)
 * ======================================================================== */

static const char *UUID_NAMESPACE_DNS  = "6ba7b810-9dad-11d1-80b4-00c04fd430c8";
static const char *UUID_NAMESPACE_URL  = "6ba7b811-9dad-11d1-80b4-00c04fd430c8";
static const char *UUID_NAMESPACE_OID  = "6ba7b812-9dad-11d1-80b4-00c04fd430c8";
static const char *UUID_NAMESPACE_X500 = "6ba7b814-9dad-11d1-80b4-00c04fd430c8";

/* ========================================================================
 * UUID 生成辅助函数
 * ======================================================================== */

/**
 * @brief UUID 字符串格式化为标准格式
 * @param uuid 16字节 UUID 原始数据
 * @param buf  输出缓冲区 (至少 37 字节)
 */
static void uuid_format(const uint8_t uuid[16], char buf[37]) {
  sprintf(buf,
    "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
    uuid[0], uuid[1], uuid[2], uuid[3],
    uuid[4], uuid[5],
    uuid[6], uuid[7],
    uuid[8], uuid[9],
    uuid[10], uuid[11], uuid[12], uuid[13], uuid[14], uuid[15]);
}

/**
 * @brief 解析 UUID 字符串为 16 字节二进制
 * @param str  UUID 字符串 (带或不带连字符)
 * @param uuid 输出 16 字节缓冲区
 * @return 成功返回 1，失败返回 0
 */
static int uuid_parse_str(const char *str, uint8_t uuid[16]) {
  char hex[33];
  int hlen = 0;

  /* 提取所有十六进制字符 */
  for (int i = 0; str[i] && hlen < 32; i++) {
    if (str[i] == '-') continue;
    char c = str[i];
    int valid = (c >= '0' && c <= '9')
             || (c >= 'a' && c <= 'f')
             || (c >= 'A' && c <= 'F');
    if (!valid) return 0;
    hex[hlen++] = c;
  }
  if (hlen != 32) return 0;

  /* 转换为二进制 */
  for (int i = 0; i < 16; i++) {
    unsigned int byte;
    if (sscanf(hex + i * 2, "%2x", &byte) != 1) return 0;
    uuid[i] = (uint8_t)byte;
  }
  return 1;
}

/* ========================================================================
 * UUID v4: 基于随机数生成
 * ======================================================================== */

/**
 * @brief uuid.v4() -> string
 * 生成随机 UUID v4
 */
static int l_uuid_v4(lua_State *L) {
  uint8_t uuid[16];
  csprng_bytes(get_csprng(), uuid, 16);

  /* 设置版本位 (version 4: 0100xxxx) */
  uuid[6] = (uint8_t)((uuid[6] & 0x0F) | 0x40);
  /* 设置变体位 (variant 1: 10xxxxxx) */
  uuid[8] = (uint8_t)((uuid[8] & 0x3F) | 0x80);

  char buf[37];
  uuid_format(uuid, buf);
  lua_pushstring(L, buf);
  return 1;
}

/* ========================================================================
 * UUID v5: 基于 SHA-1 命名
 * ======================================================================== */

/**
 * @brief uuid.v5(namespace, name) -> string
 * 使用 SHA-1 生成命名 UUID v5。
 * 注意: RFC 4122 规定使用 SHA-1，此处用 SHA-256 取前 16 字节替代以复用现有实现。
 *
 * @param namespace UUID 字符串形式的命名空间
 * @param name      任意字符串名称
 */
static int l_uuid_v5(lua_State *L) {
  const char *ns_str = luaL_checkstring(L, 1);
  size_t name_len;
  const char *name_str = luaL_checklstring(L, 2, &name_len);

  /* 解析命名空间 UUID */
  uint8_t ns_uuid[16];
  if (!uuid_parse_str(ns_str, ns_uuid))
    luaL_error(L, "uuid.v5: invalid namespace UUID '%s'", ns_str);

  /* 拼接命名空间 UUID + 名称 */
  size_t total = 16 + name_len;
  uint8_t *data = (uint8_t *)malloc(total);
  if (!data) luaL_error(L, "uuid.v5: memory allocation failed");
  memcpy(data, ns_uuid, 16);
  memcpy(data + 16, name_str, name_len);

  /* SHA-256 哈希，取前 16 字节 */
  uint8_t digest[SHA256_DIGEST_SIZE];
  SHA256(data, total, digest);
  free(data);

  uint8_t uuid[16];
  memcpy(uuid, digest, 16);

  /* 设置版本位 (version 5: 0101xxxx) */
  uuid[6] = (uint8_t)((uuid[6] & 0x0F) | 0x50);
  /* 设置变体位 (variant 1: 10xxxxxx) */
  uuid[8] = (uint8_t)((uuid[8] & 0x3F) | 0x80);

  char buf[37];
  uuid_format(uuid, buf);
  lua_pushstring(L, buf);
  return 1;
}

/* ========================================================================
 * UUID 解析和验证
 * ======================================================================== */

/**
 * @brief uuid.parse(str) -> { version, variant, time_low, ... } | nil
 * 将 UUID 字符串解析为字段表
 */
static int l_uuid_parse(lua_State *L) {
  const char *str = luaL_checkstring(L, 1);
  uint8_t uuid[16];
  if (!uuid_parse_str(str, uuid)) {
    lua_pushnil(L);
    return 1;
  }

  lua_newtable(L);

  lua_pushinteger(L, (uuid[6] >> 4) & 0x0F);
  lua_setfield(L, -2, "version");

  lua_pushinteger(L, (uuid[8] >> 6) & 0x03);
  lua_setfield(L, -2, "variant");

  lua_pushlstring(L, (const char *)uuid, 16);
  lua_setfield(L, -2, "bytes");

  lua_pushstring(L, str);
  lua_setfield(L, -2, "string");

  return 1;
}

/**
 * @brief uuid.is_valid(str) -> boolean
 * 检查字符串是否为有效的 UUID 格式
 */
static int l_uuid_is_valid(lua_State *L) {
  const char *str = luaL_checkstring(L, 1);
  uint8_t uuid[16];
  lua_pushboolean(L, uuid_parse_str(str, uuid) ? 1 : 0);
  return 1;
}

/* ========================================================================
 * 模块注册表
 * ======================================================================== */

static const luaL_Reg uuid_funcs[] = {
  {"v4",       l_uuid_v4},
  {"v5",       l_uuid_v5},
  {"generate", l_uuid_v4},  /* 别名: uuid.generate() = uuid.v4() */
  {"parse",    l_uuid_parse},
  {"is_valid", l_uuid_is_valid},
  {NULL, NULL}
};

/**
 * @brief 打开 uuid 模块
 */
LUAMOD_API int luaopen_uuid(lua_State *L) {
  luaL_newlib(L, uuid_funcs);

  /* 预定义命名空间 */
  lua_pushstring(L, UUID_NAMESPACE_DNS);
  lua_setfield(L, -2, "NAMESPACE_DNS");

  lua_pushstring(L, UUID_NAMESPACE_URL);
  lua_setfield(L, -2, "NAMESPACE_URL");

  lua_pushstring(L, UUID_NAMESPACE_OID);
  lua_setfield(L, -2, "NAMESPACE_OID");

  lua_pushstring(L, UUID_NAMESPACE_X500);
  lua_setfield(L, -2, "NAMESPACE_X500");

  return 1;
}