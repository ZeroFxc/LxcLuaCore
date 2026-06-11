/**
 * @file lcrypto.c
 * @brief LXCLUA 统一密码算法库 - Lua 绑定 (子表结构)
 *
 * 模块结构:
 *   crypto.sha256(data)           -> hex
 *   crypto.sha256.raw(data)       -> raw bytes
 *   crypto.hmac.sha256(key, data) -> hex
 *   crypto.hmac.sha256_raw(key, data) -> raw bytes
 *   crypto.aes.encrypt(key, data, mode, iv)
 *   crypto.aes.decrypt(key, data, mode, iv)
 *   crypto.crc32(data)            -> integer
 *   crypto.random.bytes(n)        -> raw bytes
 *   crypto.random.hex(n)          -> hex string
 *   crypto.random.int([min,] max) -> integer
 *   crypto.random.seed(seed)
 */

#define LUA_LIB

#include "lprefix.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "sha256.h"
#include "aes.h"
#include "crc.h"
#include "csprng.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ========================================================================
 * 内部辅助函数
 * ======================================================================== */

/**
 * @brief 将二进制数据转换为十六进制字符串
 */
static void bin_to_hex(char *dst, const uint8_t *src, size_t len) {
  static const char hex[] = "0123456789abcdef";
  for (size_t i = 0; i < len; i++) {
    dst[i * 2]     = hex[src[i] >> 4];
    dst[i * 2 + 1] = hex[src[i] & 0x0F];
  }
  dst[len * 2] = '\0';
}

/* 全局 CSPRNG 状态 (惰性初始化) */
static CSPRNG_State g_csprng;
static int g_csprng_inited = 0;

static CSPRNG_State* get_csprng(void) {
  if (!g_csprng_inited) {
    uint64_t seed = (uint64_t)time(NULL);
    csprng_init(&g_csprng, seed);
    g_csprng_inited = 1;
  }
  return &g_csprng;
}

static void push_hex(lua_State *L, const uint8_t *data, size_t len) {
  char *hex = (char *)malloc(len * 2 + 1);
  if (!hex) luaL_error(L, "memory allocation failed");
  bin_to_hex(hex, data, len);
  lua_pushstring(L, hex);
  free(hex);
}

/* ========================================================================
 * crypto.sha256 子表
 * ======================================================================== */

/**
 * @brief crypto.sha256(data) -> hex_string
 * 支持两种调用方式:
 *   crypto.sha256("data")       -- 直接调用 (通过 __call)
 *   crypto.sha256.hash("data")  -- 显式调用
 */
static int l_sha256(lua_State *L) {
  /* 判断是否通过 __call 调用 (self 是表) */
  int has_self = (lua_gettop(L) >= 1) && lua_istable(L, 1);
  int arg_idx = has_self ? 2 : 1;
  size_t len;
  const char *data = luaL_checklstring(L, arg_idx, &len);
  uint8_t digest[SHA256_DIGEST_SIZE];
  SHA256((const uint8_t *)data, len, digest);
  push_hex(L, digest, SHA256_DIGEST_SIZE);
  return 1;
}

/**
 * @brief crypto.sha256.raw(data) -> raw_bytes
 */
static int l_sha256_raw(lua_State *L) {
  /* 判断是否通过 __call 调用 (self 是表) */
  int has_self = (lua_gettop(L) >= 1) && lua_istable(L, 1);
  int arg_idx = has_self ? 2 : 1;
  size_t len;
  const char *data = luaL_checklstring(L, arg_idx, &len);
  uint8_t digest[SHA256_DIGEST_SIZE];
  SHA256((const uint8_t *)data, len, digest);
  lua_pushlstring(L, (const char *)digest, SHA256_DIGEST_SIZE);
  return 1;
}

static const luaL_Reg sha256_funcs[] = {
  {"__call",    l_sha256},
  {"raw",       l_sha256_raw},
  {NULL, NULL}
};

/* ========================================================================
 * crypto.hmac 子表
 * ======================================================================== */

/**
 * @brief crypto.hmac.sha256(key, data) -> hex_string
 */
static int l_hmac_sha256(lua_State *L) {
  size_t key_len, data_len;
  const char *key = luaL_checklstring(L, 1, &key_len);
  const char *data = luaL_checklstring(L, 2, &data_len);
  uint8_t digest[SHA256_DIGEST_SIZE];
  HMAC_SHA256((const uint8_t *)key, key_len,
              (const uint8_t *)data, data_len, digest);
  push_hex(L, digest, SHA256_DIGEST_SIZE);
  return 1;
}

/**
 * @brief crypto.hmac.sha256_raw(key, data) -> raw_bytes
 */
static int l_hmac_sha256_raw(lua_State *L) {
  size_t key_len, data_len;
  const char *key = luaL_checklstring(L, 1, &key_len);
  const char *data = luaL_checklstring(L, 2, &data_len);
  uint8_t digest[SHA256_DIGEST_SIZE];
  HMAC_SHA256((const uint8_t *)key, key_len,
              (const uint8_t *)data, data_len, digest);
  lua_pushlstring(L, (const char *)digest, SHA256_DIGEST_SIZE);
  return 1;
}

static const luaL_Reg hmac_funcs[] = {
  {"sha256",      l_hmac_sha256},
  {"sha256_raw",  l_hmac_sha256_raw},
  {NULL, NULL}
};

/* ========================================================================
 * crypto.aes 子表
 * ======================================================================== */

static void check_block_aligned(lua_State *L, size_t len, const char *mode) {
  if (len % AES_BLOCKLEN != 0)
    luaL_error(L, "AES %s: data length must be multiple of 16 (got %d)", mode, (int)len);
}

/**
 * @brief crypto.aes.encrypt(key, data [, mode [, iv]]) -> encrypted
 * mode: "ecb" (默认), "cbc", "ctr"
 */
static int l_aes_encrypt(lua_State *L) {
  size_t key_len, data_len;
  const char *key_str = luaL_checklstring(L, 1, &key_len);
  const char *data_str = luaL_checklstring(L, 2, &data_len);
  const char *mode_str = luaL_optstring(L, 3, "ecb");

  if (key_len != AES_KEYLEN)
    luaL_error(L, "AES: key must be exactly 16 bytes (got %d)", (int)key_len);

  struct AES_ctx ctx;
  uint8_t *buf = (uint8_t *)malloc(data_len);
  if (!buf) luaL_error(L, "AES: memory allocation failed");
  memcpy(buf, data_str, data_len);

  if (strcmp(mode_str, "ecb") == 0) {
    check_block_aligned(L, data_len, "ECB");
    AES_init_ctx(&ctx, (const uint8_t *)key_str);
    for (size_t i = 0; i < data_len; i += AES_BLOCKLEN)
      AES_ECB_encrypt(&ctx, buf + i);
  }
  else if (strcmp(mode_str, "cbc") == 0) {
    check_block_aligned(L, data_len, "CBC");
    size_t iv_len;
    const char *iv_str = luaL_checklstring(L, 4, &iv_len);
    if (iv_len != AES_BLOCKLEN)
      luaL_error(L, "AES CBC: IV must be exactly 16 bytes (got %d)", (int)iv_len);
    AES_init_ctx_iv(&ctx, (const uint8_t *)key_str, (const uint8_t *)iv_str);
    AES_CBC_encrypt_buffer(&ctx, buf, (uint32_t)data_len);
  }
  else if (strcmp(mode_str, "ctr") == 0) {
    size_t iv_len;
    const char *iv_str = luaL_checklstring(L, 4, &iv_len);
    if (iv_len != AES_BLOCKLEN)
      luaL_error(L, "AES CTR: IV must be exactly 16 bytes (got %d)", (int)iv_len);
    AES_init_ctx_iv(&ctx, (const uint8_t *)key_str, (const uint8_t *)iv_str);
    AES_CTR_xcrypt_buffer(&ctx, buf, (uint32_t)data_len);
  }
  else {
    free(buf);
    luaL_error(L, "AES: unsupported mode '%s' (use ecb, cbc, or ctr)", mode_str);
  }

  lua_pushlstring(L, (const char *)buf, data_len);
  free(buf);
  return 1;
}

/**
 * @brief crypto.aes.decrypt(key, data [, mode [, iv]]) -> decrypted
 */
static int l_aes_decrypt(lua_State *L) {
  size_t key_len, data_len;
  const char *key_str = luaL_checklstring(L, 1, &key_len);
  const char *data_str = luaL_checklstring(L, 2, &data_len);
  const char *mode_str = luaL_optstring(L, 3, "ecb");

  if (key_len != AES_KEYLEN)
    luaL_error(L, "AES: key must be exactly 16 bytes (got %d)", (int)key_len);

  struct AES_ctx ctx;
  uint8_t *buf = (uint8_t *)malloc(data_len);
  if (!buf) luaL_error(L, "AES: memory allocation failed");
  memcpy(buf, data_str, data_len);

  if (strcmp(mode_str, "ecb") == 0) {
    check_block_aligned(L, data_len, "ECB");
    AES_init_ctx(&ctx, (const uint8_t *)key_str);
    for (size_t i = 0; i < data_len; i += AES_BLOCKLEN)
      AES_ECB_decrypt(&ctx, buf + i);
  }
  else if (strcmp(mode_str, "cbc") == 0) {
    check_block_aligned(L, data_len, "CBC");
    size_t iv_len;
    const char *iv_str = luaL_checklstring(L, 4, &iv_len);
    if (iv_len != AES_BLOCKLEN)
      luaL_error(L, "AES CBC: IV must be exactly 16 bytes (got %d)", (int)iv_len);
    AES_init_ctx_iv(&ctx, (const uint8_t *)key_str, (const uint8_t *)iv_str);
    AES_CBC_decrypt_buffer(&ctx, buf, (uint32_t)data_len);
  }
  else if (strcmp(mode_str, "ctr") == 0) {
    size_t iv_len;
    const char *iv_str = luaL_checklstring(L, 4, &iv_len);
    if (iv_len != AES_BLOCKLEN)
      luaL_error(L, "AES CTR: IV must be exactly 16 bytes (got %d)", (int)iv_len);
    AES_init_ctx_iv(&ctx, (const uint8_t *)key_str, (const uint8_t *)iv_str);
    AES_CTR_xcrypt_buffer(&ctx, buf, (uint32_t)data_len);
  }
  else {
    free(buf);
    luaL_error(L, "AES: unsupported mode '%s' (use ecb, cbc, or ctr)", mode_str);
  }

  lua_pushlstring(L, (const char *)buf, data_len);
  free(buf);
  return 1;
}

static const luaL_Reg aes_funcs[] = {
  {"encrypt",   l_aes_encrypt},
  {"decrypt",   l_aes_decrypt},
  {NULL, NULL}
};

/* ========================================================================
 * crypto.crc32(data) -> integer
 * ======================================================================== */

static int l_crc32(lua_State *L) {
  size_t len;
  const char *data = luaL_checklstring(L, 1, &len);
  unsigned int crc = naga_crc32((unsigned char *)data, (unsigned int)len);
  lua_pushinteger(L, crc);
  return 1;
}

/* ========================================================================
 * crypto.random 子表
 * ======================================================================== */

/**
 * @brief crypto.random.bytes(n) -> raw_bytes
 */
static int l_random_bytes(lua_State *L) {
  lua_Integer n = luaL_checkinteger(L, 1);
  if (n <= 0) luaL_error(L, "random.bytes: length must be positive");
  if (n > (1 << 20)) luaL_error(L, "random.bytes: length too large (max 1MB)");
  uint8_t *buf = (uint8_t *)malloc((size_t)n);
  if (!buf) luaL_error(L, "random.bytes: memory allocation failed");
  csprng_bytes(get_csprng(), buf, (size_t)n);
  lua_pushlstring(L, (const char *)buf, (size_t)n);
  free(buf);
  return 1;
}

/**
 * @brief crypto.random.hex(n) -> hex_string
 */
static int l_random_hex(lua_State *L) {
  lua_Integer n = luaL_checkinteger(L, 1);
  if (n <= 0) luaL_error(L, "random.hex: length must be positive");
  if (n > (1 << 19)) luaL_error(L, "random.hex: length too large (max 512KB)");
  uint8_t *buf = (uint8_t *)malloc((size_t)n);
  if (!buf) luaL_error(L, "random.hex: allocation failed");
  csprng_bytes(get_csprng(), buf, (size_t)n);
  push_hex(L, buf, (size_t)n);
  free(buf);
  return 1;
}

/**
 * @brief crypto.random.int([min,] max) -> integer
 */
static int l_random_int(lua_State *L) {
  lua_Integer min = 0, max;
  if (lua_gettop(L) == 1) {
    max = luaL_checkinteger(L, 1);
  } else {
    min = luaL_checkinteger(L, 1);
    max = luaL_checkinteger(L, 2);
  }
  if (min > max)
    luaL_error(L, "random.int: min (%d) > max (%d)", (int)min, (int)max);
  uint64_t range = (uint64_t)(max - min);
  uint64_t r = csprng_range(get_csprng(), range + 1);
  lua_pushinteger(L, (lua_Integer)(min + (lua_Integer)r));
  return 1;
}

/**
 * @brief crypto.random.seed(seed)
 */
static int l_random_seed(lua_State *L) {
  lua_Integer seed = luaL_checkinteger(L, 1);
  csprng_init(get_csprng(), (uint64_t)seed);
  return 0;
}

static const luaL_Reg random_funcs[] = {
  {"bytes", l_random_bytes},
  {"hex",   l_random_hex},
  {"int",   l_random_int},
  {"seed",  l_random_seed},
  {NULL, NULL}
};

/* ========================================================================
 * 创建子表并设置 __call 元方法的辅助函数
 * ======================================================================== */

/**
 * @brief 创建一个带 __call 元方法的子表
 * @param L     Lua 状态
 * @param funcs 函数注册表 (第一个函数作为 __call)
 * @param name  调试名称 (可为 NULL)
 */
static void new_subtable_callable(lua_State *L, const luaL_Reg *funcs, const char *name) {
  luaL_newlib(L, funcs);                    /* 子表 */
  /* 设置 __call 为子表的第一个函数 (即 sha256 函数自身) */
  lua_pushvalue(L, -1);                     /* 复制子表 */
  lua_pushcfunction(L, funcs[0].func);      /* 推入函数的 C 闭包 */
  lua_setfield(L, -2, "__call");            /* 子表.__call = func */
  if (name) {
    lua_pushstring(L, name);
    lua_setfield(L, -2, "__name");
  }
  lua_setmetatable(L, -2);                  /* 设置元表 */
}

/* ========================================================================
 * 模块入口: 打开 crypto 模块
 * ======================================================================== */

LUAMOD_API int luaopen_crypto(lua_State *L) {
  lua_createtable(L, 0, 7);  /* 主表: sha256, hmac, aes, crc32, random + 3常量 */

  /* -- crypto.sha256 子表 (可调用) -- */
  new_subtable_callable(L, sha256_funcs, "sha256");
  lua_setfield(L, -2, "sha256");

  /* -- crypto.hmac 子表 -- */
  luaL_newlib(L, hmac_funcs);
  lua_setfield(L, -2, "hmac");

  /* -- crypto.aes 子表 -- */
  luaL_newlib(L, aes_funcs);
  lua_setfield(L, -2, "aes");

  /* -- crypto.crc32 函数 -- */
  lua_pushcfunction(L, l_crc32);
  lua_setfield(L, -2, "crc32");

  /* -- crypto.random 子表 -- */
  luaL_newlib(L, random_funcs);
  lua_setfield(L, -2, "random");

  /* -- 常量 -- */
  lua_pushinteger(L, SHA256_DIGEST_SIZE);
  lua_setfield(L, -2, "SHA256_SIZE");

  lua_pushinteger(L, AES_BLOCKLEN);
  lua_setfield(L, -2, "AES_BLOCK");

  lua_pushinteger(L, AES_KEYLEN);
  lua_setfield(L, -2, "AES_KEY_SIZE");

  return 1;
}