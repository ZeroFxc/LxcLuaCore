/*
** $Id: lstrlib.c $
** Standard library for string operations and pattern-matching
** See Copyright Notice in lua.h
*/

#define lstrlib_c
#define LUA_LIB

#include "lprefix.h"


#include <ctype.h>
#include <float.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdint.h>

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"
#include "llimits.h"

#include "aes.h"
#include "crc.h"
#include "sha256.h"
#include "csprng.h"
#include "config.h"
#include "pcre2.h"

/* LXCLUA 正则引擎开关，由 jit.regex.pcre2.on()/jit.regex.pcre2.off() 控制 */
extern int XCLUA_PCRE2_ENABLED;
/* LXCLUA 正则 JIT 开关，由 jit.regex.on()/jit.regex.off() 控制 */
extern int XCLUA_REGEX_JIT_ENABLED;

/* 原版 Lua 正则引擎常量 */
#define L_ESC       '%'
#define SPECIALS    "^$*+?.([%-"
#define MAXCCALLS   200
#define CAP_UNFINISHED  (-1)
#define CAP_POSITION    (-2)


/*
** maximum number of captures that a pattern can do during
** pattern-matching. This limit is arbitrary, but must fit in
** an unsigned char.
*/
#if !defined(LUA_MAXCAPTURES)
#define LUA_MAXCAPTURES		32
#endif


/* macro to 'unsign' a character */
#define uchar(c)	((unsigned char)(c))


/*
** Some sizes are better limited to fit in 'int', but must also fit in
** 'size_t'. (We assume that 'lua_Integer' cannot be smaller than 'int'.)
*/
#define MAX_SIZET	((size_t)(~(size_t)0))

#define MAXSIZE  \
	(sizeof(size_t) < sizeof(int) ? MAX_SIZET : (size_t)(INT_MAX))




static int str_len (lua_State *L) {
  size_t l;
  luaL_checklstring(L, 1, &l);
  lua_pushinteger(L, (lua_Integer)l);
  return 1;
}


/*
** translate a relative initial string position
** (negative means back from end): clip result to [1, inf).
** The length of any string in Lua must fit in a lua_Integer,
** so there are no overflows in the casts.
** The inverted comparison avoids a possible overflow
** computing '-pos'.
*/
static size_t posrelatI (lua_Integer pos, size_t len) {
  if (pos > 0)
    return (size_t)pos;
  else if (pos == 0)
    return 1;
  else if (pos < -(lua_Integer)len)  /* inverted comparison */
    return 1;  /* clip to 1 */
  else return len + (size_t)pos + 1;
}


/*
** Gets an optional ending string position from argument 'arg',
** with default value 'def'.
** Negative means back from end: clip result to [0, len]
*/
static size_t getendpos (lua_State *L, int arg, lua_Integer def,
                         size_t len) {
  lua_Integer pos = luaL_optinteger(L, arg, def);
  if (pos > (lua_Integer)len)
    return len;
  else if (pos >= 0)
    return (size_t)pos;
  else if (pos < -(lua_Integer)len)
    return 0;
  else return len + (size_t)pos + 1;
}

/* translate a relative string position: negative means back from end */
static lua_Integer posrelat (lua_Integer pos, size_t len) {
    if (pos >= 0) return pos;
    else if (0u - (size_t)pos > len) return 0;
    else return (lua_Integer)len + pos + 1;
}

static int str_sub (lua_State *L) {
  size_t l;
  const char *s = luaL_checklstring(L, 1, &l);
  size_t start = posrelatI(luaL_checkinteger(L, 2), l);
  size_t end = getendpos(L, 3, -1, l);
  if (start <= end)
    lua_pushlstring(L, s + start - 1, (end - start) + 1);
  else lua_pushliteral(L, "");
  return 1;
}


static int str_reverse (lua_State *L) {
  size_t l, i;
  luaL_Buffer b;
  const char *s = luaL_checklstring(L, 1, &l);
  char *p = luaL_buffinitsize(L, &b, l);
  for (i = 0; i < l; i++)
    p[i] = s[l - i - 1];
  luaL_pushresultsize(&b, l);
  return 1;
}


static int str_lower (lua_State *L) {
  size_t l;
  size_t i;
  luaL_Buffer b;
  const char *s = luaL_checklstring(L, 1, &l);
  char *p = luaL_buffinitsize(L, &b, l);
  for (i=0; i<l; i++)
    p[i] = cast_char(tolower(cast_uchar(s[i])));
  luaL_pushresultsize(&b, l);
  return 1;
}


static int str_upper (lua_State *L) {
  size_t l;
  size_t i;
  luaL_Buffer b;
  const char *s = luaL_checklstring(L, 1, &l);
  char *p = luaL_buffinitsize(L, &b, l);
  for (i=0; i<l; i++)
    p[i] = cast_char(toupper(cast_uchar(s[i])));
  luaL_pushresultsize(&b, l);
  return 1;
}


/*
** MAX_SIZE is limited both by size_t and lua_Integer.
** When x <= MAX_SIZE, x can be safely cast to size_t or lua_Integer.
*/
static int str_rep (lua_State *L) {
  size_t len, lsep;
  const char *s = luaL_checklstring(L, 1, &len);
  lua_Integer n = luaL_checkinteger(L, 2);
  const char *sep = luaL_optlstring(L, 3, "", &lsep);
  if (n <= 0)
    lua_pushliteral(L, "");
  else if (l_unlikely(len > MAX_SIZE - lsep ||
               cast_st2S(len + lsep) > cast_st2S(MAX_SIZE) / n))
    return luaL_error(L, "resulting string too long");
  else {
    size_t totallen = (cast_sizet(n) * (len + lsep)) - lsep;
    luaL_Buffer b;
    char *p = luaL_buffinitsize(L, &b, totallen);
    while (n-- > 1) {  /* first n-1 copies (followed by separator) */
      memcpy(p, s, len * sizeof(char)); p += len;
      if (lsep > 0) {  /* empty 'memcpy' is not that cheap */
        memcpy(p, sep, lsep * sizeof(char)); p += lsep;
      }
    }
    memcpy(p, s, len * sizeof(char));  /* last copy without separator */
    luaL_pushresultsize(&b, totallen);
  }
  return 1;
}


static int str_byte (lua_State *L) {
  size_t l;
  const char *s = luaL_checklstring(L, 1, &l);
  lua_Integer pi = luaL_optinteger(L, 2, 1);
  size_t posi = posrelatI(pi, l);
  size_t pose = getendpos(L, 3, pi, l);
  int n, i;
  if (posi > pose) return 0;  /* empty interval; return no values */
  if (l_unlikely(pose - posi >= (size_t)INT_MAX))  /* arithmetic overflow? */
    return luaL_error(L, "string slice too long");
  n = (int)(pose -  posi) + 1;
  luaL_checkstack(L, n, "string slice too long");
  for (i=0; i<n; i++)
    lua_pushinteger(L, cast_uchar(s[posi + cast_uint(i) - 1]));
  return n;
}


static int str_char (lua_State *L) {
  int n = lua_gettop(L);  /* number of arguments */
  int i;
  luaL_Buffer b;
  char *p = luaL_buffinitsize(L, &b, cast_uint(n));
  for (i=1; i<=n; i++) {
    lua_Unsigned c = (lua_Unsigned)luaL_checkinteger(L, i);
    luaL_argcheck(L, c <= (lua_Unsigned)UCHAR_MAX, i, "value out of range");
    p[i - 1] = cast_char(cast_uchar(c));
  }
  luaL_pushresultsize(&b, cast_uint(n));
  return 1;
}


/*
** Buffer to store the result of 'string.dump'. It must be initialized
** after the call to 'lua_dump', to ensure that the function is on the
** top of the stack when 'lua_dump' is called. ('luaL_buffinit' might
** push stuff.)
*/
struct str_Writer {
  int init;  /* true iff buffer has been initialized */
  luaL_Buffer B;
};


static int writer (lua_State *L, const void *b, size_t size, void *ud) {
  struct str_Writer *state = (struct str_Writer *)ud;
  if (!state->init) {
    state->init = 1;
    luaL_buffinit(L, &state->B);
  }
  if (b == NULL) {  /* finishing dump? */
    luaL_pushresult(&state->B);  /* push result */
    lua_replace(L, 1);  /* move it to reserved slot */
  }
  else
    luaL_addlstring(&state->B, (const char *)b, size);
  return 0;
}


/*
** string.dump 函数实现
** 将Lua函数导出为字节码字符串
**
** 用法：
**   string.dump(func)                    -- 基本用法
**   string.dump(func, true)              -- 剥离调试信息
**   string.dump(func, {                  -- 表参数形式
**     strip = true,                      -- 是否剥离调试信息
**     obfuscate = 1,                     -- 混淆标志位（可组合）
**     seed = 12345                       -- 随机种子（可选，0或不指定表示使用时间）
**   })
**
** 混淆标志位：
**   0: 不混淆
**   1: 控制流扁平化 (CFF)
**   2: 基本块随机打乱
**   4: 虚假基本块（预留）
**   8: 状态值编码混淆
**
** @param L Lua状态
** @return 1（返回字节码字符串）
*/
/* Nirithy== Shell Generator */
static const char* nirithy_b64 = "9876543210zyxwvutsrqponmlkjihgfedcbaZYXWVUTSRQPONMLKJIHGFEDCBA-_";

static char* nirithy_encode(const unsigned char* input, size_t len) {
  size_t out_len = 4 * ((len + 2) / 3);
  char* out = (char*)malloc(out_len + 1);
  size_t i = 0, j = 0;
  if (!out) return NULL;
  while (i < len) {
    uint32_t octet_a = i < len ? input[i++] : 0;
    uint32_t octet_b = i < len ? input[i++] : 0;
    uint32_t octet_c = i < len ? input[i++] : 0;
    uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;
    out[j++] = nirithy_b64[(triple >> 18) & 0x3F];
    out[j++] = nirithy_b64[(triple >> 12) & 0x3F];
    out[j++] = nirithy_b64[(triple >> 6) & 0x3F];
    out[j++] = nirithy_b64[triple & 0x3F];
  }
  if (len % 3 == 1) {
    out[out_len - 1] = '=';
    out[out_len - 2] = '=';
  } else if (len % 3 == 2) {
    out[out_len - 1] = '=';
  }
  out[out_len] = '\0';
  return out;
}

static void nirithy_derive_key(uint64_t timestamp, uint8_t *key) {
  uint8_t input[32];
  uint8_t digest[SHA256_DIGEST_SIZE];

  /* Input: timestamp (8 bytes) + "NirithySalt" (11 bytes) */
  memcpy(input, &timestamp, 8);
  memcpy(input + 8, "NirithySalt", 11);

  SHA256(input, 19, digest);
  memcpy(key, digest, 16); /* Use first 16 bytes as AES-128 key */
}

static void aux_envelop(lua_State *L, const char *s, size_t l) {
  uint64_t timestamp = (uint64_t)time(NULL);

  /* Structure: Timestamp (8) + IV (16) + EncryptedData (l) */
  size_t payload_len = 8 + 16 + l;
  unsigned char *payload = (unsigned char *)malloc(payload_len);
  if (!payload) {
    luaL_error(L, "memory allocation failed");
    return;
  }

  /* 1. Timestamp */
  memcpy(payload, &timestamp, 8);

  /* 2. IV (密码学安全随机数) */
  {
    CSPRNG_State rng;
    csprng_init(&rng, (uint64_t)time(NULL) ^ (uint64_t)(size_t)&payload);
    csprng_bytes(&rng, payload + 8, 16);
  }

  /* 3. Encrypt Payload */
  {
    uint8_t key[16];
    struct AES_ctx ctx;
    nirithy_derive_key(timestamp, key);

    /* Copy data to payload buffer */
    memcpy(payload + 8 + 16, s, l);

    /* Encrypt the data part using AES-128-CTR */
    AES_init_ctx_iv(&ctx, key, payload + 8);
    AES_CTR_xcrypt_buffer(&ctx, payload + 8 + 16, (uint32_t)l);
  }

  char *encoded = nirithy_encode(payload, payload_len);
  free(payload);

  if (!encoded) {
    luaL_error(L, "encoding failed");
    return;
  }

  luaL_Buffer b;
  luaL_buffinit(L, &b);
  luaL_addstring(&b, "Nirithy==");
  luaL_addstring(&b, encoded);
  free(encoded);

  luaL_pushresult(&b);
}

static int str_envelop(lua_State *L) {
  size_t l;
  const char *s = luaL_checklstring(L, 1, &l);
  aux_envelop(L, s, l);
  return 1;
}

static int str_dump (lua_State *L) {
  struct str_Writer state;
  int strip = 0;
  int obfuscate_flags = 0;
  unsigned int seed = 0;
  int envelop = 1;  /* 默认带壳 */
  const char *log_path = NULL;  /* 日志输出路径 */
  
  luaL_checktype(L, 1, LUA_TFUNCTION);
  
  /* 检查第二个参数的类型 */
  if (lua_istable(L, 2)) {
    /* 表参数形式：读取各个字段 */
    
    /* 读取 strip 字段 */
    lua_getfield(L, 2, "strip");
    if (!lua_isnil(L, -1)) {
      strip = lua_toboolean(L, -1);
    }
    lua_pop(L, 1);
    
    /* 读取 obfuscate 字段（混淆标志位） */
    lua_getfield(L, 2, "obfuscate");
    if (!lua_isnil(L, -1)) {
      obfuscate_flags = (int)lua_tointeger(L, -1);
    }
    lua_pop(L, 1);
    
    /* 读取 seed 字段（随机种子） */
    lua_getfield(L, 2, "seed");
    if (!lua_isnil(L, -1)) {
      seed = (unsigned int)lua_tointeger(L, -1);
    }
    lua_pop(L, 1);

    /* 读取 envelop 字段 */
    lua_getfield(L, 2, "envelop");
    if (!lua_isnil(L, -1)) {
      envelop = lua_toboolean(L, -1);
    }
    lua_pop(L, 1);
    
    /* 读取 log_path 字段（调试日志输出路径）*/
    /* 注意：需要保持字符串在栈上直到 dump 完成 */
    lua_getfield(L, 2, "log_path");
    if (!lua_isnil(L, -1) && lua_isstring(L, -1)) {
      log_path = lua_tostring(L, -1);
      /* 字符串现在在栈顶，保持它在那里 */
    } else {
      lua_pop(L, 1);
      lua_pushnil(L);  /* 占位，保持栈结构一致 */
    }
    /* 现在栈是: [func, table, log_path_or_nil] */
  } else {
    /* 兼容旧的布尔参数形式 */
    strip = lua_toboolean(L, 2);
    lua_pushnil(L);  /* 占位 */
  }
  
  /* 栈: [func, table/bool, log_path_or_nil]
  ** lua_dump 需要函数在栈顶，但我们需要保留 log_path 字符串的引用
  ** 解决方案：把函数复制到栈顶
  */
  lua_pushvalue(L, 1);  /* 复制函数到栈顶 */
  /* 栈: [func, table/bool, log_path_or_nil, func_copy] */
  
  state.init = 0;
  
  int result;
  if (obfuscate_flags != 0) {
    /* 使用带混淆的导出函数 */
    result = lua_dump_obfuscated(L, writer, &state, strip, obfuscate_flags, seed, log_path);
  } else {
    /* 使用普通导出函数 */
    result = lua_dump(L, writer, &state, strip);
  }
  
  if (l_unlikely(result != 0))
    return luaL_error(L, "unable to dump given function1");
  luaL_pushresult(&state.B);

  if (envelop) {
    size_t l;
    const char *s = lua_tolstring(L, -1, &l);
    aux_envelop(L, s, l);
    /* Remove original dump string */
    lua_remove(L, -2);
  }
  return 1;
}



/*
** {===========================================
** METAMETHODS
** ============================================
*/

#if defined(LUA_NOCVTS2N)	/* { */

/* no coercion from strings to numbers */

static const luaL_Reg stringmetamethods[] = {
  {"__index", NULL},  /* placeholder */
  {NULL, NULL}
};

#else		/* }{ */

static int tonum (lua_State *L, int arg) {
  if (lua_type(L, arg) == LUA_TNUMBER) {  /* already a number? */
    lua_pushvalue(L, arg);
    return 1;
  }
  else {  /* check whether it is a numerical string */
    size_t len;
    const char *s = lua_tolstring(L, arg, &len);
    return (s != NULL && lua_stringtonumber(L, s) == len + 1);
  }
}


static void trymt (lua_State *L, const char *mtkey, const char *opname) {
  lua_settop(L, 2);  /* back to the original arguments */
  if (l_unlikely(lua_type(L, 2) == LUA_TSTRING ||
                 !luaL_getmetafield(L, 2, mtkey)))
    luaL_error(L, "attempt to %s a '%s' with a '%s'", opname,
                  luaL_typename(L, -2), luaL_typename(L, -1));
  lua_insert(L, -3);  /* put metamethod before arguments */
  lua_call(L, 2, 1);  /* call metamethod */
}

static int arith (lua_State *L, int op, const char *mtname) {
  if (tonum(L, 1) && tonum(L, 2))
    lua_arith(L, op);  /* result will be on the top */
  else
    trymt(L, mtname, mtname + 2);
  return 1;
}
/*
** 字符串拼接元方法
** 允许两个字符串通过 + 操作符进行拼接
*/
static int string_add (lua_State *L) {
  size_t l1, l2;
  const char *s1 = luaL_checklstring(L, 1, &l1);
  const char *s2 = luaL_checklstring(L, 2, &l2);
  
  /* 检查结果字符串长度是否超出限制 */
  if (l_unlikely(l1 > MAX_SIZE - l2))
    return luaL_error(L, "resulting string too long");
    
  luaL_Buffer b;
  char *p = luaL_buffinitsize(L, &b, l1 + l2);
  memcpy(p, s1, l1);
  memcpy(p + l1, s2, l2);
  luaL_pushresultsize(&b, l1 + l2);
  return 1;
}

/*
** 字符串子串剔除元方法
** 允许通过字符串 - 操作符剔除匹配到的子串（全部匹配）
*/
static int string_sub (lua_State *L) {
  size_t ls, lp;
  const char *s = luaL_checklstring(L, 1, &ls);  /* 原字符串 */
  const char *p = luaL_checklstring(L, 2, &lp);  /* 要剔除的子串 */
  
  if (lp == 0) {  /* 要剔除空字符串，返回原字符串 */
    lua_pushlstring(L, s, ls);
    return 1;
  }
  
  luaL_Buffer b;
  luaL_buffinit(L, &b);
  
  const char *current = s;
  const char *end = s + ls;
  
  /* 查找并剔除所有匹配的子串 */
  while (current < end) {
    const char *match_pos = (const char *)memchr(current, *p, end - current);
    if (!match_pos) {
      /* 没有找到匹配，添加剩余部分并结束 */
      luaL_addlstring(&b, current, end - current);
      break;
    }
    
    /* 检查是否完全匹配 */
    if (match_pos + lp <= end && memcmp(match_pos, p, lp) == 0) {
      /* 找到完整匹配，添加匹配之前的内容，然后跳过匹配的部分 */
      luaL_addlstring(&b, current, match_pos - current);
      current = match_pos + lp;
    } else {
      /* 不是完整匹配，移动到下一个位置 */
      current = match_pos + 1;
    }
  }
  
  luaL_pushresult(&b);
  return 1;
}

/*
** 字符串索引元方法
** 允许通过数字索引获取对应字节的值
*/
static int string_index (lua_State *L) {
  size_t ls;
  const char *s = luaL_checklstring(L, 1, &ls);  /* 字符串 */
  lua_Integer index = luaL_checkinteger(L, 2);  /* 索引 */
  
  /* 处理负索引（从末尾开始计数） */
  if (index < 0) {
    index = (lua_Integer)ls + index + 1;
  }
  
  /* 检查索引是否有效（Lua 索引从1开始） */
  if (index < 1 || index > (lua_Integer)ls) {
    lua_pushnil(L);  /* 返回nil表示索引无效 */
    return 1;
  }
  
  /* 返回对应位置的字节值 */
  lua_pushinteger(L, (unsigned char)s[index - 1]);
  return 1;
}

static int arith_mul (lua_State *L) {
  return arith(L, LUA_OPMUL, "__mul");
}

static int arith_mod (lua_State *L) {
  return arith(L, LUA_OPMOD, "__mod");
}

static int arith_pow (lua_State *L) {
  return arith(L, LUA_OPPOW, "__pow");
}

static int arith_div (lua_State *L) {
  return arith(L, LUA_OPDIV, "__div");
}

static int arith_idiv (lua_State *L) {
  return arith(L, LUA_OPIDIV, "__idiv");
}

static int arith_unm (lua_State *L) {
  return arith(L, LUA_OPUNM, "__unm");
}


static const luaL_Reg stringmetamethods[] = {
  {"__add", string_add},
  {"__sub", string_sub},
  {"__index", string_index},
  {"__mul", arith_mul},
  {"__mod", arith_mod},
  {"__pow", arith_pow},
  {"__div", arith_div},
  {"__idiv", arith_idiv},
  {"__unm", arith_unm},
  {NULL, NULL}
};

#endif		/* } */

/* }=========================================== */

/*
** {===========================================
** PATTERN MATCHING (PCRE2 引擎)
** ============================================
*/

/* PCRE2 特殊字符（先 undef 避免与原版 Lua 引擎宏冲突） */
#undef SPECIALS
#define SPECIALS	"\\^$.*+?()[]{}|"

/* 捕获常量 */
#undef CAP_POSITION
#define CAP_POSITION	(-2)
#undef L_ESC
#define L_ESC		'%'

/* 前向声明：纯文本查找 */
static const char *lmemfind (const char *s1, size_t l1,
                               const char *s2, size_t l2);

/* PCRE2 + 原版 Lua 双引擎匹配状态 */
typedef struct MatchState {
  /* 公共字段 */
  const char *src_init;
  const char *src_end;
  lua_State *L;
  /* PCRE2 引擎字段 */
  pcre2_code *code;
  pcre2_match_data *mdata;
  PCRE2_SIZE *ovector;
  uint32_t ovec_count;   /* ovector 元素总数（用于边界检查） */
  uint32_t ovec_pairs;   /* pcre2_match 实际返回的 pair 数 */
  /* 原版 Lua 引擎字段 */
  const char *p_end;
  int matchdepth;
  int level;
  struct {
    const char *init;
    ptrdiff_t len;
  } capture[LUA_MAXCAPTURES];
} MatchState;

/* ===== PCRE2 正则编译缓存（LRU） ===== */
/* 只缓存 pcre2_code（不可变），不缓存 pcre2_match_data（每次调用 pcre2_match 会修改其内部数据） */
#define PATTERN_CACHE_SIZE 16

typedef struct PatternCacheEntry {
  char *pattern;           /* 模式字符串副本 */
  size_t pattern_len;      /* 模式长度 */
  pcre2_code *code;        /* 编译后的正则 */
  int last_used;           /* LRU 计数器，值越大表示最近使用过 */
} PatternCacheEntry;

static PatternCacheEntry pattern_cache[PATTERN_CACHE_SIZE];
static int pattern_cache_counter = 0;
static int pattern_cache_initialized = 0;

/* 初始化缓存 */
static void init_cache (void) {
  memset(pattern_cache, 0, sizeof(pattern_cache));
  pattern_cache_initialized = 1;
}

/* 从缓存中查找已编译的模式，返回 code 或 NULL */
static pcre2_code *cache_lookup (const char *p, size_t lp) {
  if (!pattern_cache_initialized) init_cache();
  for (int i = 0; i < PATTERN_CACHE_SIZE; i++) {
    PatternCacheEntry *e = &pattern_cache[i];
    if (e->pattern != NULL && e->pattern_len == lp
        && memcmp(e->pattern, p, lp) == 0) {
      e->last_used = ++pattern_cache_counter;
      return e->code;
    }
  }
  return NULL;
}

/* 将编译后的模式加入缓存，缓存满时淘汰最久未使用的条目 */
static void cache_insert (const char *p, size_t lp, pcre2_code *code) {
  if (!pattern_cache_initialized) init_cache();
  int slot = -1;
  int min_used = INT_MAX;
  for (int i = 0; i < PATTERN_CACHE_SIZE; i++) {
    if (pattern_cache[i].pattern == NULL) { slot = i; break; }
    if (pattern_cache[i].last_used < min_used) {
      min_used = pattern_cache[i].last_used;
      slot = i;
    }
  }
  /* 淘汰旧条目 */
  if (pattern_cache[slot].pattern != NULL) {
    free(pattern_cache[slot].pattern);
    pcre2_code_free(pattern_cache[slot].code);
  }
  /* 存入新条目 */
  pattern_cache[slot].pattern = (char *)malloc(lp + 1);
  memcpy(pattern_cache[slot].pattern, p, lp);
  pattern_cache[slot].pattern[lp] = '\0';
  pattern_cache[slot].pattern_len = lp;
  pattern_cache[slot].code = code;
  pattern_cache[slot].last_used = ++pattern_cache_counter;
}

/*
** 将 Lua 正则模式转换为 PCRE2 模式
** 支持转换：%d→\d, %w→\w, %s→\s, %a→[a-zA-Z], %l→[a-z], %u→[A-Z],
**   %x→[0-9a-fA-F], %p→[!-/:-@[-`{-~], %g→[!-~], %c→[\\x00-\\x1f\\x7f],
**   %z→\\0, %%→%, %1-%9→\1-\9, %X→\\X (转义魔法字符),
**   -→*? (懒惰量词)
** 返回转换后的字符串（调用者需 free），失败返回 NULL
*/
static char *lua_pattern_to_pcre2 (const char *p, size_t lp, size_t *out_len) {
  /* 预估输出缓冲区：最坏情况 %p→38字节(19x膨胀)，加结尾\0 */
  size_t bufsz = lp * 20 + 16;
  char *buf = (char *)malloc(bufsz);
  if (!buf) return NULL;
  size_t pos = 0;
  int in_bracket = 0;  /* 是否在 [...] 字符集内部 */

  for (size_t i = 0; i < lp; i++) {
    char c = p[i];
    if (c == '[') {
      in_bracket = 1;
      buf[pos++] = c;
    }
    else if (c == ']') {
      in_bracket = 0;
      buf[pos++] = c;
    }
    else if (c == '%' && i + 1 < lp) {
      char nc = p[++i];
      switch (nc) {
        case 'd': memcpy(buf + pos, "\\d", 2); pos += 2; break;
        case 'w': memcpy(buf + pos, "\\w", 2); pos += 2; break;
        case 's': memcpy(buf + pos, "\\s", 2); pos += 2; break;
        case 'a': memcpy(buf + pos, "[a-zA-Z]", 8); pos += 8; break;
        case 'l': memcpy(buf + pos, "[a-z]", 5); pos += 5; break;
        case 'u': memcpy(buf + pos, "[A-Z]", 5); pos += 5; break;
        case 'x': memcpy(buf + pos, "[0-9a-fA-F]", 11); pos += 11; break;
        case 'p': memcpy(buf + pos, "[\\x21-\\x2f\\x3a-\\x40\\x5b-\\x60\\x7b-\\x7e]", 38); pos += 38; break;
        case 'g': memcpy(buf + pos, "[!-~]", 5); pos += 5; break;
        case 'c': memcpy(buf + pos, "[\\x00-\\x1f\\x7f]", 15); pos += 15; break;
        case 'z': memcpy(buf + pos, "\\0", 2); pos += 2; break;
        case '%': buf[pos++] = '%'; break;
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
          buf[pos++] = '\\';
          buf[pos++] = nc;
          break;
        case 'b': case 'f':  /* %bxy 和 %f[set] 保留原样，PCRE2 不支持 */
          buf[pos++] = '%';
          buf[pos++] = nc;
          break;
        default:  /* %. → \. 等转义魔法字符 */
          buf[pos++] = '\\';
          buf[pos++] = nc;
          break;
      }
    }
    else if (c == '-' && !in_bracket) {
      /* Lua 懒惰量词 → PCRE2 *? */
      memcpy(buf + pos, "*?", 2); pos += 2;
    }
    else {
      buf[pos++] = c;
    }
  }
  buf[pos] = '\0';
  *out_len = pos;
  return buf;
}

/* 编译正则模式（带缓存） */
static pcre2_code *pcre2_compile_pattern (lua_State *L, const char *p, size_t lp,
                                     pcre2_match_data **mdata) {
  /* 先查 LRU 缓存 */
  pcre2_code *code = cache_lookup(p, lp);
  if (code != NULL) {
    *mdata = pcre2_match_data_create_from_pattern(code, NULL);
    if (*mdata == NULL) luaL_error(L, "not enough memory");
    return code;
  }
  /* 缓存未命中，先将 Lua 模式转换为 PCRE2 模式 */
  size_t pcre2_len;
  char *pcre2_pat = lua_pattern_to_pcre2(p, lp, &pcre2_len);
  if (!pcre2_pat) luaL_error(L, "not enough memory");

  int errcode;
  PCRE2_SIZE erroffset;
  code = pcre2_compile((PCRE2_SPTR)pcre2_pat, pcre2_len, 0, &errcode, &erroffset, NULL);
  if (code == NULL) {
    PCRE2_UCHAR errbuf[256];
    pcre2_get_error_message(errcode, errbuf, sizeof(errbuf));
    luaL_error(L, "pattern compilation error (position %d): %s\n  original pattern: %s\n  converted: %s",
               (int)erroffset + 1, (const char *)errbuf, p, pcre2_pat);
  }
  free(pcre2_pat);

  *mdata = pcre2_match_data_create_from_pattern(code, NULL);
  if (*mdata == NULL) {
    pcre2_code_free(code);
    luaL_error(L, "not enough memory");
  }
  /* 仅在 jit.regex.on() 后才启用 PCRE2 JIT */
  if (XCLUA_REGEX_JIT_ENABLED) {
    pcre2_jit_compile(code, PCRE2_JIT_COMPLETE);
  }
  /* 加入缓存（用原始 Lua 模式做 key） */
  cache_insert(p, lp, code);
  return code;
}

/* 释放匹配数据。code 由缓存管理，不在此释放 */
static void pcre2_free_pattern (pcre2_code *code, pcre2_match_data *mdata) {
  if (mdata) pcre2_match_data_free(mdata);
  (void)code;
}

/* 检查模式是否包含特殊字符 */
static int pcre2_nospecials (const char *p, size_t l) {
  size_t upto = 0;
  do {
    if (strpbrk(p + upto, SPECIALS))
      return 0;
    upto += strlen(p + upto) + 1;
  } while (upto <= l);
  return 1;
}

/* 获取第 i 个捕获组信息 */
static ptrdiff_t pcre2_get_onecapture (MatchState *ms, int i, const char *s,
                                  const char *e, const char **cap) {
  if (i == 0) {
    *cap = s;
    return (e - s);
  }
  else if (i < (int)ms->ovec_pairs) {
    PCRE2_SIZE start = ms->ovector[i * 2];
    PCRE2_SIZE end = ms->ovector[i * 2 + 1];
    #ifdef LXCLUA_PCRE2_DEBUG
    fprintf(stderr, "[DEBUG pcre2_get_onecapture] i=%d ovec_pairs=%u start=%d end=%d PCRE2_UNSET=%d\n",
            i, ms->ovec_pairs, (int)start, (int)end, (int)PCRE2_UNSET);
#endif
    if (start == PCRE2_UNSET) {
      *cap = NULL;
      return CAP_POSITION;
    }
    *cap = ms->src_init + start;
    return (end - start);
  }
  else {
    luaL_error(ms->L, "invalid capture index %%%d", i);
    return 0;
  }
}

/* 将第 i 个捕获组压入栈 */
static void pcre2_push_onecapture (MatchState *ms, int i, const char *s,
                              const char *e) {
  const char *cap;
  ptrdiff_t l = pcre2_get_onecapture(ms, i, s, e, &cap);
  if (l == CAP_POSITION)
    lua_pushnil(ms->L);
  else
    lua_pushlstring(ms->L, cap, (size_t)l);
}

/* 将所有捕获组压入栈（有捕获组时跳过全匹配） */
static int pcre2_push_captures (MatchState *ms, const char *s, const char *e) {
  int i;
  int nlevels = (int)ms->ovec_pairs;  /* 实际匹配的 pair 数 */
  if (nlevels <= 1) {
    /* 没有捕获组，返回全匹配 */
    const char *cap;
    ptrdiff_t l = pcre2_get_onecapture(ms, 0, s, e, &cap);
    lua_pushlstring(ms->L, cap, (size_t)l);
    return 1;
  }
  /* 有捕获组，跳过全匹配，只返回捕获组 */
  luaL_checkstack(ms->L, nlevels - 1, "too many captures");
  for (i = 1; i < nlevels; i++) {
    pcre2_push_onecapture(ms, i, s, e);
  }
  return nlevels - 1;
}

/* 执行 PCRE2 匹配 */
static int pcre2_do_match (MatchState *ms, const char *s) {
  PCRE2_SIZE offset = s - ms->src_init;
  int rc = pcre2_match(ms->code, (PCRE2_SPTR)ms->src_init,
                        ms->src_end - ms->src_init,
                        offset, 0, ms->mdata, NULL);
  if (rc < 0) {
    if (rc == PCRE2_ERROR_NOMATCH)
      return 0;
    PCRE2_UCHAR errbuf[256];
    pcre2_get_error_message(rc, errbuf, sizeof(errbuf));
    luaL_error(ms->L, "pattern matching error: %s", (const char *)errbuf);
    return 0;
  }
  ms->ovector = pcre2_get_ovector_pointer(ms->mdata);
  /* pcre2_get_ovector_count 返回的是 pair 数，代码内部按元素数使用，需乘以 2 */
  ms->ovec_count = pcre2_get_ovector_count(ms->mdata) * 2;
  ms->ovec_pairs = (unsigned int)rc;  /* 实际匹配的 pair 数 */
  #ifdef LXCLUA_PCRE2_DEBUG
  fprintf(stderr, "[DEBUG pcre2_do_match] rc=%d ovec_pairs=%u ovector[0]=%d ovector[1]=%d ovector[2]=%d ovector[3]=%d\n",
          rc, ms->ovec_pairs, (int)ms->ovector[0], (int)ms->ovector[1],
          (int)ms->ovector[2], (int)ms->ovector[3]);
#endif
  return 1;
}

/* str_find_aux: 查找/匹配的统一实现 */
static int pcre2_str_find_aux (lua_State *L, int find) {
  size_t ls, lp;
  const char *s = luaL_checklstring(L, 1, &ls);
  const char *p = luaL_checklstring(L, 2, &lp);
  size_t init = posrelatI(luaL_optinteger(L, 3, 1), ls) - 1;
  if (init > ls) {
    luaL_pushfail(L);
    return 1;
  }
  if (find && (lua_toboolean(L, 4) || pcre2_nospecials(p, lp))) {
    const char *s2 = lmemfind(s + init, ls - init, p, lp);
    if (s2) {
      lua_pushinteger(L, ct_diff2S(s2 - s) + 1);
      lua_pushinteger(L, cast_st2S(ct_diff2sz(s2 - s) + lp));
      return 2;
    }
  }
  else {
    pcre2_match_data *mdata;
    pcre2_code *code = pcre2_compile_pattern(L, p, lp, &mdata);
    MatchState ms;
    ms.L = L;
    ms.src_init = s;
    ms.src_end = s + ls;
    ms.code = code;
    ms.mdata = mdata;
    ms.ovector = NULL;
    ms.ovec_count = 0;
    ms.ovec_pairs = 0;

    int anchor = (*p == '^');
    const char *s1 = s + init;
    if (anchor) s1 = s;

    int found = 0;
    if (anchor) {
      found = pcre2_do_match(&ms, s1);
    }
    else {
      while (s1 <= ms.src_end) {
        if (pcre2_do_match(&ms, s1)) { found = 1; break; }
        s1++;
      }
    }

    if (found) {
      const char *match_start = ms.src_init + ms.ovector[0];
      const char *match_end = ms.src_init + ms.ovector[1];
      if (find) {
        lua_pushinteger(L, ct_diff2S(match_start - s) + 1);
        lua_pushinteger(L, ct_diff2S(match_end - s));
        int n = pcre2_push_captures(&ms, match_start, match_end);
        pcre2_free_pattern(code, mdata);
        return n + 2;
      }
      else {
        int n = pcre2_push_captures(&ms, match_start, match_end);
        pcre2_free_pattern(code, mdata);
        return n;
      }
    }
    pcre2_free_pattern(code, mdata);
  }
  luaL_pushfail(L);
  return 1;
}

static int pcre2_str_find (lua_State *L) {
  return pcre2_str_find_aux(L, 1);
}

static int pcre2_str_match (lua_State *L) {
  return pcre2_str_find_aux(L, 0);
}

/* -- gfind -- */

static int pcre2_gfind_aux (lua_State *L) {
  size_t ls, lp;
  const char *s = lua_tolstring(L, lua_upvalueindex(1), &ls);
  const char *p = lua_tolstring(L, lua_upvalueindex(2), &lp);
  lua_Integer init = posrelat(luaL_optinteger(L, lua_upvalueindex(3), 1), ls);
  if (init < 1) init = 1;
  else if (init > (lua_Integer)ls + 1) return 0;
  if (lua_toboolean(L, lua_upvalueindex(4)) || pcre2_nospecials(p, lp)) {
    const char *s2 = lmemfind(s + init - 1, ls - (size_t)init + 1, p, lp);
    if (s2) {
      lua_pushinteger(L, (s2 - s) + 1);
      lua_pushinteger(L, (s2 - s) + lp);
      lua_pushinteger(L, (s2 - s) + lp + 1);
      lua_replace(L, lua_upvalueindex(3));
      return 2;
    }
  }
  else {
    pcre2_match_data *mdata;
    pcre2_code *code = pcre2_compile_pattern(L, p, lp, &mdata);
    MatchState ms;
    ms.L = L;
    ms.src_init = s;
    ms.src_end = s + ls;
    ms.code = code;
    ms.mdata = mdata;
    ms.ovector = NULL;
    ms.ovec_count = 0;
    ms.ovec_pairs = 0;

    int anchor = (*p == '^');
    const char *s1 = s + init - 1;
    if (anchor) s1 = s;

    if (anchor) {
      if (pcre2_do_match(&ms, s1)) {
        const char *match_start = ms.src_init + ms.ovector[0];
        const char *match_end = ms.src_init + ms.ovector[1];
        lua_pushinteger(L, (match_start - s) + 1);
        lua_pushinteger(L, (match_end - s));
        lua_pushinteger(L, ms.ovector[1] + 1);
        lua_replace(L, lua_upvalueindex(3));
        int n = pcre2_push_captures(&ms, match_start, match_end);
        pcre2_free_pattern(code, mdata);
        return n + 2;
      }
    }
    else {
      while (s1 <= ms.src_end) {
        if (pcre2_do_match(&ms, s1)) {
          const char *match_start = ms.src_init + ms.ovector[0];
          const char *match_end = ms.src_init + ms.ovector[1];
          lua_pushinteger(L, (match_start - s) + 1);
          lua_pushinteger(L, (match_end - s));
          lua_pushinteger(L, ms.ovector[1] + 1);
          lua_replace(L, lua_upvalueindex(3));
          int n = pcre2_push_captures(&ms, match_start, match_end);
          pcre2_free_pattern(code, mdata);
          return n + 2;
        }
        s1++;
      }
    }
    pcre2_free_pattern(code, mdata);
  }
  return 0;
}

static int pcre2_gfind (lua_State *L) {
  luaL_checkstring(L, 1);
  luaL_checkstring(L, 2);
  int b = lua_toboolean(L, 3);
  lua_settop(L, 2);
  lua_pushinteger(L, 0);
  lua_pushboolean(L, b);
  lua_pushcclosure(L, pcre2_gfind_aux, 4);
  return 1;
}

/* -- gmatch -- */

typedef struct GMatchState {
  const char *src;
  const char *p;
  const char *lastmatch;
  pcre2_code *code;
  pcre2_match_data *mdata;
  MatchState ms;
} GMatchState;

static int pcre2_gmatch_aux (lua_State *L) {
  GMatchState *gm = (GMatchState *)lua_touserdata(L, lua_upvalueindex(3));
  const char *src;
  gm->ms.L = L;
  for (src = gm->src; src <= gm->ms.src_end; src++) {
    if (pcre2_do_match(&gm->ms, src)) {
      const char *match_start = gm->ms.src_init + gm->ms.ovector[0];
      const char *e = gm->ms.src_init + gm->ms.ovector[1];
      PCRE2_SIZE mlen = gm->ms.ovector[1] - gm->ms.ovector[0];
      if (mlen == 0 && match_start == gm->lastmatch) {
        if (match_start < gm->ms.src_end) {
          gm->src = match_start + 1;
          gm->lastmatch = match_start + 1;
        }
        else
          gm->src = gm->ms.src_end + 1;
        return pcre2_push_captures(&gm->ms, match_start, e);
      }
      if (e != gm->lastmatch || mlen == 0) {
        gm->src = gm->lastmatch = e;
        return pcre2_push_captures(&gm->ms, match_start, e);
      }
    }
  }
  return 0;
}

static int pcre2_gmatch (lua_State *L) {
  size_t ls, lp;
  const char *s = luaL_checklstring(L, 1, &ls);
  const char *p = luaL_checklstring(L, 2, &lp);
  size_t init = posrelatI(luaL_optinteger(L, 3, 1), ls) - 1;
  GMatchState *gm;
  lua_settop(L, 2);
  gm = (GMatchState *)lua_newuserdatauv(L, sizeof(GMatchState), 0);
  if (init > ls) init = ls + 1;
  gm->code = pcre2_compile_pattern(L, p, lp, &gm->mdata);
  gm->ms.L = L;
  gm->ms.src_init = s;
  gm->ms.src_end = s + ls;
  gm->ms.code = gm->code;
  gm->ms.mdata = gm->mdata;
  gm->ms.ovector = NULL;
  gm->ms.ovec_count = 0;
  gm->ms.ovec_pairs = 0;
  gm->src = s + init;
  gm->p = p;
  gm->lastmatch = NULL;
  lua_pushcclosure(L, pcre2_gmatch_aux, 3);
  return 1;
}

/* -- gsub -- */

/* PCRE2 正则：gsub 替换字符串处理（同时支持 $ 和 % 转义） */
static void pcre2_add_s (MatchState *ms, luaL_Buffer *b, const char *s,
                    const char *e) {
  size_t l;
  lua_State *L = ms->L;
  const char *news = lua_tolstring(L, 3, &l);
  while (l > 0) {
    const char *dol = (const char *)memchr(news, '$', l);
    const char *pct = (const char *)memchr(news, L_ESC, l);
    const char *p;
    if (dol == NULL && pct == NULL) {
      luaL_addlstring(b, news, l);
      return;
    }
    if (dol == NULL) p = pct;
    else if (pct == NULL) p = dol;
    else p = (dol < pct) ? dol : pct;
    luaL_addlstring(b, news, ct_diff2sz(p - news));
    char esc = *p;
    p++;
    if (esc == '$') {
      /* $ 转义：$0/$& 全匹配，$1-$9 捕获组 */
      if (*p == '$')
        luaL_addchar(b, '$');
      else if (*p == '0' || *p == '&')
        luaL_addlstring(b, s, ct_diff2sz(e - s));
      else if (isdigit(cast_uchar(*p))) {
        const char *cap;
        ptrdiff_t resl = pcre2_get_onecapture(ms, *p - '0', s, e, &cap);
        if (resl == CAP_POSITION)
          luaL_addvalue(b);
        else if (cap)
          luaL_addlstring(b, cap, cast_sizet(resl));
        else
          luaL_addstring(b, "");
      }
      else
        luaL_error(L, "invalid use of '$' in replacement string");
    }
    else {
      /* % 转义（Lua 兼容）：%0 全匹配，%1-%9 捕获组 */
      if (*p == L_ESC)
        luaL_addchar(b, *p);
      else if (*p == '0')
        luaL_addlstring(b, s, ct_diff2sz(e - s));
      else if (isdigit(cast_uchar(*p))) {
        const char *cap;
        ptrdiff_t resl = pcre2_get_onecapture(ms, *p - '0', s, e, &cap);
        if (resl == CAP_POSITION)
          luaL_addvalue(b);
        else if (cap)
          luaL_addlstring(b, cap, cast_sizet(resl));
        else
          luaL_addstring(b, "");
      }
      else
        luaL_error(L, "invalid use of '%c' in replacement string", L_ESC);
    }
    l -= ct_diff2sz(p + 1 - news);
    news = p + 1;
  }
  luaL_addlstring(b, news, l);
}

static int pcre2_add_value (MatchState *ms, luaL_Buffer *b, const char *s,
                       const char *e, int tr) {
  lua_State *L = ms->L;
  switch (tr) {
    case LUA_TFUNCTION: {
      int n;
      lua_pushvalue(L, 3);
      n = pcre2_push_captures(ms, s, e);
      lua_call(L, n, 1);
      break;
    }
    case LUA_TTABLE: {
      pcre2_push_onecapture(ms, 0, s, e);
      lua_gettable(L, 3);
      break;
    }
    default: {
      pcre2_add_s(ms, b, s, e);
      return 1;
    }
  }
  if (!lua_toboolean(L, -1)) {
    lua_pop(L, 1);
    luaL_addlstring(b, s, ct_diff2sz(e - s));
    return 0;
  }
  else if (l_unlikely(!lua_isstring(L, -1)))
    return luaL_error(L, "invalid replacement value (a %s)", luaL_typename(L, -1));
  else {
    luaL_addvalue(b);
    return 1;
  }
}

static int pcre2_str_gsub (lua_State *L) {
  size_t srcl, lp;
  const char *src = luaL_checklstring(L, 1, &srcl);
  const char *p = luaL_checklstring(L, 2, &lp);
  const char *lastmatch = NULL;
  int tr = lua_type(L, 3);
  lua_Integer max_s = luaL_optinteger(L, 4, cast_st2S(srcl) + 1);
  int anchor = (*p == '^');
  lua_Integer n = 0;
  int changed = 0;
  luaL_Buffer b;
  luaL_argexpected(L, tr == LUA_TNUMBER || tr == LUA_TSTRING ||
                   tr == LUA_TFUNCTION || tr == LUA_TTABLE, 3,
                   "string/function/table");
  luaL_buffinit(L, &b);

  pcre2_match_data *mdata;
  pcre2_code *code = pcre2_compile_pattern(L, p, lp, &mdata);
  MatchState ms;
  ms.L = L;
  ms.src_init = src;
  ms.src_end = src + srcl;
  ms.code = code;
  ms.mdata = mdata;
  ms.ovector = NULL;
  ms.ovec_count = 0;

  while (n < max_s) {
    if (pcre2_do_match(&ms, src)) {
      const char *match_start = ms.src_init + ms.ovector[0];
      const char *e = ms.src_init + ms.ovector[1];
      PCRE2_SIZE mlen = ms.ovector[1] - ms.ovector[0];

      if (e == lastmatch) {
        if (src < ms.src_end) {
          luaL_addchar(&b, *src++);
          continue;
        }
        else break;
      }

      /* 添加匹配前的未匹配文本 */
      luaL_addlstring(&b, src, ct_diff2sz(match_start - src));

      n++;
      changed = pcre2_add_value(&ms, &b, match_start, e, tr) | changed;
      src = lastmatch = e;

      if (mlen == 0 && src < ms.src_end)
        luaL_addchar(&b, *src++);
    }
    else if (src < ms.src_end)
      luaL_addchar(&b, *src++);
    else break;
    if (anchor) break;
  }

  pcre2_free_pattern(code, mdata);

  if (!changed)
    lua_pushvalue(L, 1);
  else {
    luaL_addlstring(&b, src, ct_diff2sz(ms.src_end - src));
    luaL_pushresult(&b);
  }
  lua_pushinteger(L, n);
  return 2;
}

/* }=========================================== */

/*
** {===========================================
** 原始 Lua 正则引擎（lua_ 前缀）
** ============================================
*/

/* 原始 Lua 正则特殊字符（不同于 PCRE2 的 SPECIALS） */
#define LUA_SPECIALS "^$*+?.([%-"

/* 原始 Lua 正则内部函数 */
static int lua_check_capture (MatchState *ms, int l) {
  l -= '1';
  if (l_unlikely(l < 0 || l >= ms->level ||
                 ms->capture[l].len == CAP_UNFINISHED))
    return luaL_error(ms->L, "invalid capture index %%%d", l + 1);
  return l;
}

static int lua_capture_to_close (MatchState *ms) {
  int level = ms->level;
  for (level--; level>=0; level--)
    if (ms->capture[level].len == CAP_UNFINISHED) return level;
  return luaL_error(ms->L, "invalid pattern capture");
}

static const char *lua_classend (MatchState *ms, const char *p) {
  switch (*p++) {
    case L_ESC: {
      if (l_unlikely(p == ms->p_end))
        luaL_error(ms->L, "malformed pattern (ends with '%%')");
      return p+1;
    }
    case '[': {
      if (*p == '^') p++;
      do {
        if (l_unlikely(p == ms->p_end))
          luaL_error(ms->L, "malformed pattern (missing ']')");
        if (*(p++) == L_ESC && p < ms->p_end)
          p++;
      } while (*p != ']');
      return p+1;
    }
    default: {
      return p;
    }
  }
}

static int lua_match_class (int c, int cl) {
  int res;
  switch (tolower(cl)) {
    case 'a' : res = isalpha(c); break;
    case 'c' : res = iscntrl(c); break;
    case 'd' : res = isdigit(c); break;
    case 'g' : res = isgraph(c); break;
    case 'l' : res = islower(c); break;
    case 'p' : res = ispunct(c); break;
    case 's' : res = isspace(c); break;
    case 'u' : res = isupper(c); break;
    case 'w' : res = isalnum(c); break;
    case 'x' : res = isxdigit(c); break;
    case 'z' : res = (c == 0); break;
    case 'n' : res = (c == '\n' || c == '\r'); break;
    case 'r' : res = (c == '\r'); break;
    case 't' : res = (c == '\t'); break;
    case 'v' : res = (c == '\v' || c == '\f'); break;
    case 'o' : res = (c >= '0' && c <= '7'); break;
    case 'h' : res = (c == ' ' || c == '\t'); break;
    case 'q' : res = (isprint(c) && !isspace(c)); break;
    default: return (cl == c);
  }
  return (islower(cl) ? res : !res);
}

static int lua_matchbracketclass (int c, const char *p, const char *ec) {
  int sig = 1;
  if (*(p+1) == '^') {
    sig = 0;
    p++;
  }
  while (++p < ec) {
    if (*p == L_ESC) {
      p++;
      if (lua_match_class(c, cast_uchar(*p)))
        return sig;
    }
    else if ((*(p+1) == '-') && (p+2 < ec)) {
      p+=2;
      if (cast_uchar(*(p-2)) <= c && c <= cast_uchar(*p))
        return sig;
    }
    else if (cast_uchar(*p) == c) return sig;
  }
  return !sig;
}

static int lua_singlematch (MatchState *ms, const char *s, const char *p,
                        const char *ep) {
  if (s >= ms->src_end)
    return 0;
  else {
    int c = cast_uchar(*s);
    switch (*p) {
      case '.': return 1;
      case L_ESC: return lua_match_class(c, cast_uchar(*(p+1)));
      case '[': return lua_matchbracketclass(c, p, ep-1);
      default:  return (cast_uchar(*p) == c);
    }
  }
}

static const char *lua_matchbalance (MatchState *ms, const char *s,
                                   const char *p) {
  if (l_unlikely(p >= ms->p_end - 1))
    luaL_error(ms->L, "malformed pattern (missing arguments to '%%b')");
  if (*s != *p) return NULL;
  else {
    int b = *p;
    int e = *(p+1);
    int cont = 1;
    while (++s < ms->src_end) {
      if (*s == e) {
        if (--cont == 0) return s+1;
      }
      else if (*s == b) cont++;
    }
  }
  return NULL;
}

/* 前向声明：原始 Lua 递归匹配函数 */
static const char *lua_match (MatchState *ms, const char *s, const char *p);

static const char *lua_max_expand (MatchState *ms, const char *s,
                                 const char *p, const char *ep) {
  ptrdiff_t i = 0;
  while (lua_singlematch(ms, s + i, p, ep))
    i++;
  while (i>=0) {
    const char *res = lua_match(ms, (s+i), ep+1);
    if (res) return res;
    i--;
  }
  return NULL;
}

static const char *lua_min_expand (MatchState *ms, const char *s,
                                 const char *p, const char *ep) {
  for (;;) {
    const char *res = lua_match(ms, s, ep+1);
    if (res != NULL)
      return res;
    else if (lua_singlematch(ms, s, p, ep))
      s++;
    else return NULL;
  }
}

static const char *lua_start_capture (MatchState *ms, const char *s,
                                    const char *p, int what) {
  const char *res;
  int level = ms->level;
  if (level >= LUA_MAXCAPTURES) luaL_error(ms->L, "too many captures");
  ms->capture[level].init = s;
  ms->capture[level].len = what;
  ms->level = level+1;
  if ((res=lua_match(ms, s, p)) == NULL)
    ms->level--;
  return res;
}

static const char *lua_end_capture (MatchState *ms, const char *s,
                                  const char *p) {
  int l = lua_capture_to_close(ms);
  const char *res;
  ms->capture[l].len = s - ms->capture[l].init;
  if ((res = lua_match(ms, s, p)) == NULL)
    ms->capture[l].len = CAP_UNFINISHED;
  return res;
}

static const char *lua_match_capture (MatchState *ms, const char *s, int l) {
  size_t len;
  l = lua_check_capture(ms, l);
  len = cast_sizet(ms->capture[l].len);
  if ((size_t)(ms->src_end-s) >= len &&
      memcmp(ms->capture[l].init, s, len) == 0)
    return s+len;
  else return NULL;
}

static const char *lua_match (MatchState *ms, const char *s, const char *p) {
  if (l_unlikely(ms->matchdepth-- == 0))
    luaL_error(ms->L, "pattern too complex");
  init: /* using goto to optimize tail recursion */
  if (p != ms->p_end) {
    switch (*p) {
      case '(': {
        if (*(p + 1) == ')')
          s = lua_start_capture(ms, s, p + 2, CAP_POSITION);
        else
          s = lua_start_capture(ms, s, p + 1, CAP_UNFINISHED);
        break;
      }
      case ')': {
        s = lua_end_capture(ms, s, p + 1);
        break;
      }
      case '$': {
        if ((p + 1) != ms->p_end)
          goto lua_dflt;
        s = (s == ms->src_end) ? s : NULL;
        break;
      }
      case L_ESC: {
        switch (*(p + 1)) {
          case 'b': {
            s = lua_matchbalance(ms, s, p + 2);
            if (s != NULL) {
              p += 4; goto init;
            }
            break;
          }
          case 'f': {
            const char *ep; char previous;
            p += 2;
            if (l_unlikely(*p != '['))
              luaL_error(ms->L, "missing '[' after '%%f' in pattern");
            ep = lua_classend(ms, p);
            previous = (s == ms->src_init) ? '\0' : *(s - 1);
            if (!lua_matchbracketclass(cast_uchar(previous), p, ep - 1) &&
               lua_matchbracketclass(cast_uchar(*s), p, ep - 1)) {
              p = ep; goto init;
            }
            s = NULL;
            break;
          }
          case '0': case '1': case '2': case '3':
          case '4': case '5': case '6': case '7':
          case '8': case '9': {
            s = lua_match_capture(ms, s, cast_uchar(*(p + 1)));
            if (s != NULL) {
              p += 2; goto init;
            }
            break;
          }
          default: goto lua_dflt;
        }
        break;
      }
      default: lua_dflt: {
        const char *ep = lua_classend(ms, p);
        if (!lua_singlematch(ms, s, p, ep)) {
          if (*ep == '*' || *ep == '?' || *ep == '-') {
            p = ep + 1; goto init;
          }
          else
            s = NULL;
        }
        else {
          switch (*ep) {
            case '?': {
              const char *res;
              if ((res = lua_match(ms, s + 1, ep + 1)) != NULL)
                s = res;
              else {
                p = ep + 1; goto init;
              }
              break;
            }
            case '+':
              s++;
              /* FALLTHROUGH */
            case '*':
              s = lua_max_expand(ms, s, p, ep);
              break;
            case '-':
              s = lua_min_expand(ms, s, p, ep);
              break;
            default:
              s++; p = ep; goto init;
          }
        }
        break;
      }
    }
  }
  ms->matchdepth++;
  return s;
}

/* 原始 Lua 正则：获取第 i 个捕获组信息 */
static ptrdiff_t lua_get_onecapture (MatchState *ms, int i, const char *s,
                              const char *e, const char **cap) {
  if (i >= ms->level) {
    if (l_unlikely(i != 0))
      luaL_error(ms->L, "invalid capture index %%%d", i + 1);
    *cap = s;
    return (e - s);
  }
  else {
    ptrdiff_t capl = ms->capture[i].len;
    *cap = ms->capture[i].init;
    if (l_unlikely(capl == CAP_UNFINISHED))
      luaL_error(ms->L, "unfinished capture");
    else if (capl == CAP_POSITION)
      lua_pushinteger(ms->L,
          ct_diff2S(ms->capture[i].init - ms->src_init) + 1);
    return capl;
  }
}

/* 原始 Lua 正则：将第 i 个捕获组压入栈 */
static void lua_push_onecapture (MatchState *ms, int i, const char *s,
                                                    const char *e) {
  const char *cap;
  ptrdiff_t l = lua_get_onecapture(ms, i, s, e, &cap);
  if (l != CAP_POSITION)
    lua_pushlstring(ms->L, cap, cast_sizet(l));
}

/* 原始 Lua 正则：将所有捕获组压入栈 */
static int lua_push_captures (MatchState *ms, const char *s, const char *e) {
  int i;
  int nlevels = (ms->level == 0 && s) ? 1 : ms->level;
  luaL_checkstack(ms->L, nlevels, "too many captures");
  for (i = 0; i < nlevels; i++)
    lua_push_onecapture(ms, i, s, e);
  return nlevels;
}

/* 原始 Lua 正则：检查模式是否不含特殊字符 */
static int lua_nospecials (const char *p, size_t l) {
  size_t upto = 0;
  do {
    if (strpbrk(p + upto, LUA_SPECIALS))
      return 0;
    upto += strlen(p + upto) + 1;
  } while (upto <= l);
  return 1;
}

/* 原始 Lua 正则：初始化匹配状态 */
static void lua_prepstate (MatchState *ms, lua_State *L,
                       const char *s, size_t ls, const char *p, size_t lp) {
  ms->L = L;
  ms->matchdepth = MAXCCALLS;
  ms->src_init = s;
  ms->src_end = s + ls;
  ms->p_end = p + lp;
}

/* 原始 Lua 正则：重置匹配状态 */
static void lua_reprepstate (MatchState *ms) {
  ms->level = 0;
  lua_assert(ms->matchdepth == MAXCCALLS);
}

/* 原始 Lua 正则：str_find_aux 实现 */
static int lua_str_find_aux (lua_State *L, int find) {
  size_t ls, lp;
  const char *s = luaL_checklstring(L, 1, &ls);
  const char *p = luaL_checklstring(L, 2, &lp);
  size_t init = posrelatI(luaL_optinteger(L, 3, 1), ls) - 1;
  if (init > ls) {
    luaL_pushfail(L);
    return 1;
  }
  if (find && (lua_toboolean(L, 4) || lua_nospecials(p, lp))) {
    const char *s2 = lmemfind(s + init, ls - init, p, lp);
    if (s2) {
      lua_pushinteger(L, ct_diff2S(s2 - s) + 1);
      lua_pushinteger(L, cast_st2S(ct_diff2sz(s2 - s) + lp));
      return 2;
    }
  }
  else {
    MatchState ms;
    const char *s1 = s + init;
    int anchor = (*p == '^');
    if (anchor) {
      p++; lp--;
    }
    lua_prepstate(&ms, L, s, ls, p, lp);
    do {
      const char *res;
      lua_reprepstate(&ms);
      if ((res=lua_match(&ms, s1, p)) != NULL) {
        if (find) {
          lua_pushinteger(L, ct_diff2S(s1 - s) + 1);
          lua_pushinteger(L, ct_diff2S(res - s));
          return lua_push_captures(&ms, NULL, 0) + 2;
        }
        else
          return lua_push_captures(&ms, s1, res);
      }
    } while (s1++ < ms.src_end && !anchor);
  }
  luaL_pushfail(L);
  return 1;
}

static int lua_str_find (lua_State *L) {
  return lua_str_find_aux(L, 1);
}

static int lua_str_match (lua_State *L) {
  return lua_str_find_aux(L, 0);
}

/* 原始 Lua 正则：gfind_aux */
static int lua_gfind_aux (lua_State *L) {
    size_t ls, lp;
    const char *s = lua_tolstring(L, lua_upvalueindex(1), &ls);
    const char *p = lua_tolstring(L, lua_upvalueindex(2), &lp);
    lua_Integer init = posrelat(luaL_optinteger(L, lua_upvalueindex(3), 1), ls);
    if (init < 1) init = 1;
    else if (init > (lua_Integer)ls + 1) {
        return 0;
    }
    if (lua_toboolean(L, lua_upvalueindex(4)) || lua_nospecials(p, lp)) {
        const char *s2 = lmemfind(s + init - 1, ls - (size_t)init + 1, p, lp);
        if (s2) {
            lua_pushinteger(L, (s2 - s) + 1);
            lua_pushinteger(L, (s2 - s) + lp);
            lua_pushinteger(L,(s2 - s) + lp + 1);
            lua_replace(L, lua_upvalueindex(3));
            return 2;
        }
    }
    else {
        MatchState ms;
        const char *s1 = s + init - 1;
        int anchor = (*p == '^');
        if (anchor) {
            p++; lp--;
        }
        ms.L = L;
        ms.matchdepth = MAXCCALLS;
        ms.src_init = s;
        ms.src_end = s + ls;
        ms.p_end = p + lp;
        do {
            const char *res;
            ms.level = 0;
            lua_assert(ms.matchdepth == MAXCCALLS);
            if ((res=lua_match(&ms, s1, p)) != NULL) {
                lua_pushinteger(L, (s1 - s) + 1);
                lua_pushinteger(L, res - s);
                lua_pushinteger(L, res - s + 1);
                lua_replace(L, lua_upvalueindex(3));
                return lua_push_captures(&ms, NULL, 0) + 2;
            }
        } while (s1++ < ms.src_end && !anchor);
    }
    return 0;
}

static int lua_gfind (lua_State *L) {
    luaL_checkstring(L, 1);
    luaL_checkstring(L, 2);
    int b = lua_toboolean(L, 3);
    lua_settop(L, 2);
    lua_pushinteger(L, 0);
    lua_pushboolean(L, b);
    lua_pushcclosure(L, lua_gfind_aux, 4);
    return 1;
}

/* 原始 Lua 正则：gmatch_aux */
typedef struct LuaGMatchState {
  const char *src;
  const char *p;
  const char *lastmatch;
  MatchState ms;
} LuaGMatchState;

static int lua_gmatch_aux (lua_State *L) {
  LuaGMatchState *gm = (LuaGMatchState *)lua_touserdata(L, lua_upvalueindex(3));
  const char *src;
  gm->ms.L = L;
  for (src = gm->src; src <= gm->ms.src_end; src++) {
    const char *e;
    lua_reprepstate(&gm->ms);
    if ((e = lua_match(&gm->ms, src, gm->p)) != NULL && e != gm->lastmatch) {
      gm->src = gm->lastmatch = e;
      return lua_push_captures(&gm->ms, src, e);
    }
  }
  return 0;
}

static int lua_gmatch (lua_State *L) {
  size_t ls, lp;
  const char *s = luaL_checklstring(L, 1, &ls);
  const char *p = luaL_checklstring(L, 2, &lp);
  size_t init = posrelatI(luaL_optinteger(L, 3, 1), ls) - 1;
  LuaGMatchState *gm;
  lua_settop(L, 2);
  gm = (LuaGMatchState *)lua_newuserdatauv(L, sizeof(LuaGMatchState), 0);
  if (init > ls)
    init = ls + 1;
  lua_prepstate(&gm->ms, L, s, ls, p, lp);
  gm->src = s + init; gm->p = p; gm->lastmatch = NULL;
  lua_pushcclosure(L, lua_gmatch_aux, 3);
  return 1;
}

/* 原始 Lua 正则：gsub 辅助函数（同时支持 % 和 $ 转义） */
static void lua_add_s (MatchState *ms, luaL_Buffer *b, const char *s,
                                                   const char *e) {
  size_t l;
  lua_State *L = ms->L;
  const char *news = lua_tolstring(L, 3, &l);
  while (l > 0) {
    const char *pct = (const char *)memchr(news, L_ESC, l);
    const char *dol = (const char *)memchr(news, '$', l);
    const char *p;
    if (pct == NULL && dol == NULL) {
      luaL_addlstring(b, news, l);
      break;
    }
    if (pct == NULL) p = dol;
    else if (dol == NULL) p = pct;
    else p = (pct < dol) ? pct : dol;
    luaL_addlstring(b, news, ct_diff2sz(p - news));
    char esc = *p;
    p++;
    if (esc == L_ESC) {
      /* % 转义：%0 全匹配，%1-%9 捕获组 */
      if (*p == L_ESC)
        luaL_addchar(b, *p);
      else if (*p == '0')
        luaL_addlstring(b, s, ct_diff2sz(e - s));
      else if (isdigit(cast_uchar(*p))) {
        const char *cap;
        ptrdiff_t resl = lua_get_onecapture(ms, *p - '1', s, e, &cap);
        if (resl == CAP_POSITION)
          luaL_addvalue(b);
        else
          luaL_addlstring(b, cap, cast_sizet(resl));
      }
      else
        luaL_error(L, "invalid use of '%c' in replacement string", L_ESC);
    }
    else {
      /* $ 转义（PCRE2 兼容）：$0/$& 全匹配，$1-$9 捕获组 */
      if (*p == '$')
        luaL_addchar(b, '$');
      else if (*p == '0' || *p == '&')
        luaL_addlstring(b, s, ct_diff2sz(e - s));
      else if (isdigit(cast_uchar(*p))) {
        const char *cap;
        ptrdiff_t resl = lua_get_onecapture(ms, *p - '1', s, e, &cap);
        if (resl == CAP_POSITION)
          luaL_addvalue(b);
        else
          luaL_addlstring(b, cap, cast_sizet(resl));
      }
      else {
        /* $ 后不是有效转义字符，输出字面 $，p 回退以正确计数 */
        luaL_addlstring(b, "$", 1);
        p--;
      }
    }
    l -= ct_diff2sz(p + 1 - news);
    news = p + 1;
  }
}

static int lua_add_value (MatchState *ms, luaL_Buffer *b, const char *s,
                                      const char *e, int tr) {
  lua_State *L = ms->L;
  switch (tr) {
    case LUA_TFUNCTION: {
      int n;
      lua_pushvalue(L, 3);
      n = lua_push_captures(ms, s, e);
      lua_call(L, n, 1);
      break;
    }
    case LUA_TTABLE: {
      lua_push_onecapture(ms, 0, s, e);
      lua_gettable(L, 3);
      break;
    }
    default: {
      lua_add_s(ms, b, s, e);
      return 1;
    }
  }
  if (!lua_toboolean(L, -1)) {
    lua_pop(L, 1);
    luaL_addlstring(b, s, ct_diff2sz(e - s));
    return 0;
  }
  else if (l_unlikely(!lua_isstring(L, -1)))
    return luaL_error(L, "invalid replacement value (a %s)",
                         luaL_typename(L, -1));
  else {
    luaL_addvalue(b);
    return 1;
  }
}

/* 原始 Lua 正则：gsub */
static int lua_str_gsub (lua_State *L) {
  size_t srcl, lp;
  const char *src = luaL_checklstring(L, 1, &srcl);
  const char *p = luaL_checklstring(L, 2, &lp);
  const char *lastmatch = NULL;
  int tr = lua_type(L, 3);
  lua_Integer max_s = luaL_optinteger(L, 4, cast_st2S(srcl) + 1);
  int anchor = (*p == '^');
  lua_Integer n = 0;
  int changed = 0;
  MatchState ms;
  luaL_Buffer b;
  luaL_argexpected(L, tr == LUA_TNUMBER || tr == LUA_TSTRING ||
                   tr == LUA_TFUNCTION || tr == LUA_TTABLE, 3,
                      "string/function/table");
  luaL_buffinit(L, &b);
  if (anchor) {
    p++; lp--;
  }
  lua_prepstate(&ms, L, src, srcl, p, lp);
  while (n < max_s) {
    const char *e;
    lua_reprepstate(&ms);
    if ((e = lua_match(&ms, src, p)) != NULL && e != lastmatch) {
      n++;
      changed = lua_add_value(&ms, &b, src, e, tr) | changed;
      src = lastmatch = e;
    }
    else if (src < ms.src_end)
      luaL_addchar(&b, *src++);
    else break;
    if (anchor) break;
  }
  if (!changed)
    lua_pushvalue(L, 1);
  else {
    luaL_addlstring(&b, src, ct_diff2sz(ms.src_end - src));
    luaL_pushresult(&b);
  }
  lua_pushinteger(L, n);
  return 2;
}

/* }=========================================== */

/*
** {===========================================
** 双引擎调度包装函数
** 根据 XCLUA_PCRE2_ENABLED 选择 PCRE2 或原始 Lua 引擎
** ============================================
*/

static int str_find (lua_State *L) {
  return XCLUA_PCRE2_ENABLED ? pcre2_str_find(L) : lua_str_find(L);
}

static int str_match (lua_State *L) {
  return XCLUA_PCRE2_ENABLED ? pcre2_str_match(L) : lua_str_match(L);
}

static int gfind (lua_State *L) {
  return XCLUA_PCRE2_ENABLED ? pcre2_gfind(L) : lua_gfind(L);
}

static int gmatch (lua_State *L) {
  return XCLUA_PCRE2_ENABLED ? pcre2_gmatch(L) : lua_gmatch(L);
}

static int str_gsub (lua_State *L) {
  return XCLUA_PCRE2_ENABLED ? pcre2_str_gsub(L) : lua_str_gsub(L);
}

/* }=========================================== */

/* 纯文本查找（被 str_split / str_contains 等复用） */
static const char *lmemfind (const char *s1, size_t l1,
                               const char *s2, size_t l2) {
  if (l2 == 0) return s1;
  else if (l2 > l1) return NULL;
  else {
    const char *init;
    l2--;
    l1 = l1-l2;
    while (l1 > 0 && (init = (const char *)memchr(s1, *s2, l1)) != NULL) {
      init++;
      if (memcmp(init, s2+1, l2) == 0)
        return init-1;
      else {
        l1 -= ct_diff2sz(init - s1);
        s1 = init;
      }
    }
    return NULL;
  }
}

/*
** {===========================================
** UTILS AND EXTENDED FUNCTIONS
** ============================================
*/

static int str_split (lua_State *L) {
  size_t l, sep_l;
  const char *s = luaL_checklstring(L, 1, &l);
  const char *sep = luaL_optlstring(L, 2, "", &sep_l);
  const char *e = s + l;
  int i = 1;
  
  lua_newtable(L);
  
  if (sep_l == 0) {  /* empty separator: return characters */
    while (s < e) {
      lua_pushlstring(L, s++, 1);
      lua_rawseti(L, -2, i++);
    }
  }
  else {
    while (s < e) {
      const char *p = lmemfind(s, e - s, sep, sep_l);
      if (p == NULL) {
        lua_pushlstring(L, s, e - s);
        lua_rawseti(L, -2, i++);
        break;
      }
      lua_pushlstring(L, s, p - s);
      lua_rawseti(L, -2, i++);
      s = p + sep_l;
    }
    if (s == e && l > 0) { /* trailing empty string if separator was at the end */
        /* Logic check: if string is "a,b," and sep is ",", we want {"a", "b", ""} */
        /* lmemfind returns pointer to start of sep. */
        /* loop breaks when s >= e. */
        /* If the last char was sep, then s will equal e after s = p + sep_l. */
        /* But we need to check if the loop condition covers this. */
        /* Actually, if s == e, it means we finished exactly at end. */
        /* If original string ended with sep, we pushed the part before sep. */
        /* Example: "a," split by ",". */
        /* 1. lmemfind finds "," at offset 1. p points to ",". */
        /* 2. push "a". i=2. s becomes "," + 1 = end. */
        /* 3. loop terminates. */
        /* We need to push empty string? Python's split("a,") -> ['a', '']. */
        /* Lua's common implementations usually do this. */
        
        /* Let's refine the loop logic. */
    }
  }
  
  /* Re-implementation for correct behavior matching common split */
  if (sep_l > 0) {
      /* Reset stack and table */
      lua_pop(L, 1); 
      lua_newtable(L);
      i = 1;
      s = lua_tostring(L, 1); /* reset s */
      
      while (s < e) {
        const char *p = lmemfind(s, e - s, sep, sep_l);
        if (p == NULL) {
          lua_pushlstring(L, s, e - s);
          lua_rawseti(L, -2, i++);
          s = e; /* done */
        } else {
          lua_pushlstring(L, s, p - s);
          lua_rawseti(L, -2, i++);
          s = p + sep_l;
        }
      }
      /* If the string ended with separator, push empty string */
      /* Check original string length to avoid pushing "" for empty string input if desired, 
         but "split" usually returns {""} for "" input with any separator. */
      if (l == 0) {
          lua_pushliteral(L, "");
          lua_rawseti(L, -2, 1);
      }
      else if (s == e) { 
          /* If s reached e exactly after a separator, it means last part is empty. */
          /* We need to know if the last operation was a separator skip. */
          /* Compare s with previous pointer? */
          /* Easier way: check if original string ends with separator */
          if (l >= sep_l && memcmp(e - sep_l, sep, sep_l) == 0) {
              lua_pushliteral(L, "");
              lua_rawseti(L, -2, i);
          }
      }
  }
  
  return 1;
}

static int str_trim (lua_State *L) {
  size_t l;
  const char *s = luaL_checklstring(L, 1, &l);
  while (l > 0 && isspace(uchar(*s))) {
    s++; l--;
  }
  while (l > 0 && isspace(uchar(s[l - 1]))) {
    l--;
  }
  lua_pushlstring(L, s, l);
  return 1;
}

static int str_ltrim (lua_State *L) {
  size_t l;
  const char *s = luaL_checklstring(L, 1, &l);
  while (l > 0 && isspace(uchar(*s))) {
    s++; l--;
  }
  lua_pushlstring(L, s, l);
  return 1;
}

static int str_rtrim (lua_State *L) {
  size_t l;
  const char *s = luaL_checklstring(L, 1, &l);
  while (l > 0 && isspace(uchar(s[l - 1]))) {
    l--;
  }
  lua_pushlstring(L, s, l);
  return 1;
}

static int str_startswith (lua_State *L) {
  size_t ls, lp;
  const char *s = luaL_checklstring(L, 1, &ls);
  const char *p = luaL_checklstring(L, 2, &lp);
  if (lp > ls)
    lua_pushboolean(L, 0);
  else
    lua_pushboolean(L, memcmp(s, p, lp) == 0);
  return 1;
}

static int str_endswith (lua_State *L) {
  size_t ls, lp;
  const char *s = luaL_checklstring(L, 1, &ls);
  const char *p = luaL_checklstring(L, 2, &lp);
  if (lp > ls)
    lua_pushboolean(L, 0);
  else
    lua_pushboolean(L, memcmp(s + ls - lp, p, lp) == 0);
  return 1;
}

static int str_contains (lua_State *L) {
  size_t ls, lp;
  const char *s = luaL_checklstring(L, 1, &ls);
  const char *p = luaL_checklstring(L, 2, &lp);
  lua_pushboolean(L, lmemfind(s, ls, p, lp) != NULL);
  return 1;
}

static int str_hex (lua_State *L) {
  size_t l, i;
  const char *s = luaL_checklstring(L, 1, &l);
  luaL_Buffer b;
  char *h = luaL_buffinitsize(L, &b, l * 2);
  for (i = 0; i < l; i++) {
    sprintf(h + i * 2, "%02x", uchar(s[i]));
  }
  luaL_pushresultsize(&b, l * 2);
  return 1;
}

static int str_fromhex (lua_State *L) {
  size_t l, i;
  const char *s = luaL_checklstring(L, 1, &l);
  luaL_Buffer b;
  if (l % 2 != 0) return luaL_error(L, "invalid hex string length");
  char *p = luaL_buffinitsize(L, &b, l / 2);
  for (i = 0; i < l; i += 2) {
    unsigned int c;
    if (sscanf(s + i, "%02x", &c) != 1)
      return luaL_error(L, "invalid hex string");
    p[i / 2] = cast_char(c);
  }
  luaL_pushresultsize(&b, l / 2);
  return 1;
}

static int str_escape (lua_State *L) {
  size_t l;
  const char *s = luaL_checklstring(L, 1, &l);
  luaL_Buffer b;
  luaL_buffinit(L, &b);
  for (size_t i = 0; i < l; i++) {
    if (strchr(SPECIALS, s[i]))
      luaL_addchar(&b, '%');
    luaL_addchar(&b, s[i]);
  }
  luaL_pushresult(&b);
  return 1;
}

/*
** Cryptographic and Hashing Extensions
*/

/*
** AES Encrypt (CBC mode)
** Args: key (string), data (string), [iv (string)]
** Returns: encrypted_data (string)
*/
static int str_aes_encrypt(lua_State *L) {
  size_t key_len, data_len, iv_len;
  const char *key = luaL_checklstring(L, 1, &key_len);
  const char *data = luaL_checklstring(L, 2, &data_len);
  const char *iv = luaL_optlstring(L, 3, NULL, &iv_len);
  
  if (key_len != AES_KEYLEN) {
    return luaL_error(L, "Key length must be %d bytes", AES_KEYLEN);
  }
  
  /* Prepare IV */
  uint8_t iv_buf[AES_BLOCKLEN];
  if (iv) {
    if (iv_len != AES_BLOCKLEN) {
      return luaL_error(L, "IV length must be %d bytes", AES_BLOCKLEN);
    }
    memcpy(iv_buf, iv, AES_BLOCKLEN);
  } else {
    memset(iv_buf, 0, AES_BLOCKLEN); /* Default zero IV */
  }
  
  /* Calculate padded length (PKCS#7 padding is standard, but here we just pad to block size) */
  /* For simplicity in this C extension, let's enforce input to be multiple of block size or handle padding manually. */
  /* AES library usually expects buffer length to be multiple of 16. */
  
  size_t padded_len = (data_len + AES_BLOCKLEN - 1) / AES_BLOCKLEN * AES_BLOCKLEN;
  if (padded_len == 0) padded_len = AES_BLOCKLEN;
  if (data_len % AES_BLOCKLEN != 0 || data_len == 0) {
      padded_len = (data_len / AES_BLOCKLEN + 1) * AES_BLOCKLEN;
  }
  
  /* Allocate buffer for encrypted data (including padding space) */
  /* Note: AES_CBC_encrypt_buffer modifies buffer in place */
  unsigned char *buf = (unsigned char *)malloc(padded_len);
  if (!buf) return luaL_error(L, "Memory allocation failed");
  
  memset(buf, 0, padded_len);
  memcpy(buf, data, data_len);
  
  /* Init AES Context */
  struct AES_ctx ctx;
  AES_init_ctx_iv(&ctx, (const uint8_t*)key, iv_buf);
  
  /* Encrypt */
  AES_CBC_encrypt_buffer(&ctx, buf, (uint32_t)padded_len);
  
  lua_pushlstring(L, (const char*)buf, padded_len);
  free(buf);
  return 1;
}

/*
** AES Decrypt (CBC mode)
** Args: key (string), data (string), [iv (string)]
** Returns: decrypted_data (string)
*/
static int str_aes_decrypt(lua_State *L) {
  size_t key_len, data_len, iv_len;
  const char *key = luaL_checklstring(L, 1, &key_len);
  const char *data = luaL_checklstring(L, 2, &data_len);
  const char *iv = luaL_optlstring(L, 3, NULL, &iv_len);
  
  if (key_len != AES_KEYLEN) {
    return luaL_error(L, "Key length must be %d bytes", AES_KEYLEN);
  }
  if (data_len % AES_BLOCKLEN != 0) {
    return luaL_error(L, "Data length must be multiple of %d bytes", AES_BLOCKLEN);
  }
  
  /* Prepare IV */
  uint8_t iv_buf[AES_BLOCKLEN];
  if (iv) {
    if (iv_len != AES_BLOCKLEN) {
      return luaL_error(L, "IV length must be %d bytes", AES_BLOCKLEN);
    }
    memcpy(iv_buf, iv, AES_BLOCKLEN);
  } else {
    memset(iv_buf, 0, AES_BLOCKLEN);
  }
  
  unsigned char *buf = (unsigned char *)malloc(data_len);
  if (!buf) return luaL_error(L, "Memory allocation failed");
  
  memcpy(buf, data, data_len);
  
  /* Init AES Context */
  struct AES_ctx ctx;
  AES_init_ctx_iv(&ctx, (const uint8_t*)key, iv_buf);
  
  /* Decrypt */
  AES_CBC_decrypt_buffer(&ctx, buf, (uint32_t)data_len);
  
  lua_pushlstring(L, (const char*)buf, data_len);
  free(buf);
  return 1;
}

/*
** CRC32
** Args: data (string)
** Returns: crc (integer)
*/
static int str_crc32(lua_State *L) {
  size_t len;
  const char *data = luaL_checklstring(L, 1, &len);
  unsigned int crc = naga_crc32((unsigned char*)data, (unsigned int)len);
  lua_pushinteger(L, crc);
  return 1;
}

/*
** SHA256
** Args: data (string)
** Returns: hash (hex string)
*/
static int str_sha256(lua_State *L) {
  size_t len;
  const char *data = luaL_checklstring(L, 1, &len);
  uint8_t digest[SHA256_DIGEST_SIZE];
  
  SHA256((const uint8_t*)data, len, digest);
  
  char hex_digest[SHA256_DIGEST_SIZE * 2 + 1];
  for (int i = 0; i < SHA256_DIGEST_SIZE; i++) {
    sprintf(hex_digest + i * 2, "%02x", digest[i]);
  }
  lua_pushstring(L, hex_digest);
  return 1;
}

/* }=========================================== */



/*
** {===========================================
** STRING FORMAT
** ============================================
*/

#if !defined(lua_number2strx)	/* { */

/*
** Hexadecimal floating-point formatter
*/

#define SIZELENMOD	(sizeof(LUA_NUMBER_FRMLEN)/sizeof(char))


/*
** Number of bits that goes into the first digit. It can be any value
** between 1 and 4; the following definition tries to align the number
** to nibble boundaries by making what is left after that first digit a
** multiple of 4.
*/
#define L_NBFD		((l_floatatt(MANT_DIG) - 1)%4 + 1)


/*
** Add integer part of 'x' to buffer and return new 'x'
*/
static lua_Number adddigit (char *buff, unsigned n, lua_Number x) {
  lua_Number dd = l_mathop(floor)(x);  /* get integer part from 'x' */
  int d = (int)dd;
  buff[n] = cast_char(d < 10 ? d + '0' : d - 10 + 'a');  /* add to buffer */
  return x - dd;  /* return what is left */
}


static int num2straux (char *buff, unsigned sz, lua_Number x) {
  /* if 'inf' or 'NaN', format it like '%g' */
  if (x != x || x == (lua_Number)HUGE_VAL || x == -(lua_Number)HUGE_VAL)
    return l_sprintf(buff, sz, LUA_NUMBER_FMT, (LUAI_UACNUMBER)x);
  else if (x == 0) {  /* can be -0... */
    /* create "0" or "-0" followed by exponent */
    return l_sprintf(buff, sz, LUA_NUMBER_FMT "x0p+0", (LUAI_UACNUMBER)x);
  }
  else {
    int e;
    lua_Number m = l_mathop(frexp)(x, &e);  /* 'x' fraction and exponent */
    unsigned n = 0;  /* character count */
    if (m < 0) {  /* is number negative? */
      buff[n++] = '-';  /* add sign */
      m = -m;  /* make it positive */
    }
    buff[n++] = '0'; buff[n++] = 'x';  /* add "0x" */
    m = adddigit(buff, n++, m * (1 << L_NBFD));  /* add first digit */
    e -= L_NBFD;  /* this digit goes before the radix point */
    if (m > 0) {  /* more digits? */
      buff[n++] = lua_getlocaledecpoint();  /* add radix point */
      do {  /* add as many digits as needed */
        m = adddigit(buff, n++, m * 16);
      } while (m > 0);
    }
    n += cast_uint(l_sprintf(buff + n, sz - n, "p%+d", e));  /* add exponent */
    lua_assert(n < sz);
    return cast_int(n);
  }
}


static int lua_number2strx (lua_State *L, char *buff, unsigned sz,
                            const char *fmt, lua_Number x) {
  int n = num2straux(buff, sz, x);
  if (fmt[SIZELENMOD] == 'A') {
    int i;
    for (i = 0; i < n; i++)
      buff[i] = cast_char(toupper(cast_uchar(buff[i])));
  }
  else if (l_unlikely(fmt[SIZELENMOD] != 'a'))
    return luaL_error(L, "modifiers for format '%%a'/'%%A' not implemented");
  return n;
}

#endif				/* } */


/*
** Maximum size for items formatted with '%f'. This size is produced
** by format('%.99f', -maxfloat), and is equal to 99 + 3 ('-', '.',
** and '\0') + number of decimal digits to represent maxfloat (which
** is maximum exponent + 1). (99+3+1, adding some extra, 110)
*/
#define MAX_ITEMF	(110 + l_floatatt(MAX_10_EXP))


/*
** All formats except '%f' do not need that large limit.  The other
** float formats use exponents, so that they fit in the 99 limit for
** significant digits; 's' for large strings and 'q' add items directly
** to the buffer; all integer formats also fit in the 99 limit.  The
** worst case are floats: they may need 99 significant digits, plus
** '0x', '-', '.', 'e+XXXX', and '\0'. Adding some extra, 120.
*/
#define MAX_ITEM	120


/* valid flags in a format specification */
#if !defined(L_FMTFLAGSF)

/* valid flags for a, A, e, E, f, F, g, and G conversions */
#define L_FMTFLAGSF	"-+#0 "

/* valid flags for o, x, and X conversions */
#define L_FMTFLAGSX	"-#0"

/* valid flags for d and i conversions */
#define L_FMTFLAGSI	"-+0 "

/* valid flags for u conversions */
#define L_FMTFLAGSU	"-0"

/* valid flags for c, p, and s conversions */
#define L_FMTFLAGSC	"-"

#endif


/*
** Maximum size of each format specification (such as "%-099.99d"):
** Initial '%', flags (up to 5), width (2), period, precision (2),
** length modifier (8), conversion specifier, and final '\0', plus some
** extra.
*/
#define MAX_FORMAT	32


static void addquoted (luaL_Buffer *b, const char *s, size_t len) {
  luaL_addchar(b, '"');
  while (len--) {
    if (*s == '"' || *s == '\\' || *s == '\n') {
      luaL_addchar(b, '\\');
      luaL_addchar(b, *s);
    }
    else if (iscntrl(cast_uchar(*s))) {
      char buff[10];
      if (!isdigit(cast_uchar(*(s+1))))
        l_sprintf(buff, sizeof(buff), "\\%d", (int)cast_uchar(*s));
      else
        l_sprintf(buff, sizeof(buff), "\\%03d", (int)cast_uchar(*s));
      luaL_addstring(b, buff);
    }
    else
      luaL_addchar(b, *s);
    s++;
  }
  luaL_addchar(b, '"');
}


/*
** Serialize a floating-point number in such a way that it can be
** scanned back by Lua. Use hexadecimal format for "common" numbers
** (to preserve precision); inf, -inf, and NaN are handled separately.
** (NaN cannot be expressed as a numeral, so we write '(0/0)' for it.)
*/
static int quotefloat (lua_State *L, char *buff, lua_Number n) {
  const char *s;  /* for the fixed representations */
  if (n == (lua_Number)HUGE_VAL)  /* inf? */
    s = "1e9999";
  else if (n == -(lua_Number)HUGE_VAL)  /* -inf? */
    s = "-1e9999";
  else if (n != n)  /* NaN? */
    s = "(0/0)";
  else {  /* format number as hexadecimal */
    int  nb = lua_number2strx(L, buff, MAX_ITEM,
                                 "%" LUA_NUMBER_FRMLEN "a", n);
    /* ensures that 'buff' string uses a dot as the radix character */
    if (memchr(buff, '.', cast_uint(nb)) == NULL) {  /* no dot? */
      char point = lua_getlocaledecpoint();  /* try locale point */
      char *ppoint = (char *)memchr(buff, point, cast_uint(nb));
      if (ppoint) *ppoint = '.';  /* change it to a dot */
    }
    return nb;
  }
  /* for the fixed representations */
  return l_sprintf(buff, MAX_ITEM, "%s", s);
}


static void addliteral (lua_State *L, luaL_Buffer *b, int arg) {
  switch (lua_type(L, arg)) {
    case LUA_TSTRING: {
      size_t len;
      const char *s = lua_tolstring(L, arg, &len);
      addquoted(b, s, len);
      break;
    }
    case LUA_TNUMBER: {
      char *buff = luaL_prepbuffsize(b, MAX_ITEM);
      int nb;
      if (!lua_isinteger(L, arg))  /* float? */
        nb = quotefloat(L, buff, lua_tonumber(L, arg));
      else {  /* integers */
        lua_Integer n = lua_tointeger(L, arg);
        const char *format = (n == LUA_MININTEGER)  /* corner case? */
                           ? "0x%" LUA_INTEGER_FRMLEN "x"  /* use hex */
                           : LUA_INTEGER_FMT;  /* else use default format */
        nb = l_sprintf(buff, MAX_ITEM, format, (LUAI_UACINT)n);
      }
      luaL_addsize(b, cast_uint(nb));
      break;
    }
    case LUA_TNIL: case LUA_TBOOLEAN: {
      luaL_tolstring(L, arg, NULL);
      luaL_addvalue(b);
      break;
    }
    default: {
      luaL_argerror(L, arg, "value has no literal form");
    }
  }
}


static const char *get2digits (const char *s) {
  if (isdigit(cast_uchar(*s))) {
    s++;
    if (isdigit(cast_uchar(*s))) s++;  /* (2 digits at most) */
  }
  return s;
}


/*
** Check whether a conversion specification is valid. When called,
** first character in 'form' must be '%' and last character must
** be a valid conversion specifier. 'flags' are the accepted flags;
** 'precision' signals whether to accept a precision.
*/
static void checkformat (lua_State *L, const char *form, const char *flags,
                                       int precision) {
  const char *spec = form + 1;  /* skip '%' */
  spec += strspn(spec, flags);  /* skip flags */
  if (*spec != '0') {  /* a width cannot start with '0' */
    spec = get2digits(spec);  /* skip width */
    if (*spec == '.' && precision) {
      spec++;
      spec = get2digits(spec);  /* skip precision */
    }
  }
  if (!isalpha(cast_uchar(*spec)))  /* did not go to the end? */
    luaL_error(L, "invalid conversion specification: '%s'", form);
}


/*
** Get a conversion specification and copy it to 'form'.
** Return the address of its last character.
*/
static const char *getformat (lua_State *L, const char *strfrmt,
                                            char *form) {
  /* spans flags, width, and precision ('0' is included as a flag) */
  size_t len = strspn(strfrmt, L_FMTFLAGSF "123456789.");
  len++;  /* adds following character (should be the specifier) */
  /* still needs space for '%', '\0', plus a length modifier */
  if (len >= MAX_FORMAT - 10)
    luaL_error(L, "invalid format (too long)");
  *(form++) = '%';
  memcpy(form, strfrmt, len * sizeof(char));
  *(form + len) = '\0';
  return strfrmt + len - 1;
}


/*
** add length modifier into formats
*/
static void addlenmod (char *form, const char *lenmod) {
  size_t l = strlen(form);
  size_t lm = strlen(lenmod);
  char spec = form[l - 1];
  strcpy(form + l - 1, lenmod);
  form[l + lm - 1] = spec;
  form[l + lm] = '\0';
}


static int str_format (lua_State *L) {
  int top = lua_gettop(L);
  int arg = 1;
  size_t sfl;
  const char *strfrmt = luaL_checklstring(L, arg, &sfl);
  const char *strfrmt_end = strfrmt+sfl;
  const char *flags;
  luaL_Buffer b;
  luaL_buffinit(L, &b);
  while (strfrmt < strfrmt_end) {
    if (*strfrmt != L_ESC)
      luaL_addchar(&b, *strfrmt++);
    else if (*++strfrmt == L_ESC)
      luaL_addchar(&b, *strfrmt++);  /* %% */
    else { /* format item */
      char form[MAX_FORMAT];  /* to store the format ('%...') */
      unsigned maxitem = MAX_ITEM;  /* maximum length for the result */
      char *buff = luaL_prepbuffsize(&b, maxitem);  /* to put result */
      int nb = 0;  /* number of bytes in result */
      if (++arg > top)
        return luaL_argerror(L, arg, "no value");
      strfrmt = getformat(L, strfrmt, form);
      switch (*strfrmt++) {
        case 'c': {
          checkformat(L, form, L_FMTFLAGSC, 0);
          nb = l_sprintf(buff, maxitem, form, (int)luaL_checkinteger(L, arg));
          break;
        }
        case 'd': case 'i':
          flags = L_FMTFLAGSI;
          goto intcase;
        case 'u':
          flags = L_FMTFLAGSU;
          goto intcase;
        case 'o': case 'x': case 'X':
          flags = L_FMTFLAGSX;
         intcase: {
          lua_Integer n = luaL_checkinteger(L, arg);
          checkformat(L, form, flags, 1);
          addlenmod(form, LUA_INTEGER_FRMLEN);
          nb = l_sprintf(buff, maxitem, form, (LUAI_UACINT)n);
          break;
        }
        case 'a': case 'A':
          checkformat(L, form, L_FMTFLAGSF, 1);
          addlenmod(form, LUA_NUMBER_FRMLEN);
          nb = lua_number2strx(L, buff, maxitem, form,
                                  luaL_checknumber(L, arg));
          break;
        case 'f':
          maxitem = MAX_ITEMF;  /* extra space for '%f' */
          buff = luaL_prepbuffsize(&b, maxitem);
          /* FALLTHROUGH */
        case 'e': case 'E': case 'g': case 'G': {
          lua_Number n = luaL_checknumber(L, arg);
          checkformat(L, form, L_FMTFLAGSF, 1);
          addlenmod(form, LUA_NUMBER_FRMLEN);
          nb = l_sprintf(buff, maxitem, form, (LUAI_UACNUMBER)n);
          break;
        }
        case 'p': {
          const void *p = lua_topointer(L, arg);
          checkformat(L, form, L_FMTFLAGSC, 0);
          if (p == NULL) {  /* avoid calling 'printf' with argument NULL */
            p = "(null)";  /* result */
            form[strlen(form) - 1] = 's';  /* format it as a string */
          }
          nb = l_sprintf(buff, maxitem, form, p);
          break;
        }
        case 'q': {
          if (form[2] != '\0')  /* modifiers? */
            return luaL_error(L, "no modifiers allowed for '%%q' specifier");
          addliteral(L, &b, arg);
          break;
        }
        case 's': {
          size_t l;
          const char *s = luaL_tolstring(L, arg, &l);
          if (form[2] == '\0')  /* no modifiers? */
            luaL_addvalue(&b);  /* keep entire string */
          else {
            luaL_argcheck(L, l == strlen(s), arg, "string contains zeros");
            checkformat(L, form, L_FMTFLAGSC, 1);
            if (strchr(form, '.') == NULL && l >= 100) {
              /* no precision and string is too long to be formatted */
              luaL_addvalue(&b);  /* keep entire string */
            }
            else {  /* format the string into 'buff' */
              nb = l_sprintf(buff, maxitem, form, s);
              lua_pop(L, 1);  /* remove result from 'luaL_tolstring' */
            }
          }
          break;
        }
        default: {  /* also treat cases 'pnLlh' */
          return luaL_error(L, "invalid conversion '%s' to 'format'", form);
        }
      }
      lua_assert(cast_uint(nb) < maxitem);
      luaL_addsize(&b, cast_uint(nb));
    }
  }
  luaL_pushresult(&b);
  return 1;
}

/* }=========================================== */


/*
** {===========================================
** PACK/UNPACK
** ============================================
*/


/* value used for padding */
#if !defined(LUAL_PACKPADBYTE)
#define LUAL_PACKPADBYTE		0x00
#endif

/* maximum size for the binary representation of an integer */
#define MAXINTSIZE	16

/* number of bits in a character */
#define NB	CHAR_BIT

/* mask for one character (NB 1's) */
#define MC	((1 << NB) - 1)

/* size of a lua_Integer */
#define SZINT	((int)sizeof(lua_Integer))


/* dummy union to get native endianness */
static const union {
  int dummy;
  char little;  /* true iff machine is little endian */
} nativeendian = {1};


/*
** information to pack/unpack stuff
*/
typedef struct Header {
  lua_State *L;
  int islittle;
  unsigned maxalign;
} Header;


/*
** options for pack/unpack
*/
typedef enum KOption {
  Kint,		/* signed integers */
  Kuint,	/* unsigned integers */
  Kfloat,	/* single-precision floating-point numbers */
  Knumber,	/* Lua "native" floating-point numbers */
  Kdouble,	/* double-precision floating-point numbers */
  Kchar,	/* fixed-length strings */
  Kstring,	/* strings with prefixed length */
  Kzstr,	/* zero-terminated strings */
  Kpadding,	/* padding */
  Kpaddalign,	/* padding for alignment */
  Knop		/* no-op (configuration or spaces) */
} KOption;


/*
** Read an integer numeral from string 'fmt' or return 'df' if
** there is no numeral
*/
static int digit (int c) { return '0' <= c && c <= '9'; }

static size_t getnum (const char **fmt, size_t df) {
  if (!digit(**fmt))  /* no number? */
    return df;  /* return default value */
  else {
    size_t a = 0;
    do {
      a = a*10 + cast_uint(*((*fmt)++) - '0');
    } while (digit(**fmt) && a <= (MAX_SIZE - 9)/10);
    return a;
  }
}


/*
** Read an integer numeral and raises an error if it is larger
** than the maximum size of integers.
*/
static unsigned getnumlimit (Header *h, const char **fmt, size_t df) {
  size_t sz = getnum(fmt, df);
  if (l_unlikely((sz - 1u) >= MAXINTSIZE))
    return cast_uint(luaL_error(h->L,
               "integral size (%d) out of limits [1,%d]", sz, MAXINTSIZE));
  return cast_uint(sz);
}


/*
** Initialize Header
*/
static void initheader (lua_State *L, Header *h) {
  h->L = L;
  h->islittle = nativeendian.little;
  h->maxalign = 1;
}


/*
** Read and classify next option. 'size' is filled with option's size.
*/
static KOption getoption (Header *h, const char **fmt, size_t *size) {
  /* dummy structure to get native alignment requirements */
  struct cD { char c; union { LUAI_MAXALIGN; } u; };
  int opt = *((*fmt)++);
  *size = 0;  /* default */
  switch (opt) {
    case 'b': *size = sizeof(char); return Kint;
    case 'B': *size = sizeof(char); return Kuint;
    case 'h': *size = sizeof(short); return Kint;
    case 'H': *size = sizeof(short); return Kuint;
    case 'l': *size = sizeof(long); return Kint;
    case 'L': *size = sizeof(long); return Kuint;
    case 'j': *size = sizeof(lua_Integer); return Kint;
    case 'J': *size = sizeof(lua_Integer); return Kuint;
    case 'T': *size = sizeof(size_t); return Kuint;
    case 'f': *size = sizeof(float); return Kfloat;
    case 'n': *size = sizeof(lua_Number); return Knumber;
    case 'd': *size = sizeof(double); return Kdouble;
    case 'i': *size = getnumlimit(h, fmt, sizeof(int)); return Kint;
    case 'I': *size = getnumlimit(h, fmt, sizeof(int)); return Kuint;
    case 's': *size = getnumlimit(h, fmt, sizeof(size_t)); return Kstring;
    case 'c':
      *size = getnum(fmt, cast_sizet(-1));
      if (l_unlikely(*size == cast_sizet(-1)))
        luaL_error(h->L, "missing size for format option 'c'");
      return Kchar;
    case 'z': return Kzstr;
    case 'x': *size = 1; return Kpadding;
    case 'X': return Kpaddalign;
    case ' ': break;
    case '<': h->islittle = 1; break;
    case '>': h->islittle = 0; break;
    case '=': h->islittle = nativeendian.little; break;
    case '!': {
      const size_t maxalign = offsetof(struct cD, u);
      h->maxalign = getnumlimit(h, fmt, maxalign);
      break;
    }
    default: luaL_error(h->L, "invalid format option '%c'", opt);
  }
  return Knop;
}


/*
** Read, classify, and fill other details about the next option.
** 'psize' is filled with option's size, 'notoalign' with its
** alignment requirements.
** Local variable 'size' gets the size to be aligned. (Kpadal option
** always gets its full alignment, other options are limited by
** the maximum alignment ('maxalign'). Kchar option needs no alignment
** despite its size.
*/
static KOption getdetails (Header *h, size_t totalsize, const char **fmt,
                           size_t *psize, unsigned *ntoalign) {
  KOption opt = getoption(h, fmt, psize);
  size_t align = *psize;  /* usually, alignment follows size */
  if (opt == Kpaddalign) {  /* 'X' gets alignment from following option */
    if (**fmt == '\0' || getoption(h, fmt, &align) == Kchar || align == 0)
      luaL_argerror(h->L, 1, "invalid next option for option 'X'");
  }
  if (align <= 1 || opt == Kchar)  /* need no alignment? */
    *ntoalign = 0;
  else {
    if (align > h->maxalign)  /* enforce maximum alignment */
      align = h->maxalign;
    if (l_unlikely(!ispow2(align))) {  /* not a power of 2? */
      *ntoalign = 0;  /* to avoid warnings */
      luaL_argerror(h->L, 1, "format asks for alignment not power of 2");
    }
    else {
      /* 'szmoda' = totalsize % align */
      unsigned szmoda = cast_uint(totalsize & (align - 1));
      *ntoalign = cast_uint((align - szmoda) & (align - 1));
    }
  }
  return opt;
}


/*
** Pack integer 'n' with 'size' bytes and 'islittle' endianness.
** The final 'if' handles the case when 'size' is larger than
** the size of a Lua integer, correcting the extra sign-extension
** bytes if necessary (by default they would be zeros).
*/
static void packint (luaL_Buffer *b, lua_Unsigned n,
                     int islittle, unsigned size, int neg) {
  char *buff = luaL_prepbuffsize(b, size);
  unsigned i;
  buff[islittle ? 0 : size - 1] = (char)(n & MC);  /* first byte */
  for (i = 1; i < size; i++) {
    n >>= NB;
    buff[islittle ? i : size - 1 - i] = (char)(n & MC);
  }
  if (neg && size > SZINT) {  /* negative number need sign extension? */
    for (i = SZINT; i < size; i++)  /* correct extra bytes */
      buff[islittle ? i : size - 1 - i] = (char)MC;
  }
  luaL_addsize(b, size);  /* add result to buffer */
}


/*
** Copy 'size' bytes from 'src' to 'dest', correcting endianness if
** given 'islittle' is different from native endianness.
*/
static void copywithendian (char *dest, const char *src,
                            unsigned size, int islittle) {
  if (islittle == nativeendian.little)
    memcpy(dest, src, size);
  else {
    dest += size - 1;
    while (size-- != 0)
      *(dest--) = *(src++);
  }
}


static int str_pack (lua_State *L) {
  luaL_Buffer b;
  Header h;
  const char *fmt = luaL_checkstring(L, 1);  /* format string */
  int arg = 1;  /* current argument to pack */
  size_t totalsize = 0;  /* accumulate total size of result */
  initheader(L, &h);
  lua_pushnil(L);  /* mark to separate arguments from string buffer */
  luaL_buffinit(L, &b);
  while (*fmt != '\0') {
    unsigned ntoalign;
    size_t size;
    KOption opt = getdetails(&h, totalsize, &fmt, &size, &ntoalign);
    luaL_argcheck(L, size + ntoalign <= MAX_SIZE - totalsize, arg,
                     "result too long");
    totalsize += ntoalign + size;
    while (ntoalign-- > 0)
     luaL_addchar(&b, LUAL_PACKPADBYTE);  /* fill alignment */
    arg++;
    switch (opt) {
      case Kint: {  /* signed integers */
        lua_Integer n = luaL_checkinteger(L, arg);
        if (size < SZINT) {  /* need overflow check? */
          lua_Integer lim = (lua_Integer)1 << ((size * NB) - 1);
          luaL_argcheck(L, -lim <= n && n < lim, arg, "integer overflow");
        }
        packint(&b, (lua_Unsigned)n, h.islittle, cast_uint(size), (n < 0));
        break;
      }
      case Kuint: {  /* unsigned integers */
        lua_Integer n = luaL_checkinteger(L, arg);
        if (size < SZINT)  /* need overflow check? */
          luaL_argcheck(L, (lua_Unsigned)n < ((lua_Unsigned)1 << (size * NB)),
                           arg, "unsigned overflow");
        packint(&b, (lua_Unsigned)n, h.islittle, cast_uint(size), 0);
        break;
      }
      case Kfloat: {  /* C float */
        float f = (float)luaL_checknumber(L, arg);  /* get argument */
        char *buff = luaL_prepbuffsize(&b, sizeof(f));
        /* move 'f' to final result, correcting endianness if needed */
        copywithendian(buff, (char *)&f, sizeof(f), h.islittle);
        luaL_addsize(&b, size);
        break;
      }
      case Knumber: {  /* Lua float */
        lua_Number f = luaL_checknumber(L, arg);  /* get argument */
        char *buff = luaL_prepbuffsize(&b, sizeof(f));
        /* move 'f' to final result, correcting endianness if needed */
        copywithendian(buff, (char *)&f, sizeof(f), h.islittle);
        luaL_addsize(&b, size);
        break;
      }
      case Kdouble: {  /* C double */
        double f = (double)luaL_checknumber(L, arg);  /* get argument */
        char *buff = luaL_prepbuffsize(&b, sizeof(f));
        /* move 'f' to final result, correcting endianness if needed */
        copywithendian(buff, (char *)&f, sizeof(f), h.islittle);
        luaL_addsize(&b, size);
        break;
      }
      case Kchar: {  /* fixed-size string */
        size_t len;
        const char *s = luaL_checklstring(L, arg, &len);
        luaL_argcheck(L, len <= size, arg, "string longer than given size");
        luaL_addlstring(&b, s, len);  /* add string */
        if (len < size) {  /* does it need padding? */
          size_t psize = size - len;  /* pad size */
          char *buff = luaL_prepbuffsize(&b, psize);
          memset(buff, LUAL_PACKPADBYTE, psize);
          luaL_addsize(&b, psize);
        }
        break;
      }
      case Kstring: {  /* strings with length count */
        size_t len;
        const char *s = luaL_checklstring(L, arg, &len);
        luaL_argcheck(L, size >= sizeof(lua_Unsigned) ||
                         len < ((lua_Unsigned)1 << (size * NB)),
                         arg, "string length does not fit in given size");
        /* pack length */
        packint(&b, (lua_Unsigned)len, h.islittle, cast_uint(size), 0);
        luaL_addlstring(&b, s, len);
        totalsize += len;
        break;
      }
      case Kzstr: {  /* zero-terminated string */
        size_t len;
        const char *s = luaL_checklstring(L, arg, &len);
        luaL_argcheck(L, strlen(s) == len, arg, "string contains zeros");
        luaL_addlstring(&b, s, len);
        luaL_addchar(&b, '\0');  /* add zero at the end */
        totalsize += len + 1;
        break;
      }
      case Kpadding: luaL_addchar(&b, LUAL_PACKPADBYTE);  /* FALLTHROUGH */
      case Kpaddalign: case Knop:
        arg--;  /* undo increment */
        break;
    }
  }
  luaL_pushresult(&b);
  return 1;
}


static int str_packsize (lua_State *L) {
  Header h;
  const char *fmt = luaL_checkstring(L, 1);  /* format string */
  size_t totalsize = 0;  /* accumulate total size of result */
  initheader(L, &h);
  while (*fmt != '\0') {
    unsigned ntoalign;
    size_t size;
    KOption opt = getdetails(&h, totalsize, &fmt, &size, &ntoalign);
    luaL_argcheck(L, opt != Kstring && opt != Kzstr, 1,
                     "variable-length format");
    size += ntoalign;  /* total space used by option */
    luaL_argcheck(L, totalsize <= LUA_MAXINTEGER - size,
                     1, "format result too large");
    totalsize += size;
  }
  lua_pushinteger(L, cast_st2S(totalsize));
  return 1;
}


/*
** Unpack an integer with 'size' bytes and 'islittle' endianness.
** If size is smaller than the size of a Lua integer and integer
** is signed, must do sign extension (propagating the sign to the
** higher bits); if size is larger than the size of a Lua integer,
** it must check the unread bytes to see whether they do not cause an
** overflow.
*/
static lua_Integer unpackint (lua_State *L, const char *str,
                              int islittle, int size, int issigned) {
  lua_Unsigned res = 0;
  int i;
  int limit = (size  <= SZINT) ? size : SZINT;
  for (i = limit - 1; i >= 0; i--) {
    res <<= NB;
    res |= (lua_Unsigned)(unsigned char)str[islittle ? i : size - 1 - i];
  }
  if (size < SZINT) {  /* real size smaller than lua_Integer? */
    if (issigned) {  /* needs sign extension? */
      lua_Unsigned mask = (lua_Unsigned)1 << (size*NB - 1);
      res = ((res ^ mask) - mask);  /* do sign extension */
    }
  }
  else if (size > SZINT) {  /* must check unread bytes */
    int mask = (!issigned || (lua_Integer)res >= 0) ? 0 : MC;
    for (i = limit; i < size; i++) {
      if (l_unlikely((unsigned char)str[islittle ? i : size - 1 - i] != mask))
        luaL_error(L, "%d-byte integer does not fit into a Lua integer", size);
    }
  }
  return (lua_Integer)res;
}


static int str_unpack (lua_State *L) {
  Header h;
  const char *fmt = luaL_checkstring(L, 1);
  size_t ld;
  const char *data = luaL_checklstring(L, 2, &ld);
  size_t pos = posrelatI(luaL_optinteger(L, 3, 1), ld) - 1;
  int n = 0;  /* number of results */
  luaL_argcheck(L, pos <= ld, 3, "initial position out of string");
  initheader(L, &h);
  while (*fmt != '\0') {
    unsigned ntoalign;
    size_t size;
    KOption opt = getdetails(&h, pos, &fmt, &size, &ntoalign);
    luaL_argcheck(L, ntoalign + size <= ld - pos, 2,
                    "data string too short");
    pos += ntoalign;  /* skip alignment */
    /* stack space for item + next position */
    luaL_checkstack(L, 2, "too many results");
    n++;
    switch (opt) {
      case Kint:
      case Kuint: {
        lua_Integer res = unpackint(L, data + pos, h.islittle,
                                       cast_int(size), (opt == Kint));
        lua_pushinteger(L, res);
        break;
      }
      case Kfloat: {
        float f;
        copywithendian((char *)&f, data + pos, sizeof(f), h.islittle);
        lua_pushnumber(L, (lua_Number)f);
        break;
      }
      case Knumber: {
        lua_Number f;
        copywithendian((char *)&f, data + pos, sizeof(f), h.islittle);
        lua_pushnumber(L, f);
        break;
      }
      case Kdouble: {
        double f;
        copywithendian((char *)&f, data + pos, sizeof(f), h.islittle);
        lua_pushnumber(L, (lua_Number)f);
        break;
      }
      case Kchar: {
        lua_pushlstring(L, data + pos, size);
        break;
      }
      case Kstring: {
        lua_Unsigned len = (lua_Unsigned)unpackint(L, data + pos,
                                          h.islittle, cast_int(size), 0);
        luaL_argcheck(L, len <= ld - pos - size, 2, "data string too short");
        lua_pushlstring(L, data + pos + size, cast_sizet(len));
        pos += cast_sizet(len);  /* skip string */
        break;
      }
      case Kzstr: {
        size_t len = strlen(data + pos);
        luaL_argcheck(L, pos + len < ld, 2,
                         "unfinished string for format 'z'");
        lua_pushlstring(L, data + pos, len);
        pos += len + 1;  /* skip string plus final '\0' */
        break;
      }
      case Kpaddalign: case Kpadding: case Knop:
        n--;  /* undo increment */
        break;
    }
    pos += size;
  }
  lua_pushinteger(L, cast_st2S(pos) + 1);  /* next position */
  return n + 1;
}

/* }=========================================== */

/**
 * 读取文件内容
 * @param L Lua状态机
 * @return 返回文件内容（字符串）或错误信息
 * 参数:
 *   arg1: 文件路径 (string)
 */
static int str_file (lua_State *L) {
  const char *file_path = luaL_checkstring(L, 1);
  
  FILE *fp = fopen(file_path, "rb");
  if (!fp) {
    return luaL_error(L, "cannot open file: %s", file_path);
  }
  
  fseek(fp, 0, SEEK_END);
  long file_size = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  
  if (file_size < 0) {
    fclose(fp);
    return luaL_error(L, "cannot get file size");
  }
  
  unsigned char *file_data = (unsigned char *)malloc(file_size);
  if (!file_data) {
    fclose(fp);
    return luaL_error(L, "memory allocation failed");
  }
  
  size_t read_size = fread(file_data, 1, file_size, fp);
  fclose(fp);
  
  if (read_size != (size_t)file_size) {
    free(file_data);
    return luaL_error(L, "failed to read file");
  }
  
  lua_pushlstring(L, (const char *)file_data, file_size);
  free(file_data);
  return 1;
}


static const luaL_Reg strlib[] = {
  {"aes_decrypt", str_aes_decrypt},
  {"aes_encrypt", str_aes_encrypt},
  {"byte", str_byte},
  {"char", str_char},
  {"contains", str_contains},
  {"crc32", str_crc32},
  {"dump", str_dump},
  {"endswith", str_endswith},
  {"envelop", str_envelop},
  {"escape", str_escape},
  {"file", str_file},
  {"find", str_find},
  {"format", str_format},
  {"fromhex", str_fromhex},
  {"gfind", gfind},
  {"gmatch", gmatch},
  {"gsub", str_gsub},
  {"hex", str_hex},
  {"len", str_len},
  {"lower", str_lower},
  {"ltrim", str_ltrim},
  {"match", str_match},
  {"pack", str_pack},
  {"packsize", str_packsize},
  {"rep", str_rep},
  {"reverse", str_reverse},
  {"rtrim", str_rtrim},
  {"sha256", str_sha256},
  {"split", str_split},
  {"startswith", str_startswith},
  {"sub", str_sub},
  {"trim", str_trim},
  {"unpack", str_unpack},
  {"upper", str_upper},
  {NULL, NULL}
};


static void createmetatable (lua_State *L) {
  /* table to be metatable for strings */
  luaL_newlibtable(L, stringmetamethods);
  luaL_setfuncs(L, stringmetamethods, 0);
  lua_pushliteral(L, "");  /* dummy string */
  lua_pushvalue(L, -2);  /* copy table */
  lua_setmetatable(L, -2);  /* set table as metatable for strings */
  lua_pop(L, 1);  /* pop dummy string */
  lua_pushvalue(L, -2);  /* get string library */
  lua_setfield(L, -2, "__index");  /* metatable.__index = string */
  lua_pop(L, 1);  /* pop metatable */
}


/*
** Open string library
*/
LUAMOD_API int luaopen_string (lua_State *L) {
  luaL_newlib(L, strlib);
  createmetatable(L);
  return 1;
}

