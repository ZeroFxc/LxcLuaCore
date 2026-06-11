/**
 * @file lecc.c
 * @brief LXCLUA ECC 椭圆曲线加密库 - Lua 绑定 (secp256k1)
 *
 * 模块结构:
 *   ecc.key.generate()                             -> {private_key, public_key}
 *   ecc.key.from_private(priv_hex)                 -> {private_key, public_key}
 *   ecc.sign(data, private_key)                    -> {r, s}
 *   ecc.verify(data, signature, public_key)        -> boolean
 *   ecc.ecdh(private_key, peer_public_key)         -> shared_secret_hex
 *   ecc.recover(data, signature, recovery_id)      -> public_key_hex | nil
 *   ecc.encode.public(pubkey, compressed)          -> hex_string
 *   ecc.encode.private(privkey)                    -> hex_string
 *   ecc.decode.public(pubkey_hex)                  -> public_key_table
 *   ecc.decode.private(privkey_hex)                -> private_key
 *
 * 曲线: secp256k1 (Bitcoin/Ethereum 标准)
 * 256 位无符号整数使用 8 个 uint32_t 表示 (little-endian limb order)
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
#include <stdint.h>

/* ========================================================================
 * 常量与类型定义
 * ======================================================================== */

#define U256_LIMBS 8       /* 256 位 = 8 个 32 位 limb */
#define U512_LIMBS 16      /* 512 位中间结果 */
#define U288_LIMBS 10      /* 快速约简中间结果 (288 位安全) */
#define PUBKEY_COMPRESSED_EVEN    0x02
#define PUBKEY_COMPRESSED_ODD     0x03
#define PUBKEY_UNCOMPRESSED       0x04

/**
 * @brief 256 位无符号整数 (little-endian limb 序)
 *        d[0] 是最低 32 位, d[7] 是最高 32 位
 */
typedef struct {
  uint32_t d[U256_LIMBS];
} uint256_t;

/**
 * @brief 椭圆曲线点 (仿射坐标)
 */
typedef struct {
  uint256_t x;
  uint256_t y;
  int is_infinity;  /* 是否为无穷远点 */
} ecc_point;

/* ========================================================================
 * secp256k1 曲线参数 (以 uint32_t limb 形式定义)
 * ======================================================================== */

/* 域素数 p = 2^256 - 2^32 - 977
 * = 0xFFFFFFFF FFFFFFFF FFFFFFFF FFFFFFFF FFFFFFFF FFFFFFFF FFFFFFFE FFFFFC2F
 */
static const uint256_t SECP256K1_P = {{
  0xFFFFFC2F, 0xFFFFFFFE, 0xFFFFFFFF, 0xFFFFFFFF,
  0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF
}};

/* 曲线参数 a = 0 (secp256k1 特有, 点加倍公式简化) */
static const uint256_t SECP256K1_A __attribute__((unused)) = {{0, 0, 0, 0, 0, 0, 0, 0}};

/* 曲线参数 b = 7 */
static const uint256_t SECP256K1_B = {{7, 0, 0, 0, 0, 0, 0, 0}};

/*
 * 基点 G = (Gx, Gy):
 *   Gx = 0x79BE667EF9DCBBAC55A06295CE870B07029BFCDB2DCE28D959F2815B16F81798
 *   Gy = 0x483ADA7726A3C4655DA4FBFC0E1108A8FD17B448A68554199C47D08FFB10D4B8
 * (坐标已直接内联于下方 SECP256K1_G 定义中)
 */

/* 群的阶 n
 * = 0xFFFFFFFF FFFFFFFF FFFFFFFF FFFFFFFE BAAEDCE6 AF48A03B BFD25E8C D0364141
 */
static const uint256_t SECP256K1_N = {{
  0xD0364141, 0xBFD25E8C, 0xAF48A03B, 0xBAAEDCE6,
  0xFFFFFFFE, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF
}};

/**
 * @brief 检查 uint256_t 是否仅最低 limb 非零 (用于优化)
 * @param a 待检查的整数
 * @return 1 如果 d[1..7] 全部为零, 0 否则
 */
static int u256_is_zero_partial(const uint256_t *a) {
  for (int i = 1; i < U256_LIMBS; i++) {
    if (a->d[i] != 0) return 0;
  }
  return 1;
}

/* 基点 G (预定义) */
static const ecc_point SECP256K1_G = {
  {{0x16F81798, 0x59F2815B, 0x2DCE28D9, 0x029BFCDB,
    0xCE870B07, 0x55A06295, 0xF9DCBBAC, 0x79BE667E}},
  {{0xFB10D4B8, 0x9C47D08F, 0xA6855419, 0xFD17B448,
    0x0E1108A8, 0x5DA4FBFC, 0x26A3C465, 0x483ADA77}},
  0  /* not infinity */
};

/* 零值 */
static const uint256_t U256_ZERO = {{0, 0, 0, 0, 0, 0, 0, 0}};

/* ========================================================================
 * 全局 CSPRNG 状态 (惰性初始化)
 * ======================================================================== */

static CSPRNG_State g_ecc_csprng;
static int g_ecc_csprng_inited = 0;

/**
 * @brief 获取全局 CSPRNG 状态，首次调用时惰性初始化
 * @return 指向全局 CSPRNG 状态的指针
 */
static CSPRNG_State* get_csprng(void) {
  if (!g_ecc_csprng_inited) {
    uint64_t seed = (uint64_t)time(NULL);
    csprng_init(&g_ecc_csprng, seed);
    g_ecc_csprng_inited = 1;
  }
  return &g_ecc_csprng;
}

/* ========================================================================
 * 辅助函数: 十六进制转换
 * ======================================================================== */

/** 十六进制字符查找表 */
static const char HEX_CHARS[] = "0123456789abcdef";

/**
 * @brief 将二进制数据转换为十六进制字符串 (调用者负责释放)
 * @param dst  输出缓冲区 (至少 len*2+1 字节)
 * @param src  输入二进制数据
 * @param len  输入长度 (字节)
 */
static void bin_to_hex(char *dst, const uint8_t *src, size_t len) {
  for (size_t i = 0; i < len; i++) {
    dst[i * 2]     = HEX_CHARS[src[i] >> 4];
    dst[i * 2 + 1] = HEX_CHARS[src[i] & 0x0F];
  }
  dst[len * 2] = '\0';
}

/**
 * @brief 将十六进制字符转换为 4 位值
 * @param c 十六进制字符 ('0'-'9', 'a'-'f', 'A'-'F')
 * @return 4 位值 (0-15), 无效字符返回 -1
 */
static int hex_char_to_nibble(int c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

/**
 * @brief 将十六进制字符串解码为二进制数据
 * @param hex  十六进制字符串 (必须是偶数长度)
 * @param out  输出缓冲区
 * @param out_len 期望的输出长度
 * @return 成功返回 0, 失败返回 -1
 */
static int hex_to_bin(const char *hex, uint8_t *out, size_t out_len) {
  size_t hex_len = strlen(hex);
  if (hex_len != out_len * 2) return -1;
  for (size_t i = 0; i < out_len; i++) {
    int hi = hex_char_to_nibble((int)(unsigned char)hex[i * 2]);
    int lo = hex_char_to_nibble((int)(unsigned char)hex[i * 2 + 1]);
    if (hi < 0 || lo < 0) return -1;
    out[i] = (uint8_t)((hi << 4) | lo);
  }
  return 0;
}

/**
 * @brief 将 uint256_t 以十六进制大端字符串推入 Lua 栈
 * @param L Lua 状态
 * @param a 要转换的 256 位整数
 */
static void push_uint256_hex(lua_State *L, const uint256_t *a) {
  char hex[65];
  uint8_t bytes[32];
  /* 将 limb 转为大端字节序 */
  for (int i = 0; i < 8; i++) {
    uint32_t limb = a->d[7 - i];
    bytes[i * 4 + 0] = (uint8_t)(limb >> 24);
    bytes[i * 4 + 1] = (uint8_t)(limb >> 16);
    bytes[i * 4 + 2] = (uint8_t)(limb >> 8);
    bytes[i * 4 + 3] = (uint8_t)(limb);
  }
  bin_to_hex(hex, bytes, 32);
  lua_pushstring(L, hex);
}

/**
 * @brief 将原始字节推入 Lua 栈
 * @param L    Lua 状态
 * @param a    要转换的 256 位整数
 */
static void push_uint256_raw(lua_State *L, const uint256_t *a) {
  uint8_t bytes[32];
  for (int i = 0; i < 8; i++) {
    uint32_t limb = a->d[7 - i];
    bytes[i * 4 + 0] = (uint8_t)(limb >> 24);
    bytes[i * 4 + 1] = (uint8_t)(limb >> 16);
    bytes[i * 4 + 2] = (uint8_t)(limb >> 8);
    bytes[i * 4 + 3] = (uint8_t)(limb);
  }
  lua_pushlstring(L, (const char *)bytes, 32);
}

/* ========================================================================
 * 256 位无符号整数基本运算
 * ======================================================================== */

/**
 * @brief 复制 256 位整数: r = a
 * @param r 目标
 * @param a 源
 */
static void u256_copy(uint256_t *r, const uint256_t *a) {
  memcpy(r->d, a->d, sizeof(r->d));
}

/**
 * @brief 判断 256 位整数是否为零
 * @param a 待判断的整数
 * @return 1 如果为零, 0 否则
 */
static int u256_is_zero(const uint256_t *a) {
  for (int i = 0; i < U256_LIMBS; i++) {
    if (a->d[i] != 0) return 0;
  }
  return 1;
}

/**
 * @brief 比较两个 256 位整数
 * @param a 第一个整数
 * @param b 第二个整数
 * @return -1 如果 a < b, 0 如果 a == b, 1 如果 a > b
 */
static int u256_cmp(const uint256_t *a, const uint256_t *b) {
  for (int i = U256_LIMBS - 1; i >= 0; i--) {
    if (a->d[i] > b->d[i]) return 1;
    if (a->d[i] < b->d[i]) return -1;
  }
  return 0;
}

/**
 * @brief 判断 a >= b
 * @param a 第一个整数
 * @param b 第二个整数
 * @return 1 如果 a >= b, 0 否则
 */
static int u256_ge(const uint256_t *a, const uint256_t *b) {
  return u256_cmp(a, b) >= 0;
}

/**
 * @brief 256 位加法: r = a + b (不进位截断, 低 256 位)
 * @param r 结果
 * @param a 加数
 * @param b 加数
 * @return 进位值 (0 或 1)
 */
static uint32_t u256_add(uint256_t *r, const uint256_t *a, const uint256_t *b) {
  uint64_t carry = 0;
  for (int i = 0; i < U256_LIMBS; i++) {
    carry += (uint64_t)a->d[i] + (uint64_t)b->d[i];
    r->d[i] = (uint32_t)(carry & 0xFFFFFFFF);
    carry >>= 32;
  }
  return (uint32_t)carry;
}

/**
 * @brief 256 位减法: r = a - b (不进位截断)
 * @param r 结果
 * @param a 被减数
 * @param b 减数
 * @return 借位值 (0 或 1, 1 表示 a < b)
 */
static uint32_t u256_sub(uint256_t *r, const uint256_t *a, const uint256_t *b) {
  uint64_t borrow = 0;
  for (int i = 0; i < U256_LIMBS; i++) {
    uint64_t diff = (uint64_t)a->d[i] - (uint64_t)b->d[i] - borrow;
    r->d[i] = (uint32_t)(diff & 0xFFFFFFFF);
    borrow = (diff >> 32) & 1;
  }
  return (uint32_t)borrow;
}

/**
 * @brief 模加法: r = (a + b) mod m
 * @param r 结果
 * @param a 加数
 * @param b 加数
 * @param m 模数
 */
static void u256_add_mod(uint256_t *r, const uint256_t *a, const uint256_t *b,
                         const uint256_t *m) {
  uint256_t tmp;
  uint32_t carry = u256_add(&tmp, a, b);
  /* 如果 a + b >= m (有进位或 tmp >= m), 减去 m */
  if (carry || u256_ge(&tmp, m)) {
    u256_sub(r, &tmp, m);
  } else {
    u256_copy(r, &tmp);
  }
}

/**
 * @brief 模减法: r = (a - b) mod m
 * @param r 结果
 * @param a 被减数
 * @param b 减数
 * @param m 模数
 */
static void u256_sub_mod(uint256_t *r, const uint256_t *a, const uint256_t *b,
                         const uint256_t *m) {
  uint256_t tmp;
  uint32_t borrow = u256_sub(&tmp, a, b);
  /* 如果 a < b (有借位), 加上 m */
  if (borrow) {
    u256_add(r, &tmp, m);
  } else {
    u256_copy(r, &tmp);
  }
}

/**
 * @brief 完整 512 位乘法: r[0..15] = a[0..7] * b[0..7]
 *        使用增量进位传播, 避免 uint64_t 中间累加溢出
 *        (每个中间元素最多 8 个 (2^32-1)^2 求和,
 *         若一次性累加会超过 uint64_t 上限)
 * @param r 512 位结果 (16 个 uint32_t, little-endian)
 * @param a 乘数 (8 个 uint32_t)
 * @param b 乘数 (8 个 uint32_t)
 */
static void u256_mul_full(uint32_t r[U512_LIMBS],
                          const uint32_t a[U256_LIMBS],
                          const uint32_t b[U256_LIMBS]) {
  memset(r, 0, U512_LIMBS * sizeof(uint32_t));

  for (int i = 0; i < U256_LIMBS; i++) {
    uint64_t carry = 0;
    for (int j = 0; j < U256_LIMBS; j++) {
      /* r[i+j] 最多来自一次乘法累加, 安全范围为 [0, 2^64) */
      uint64_t prod = (uint64_t)a[i] * (uint64_t)b[j]
                    + (uint64_t)r[i + j]
                    + carry;
      r[i + j] = (uint32_t)(prod & 0xFFFFFFFF);
      carry = prod >> 32;
    }
    /* 传播进位到更高 limb */
    for (int j = i + U256_LIMBS; carry > 0 && j < U512_LIMBS; j++) {
      uint64_t sum = (uint64_t)r[j] + carry;
      r[j] = (uint32_t)(sum & 0xFFFFFFFF);
      carry = sum >> 32;
    }
  }
}

/**
 * @brief 快速 secp256k1 模约简: r = C mod p
 *        利用 p = 2^256 - 2^32 - 977
 *        C_hi = C[8..15], C_lo = C[0..7]
 *        计算 D = C_lo + C_hi * 2^32 + C_hi * 977
 *        然后循环减 p 直至 D < p
 * @param r 结果 (mod p)
 * @param c 512 位乘积 (16 个 limb)
 */
static void u256_fast_reduce_p(uint256_t *r, const uint32_t c[U512_LIMBS]) {
  /* 使用 10 个 limb 的中间结果 (足够容纳 < 2^289 的值) */
  uint32_t D[U288_LIMBS];
  uint64_t carry;

  memset(D, 0, sizeof(D));

  /* Step 1: D = C_lo (低 256 位) */
  memcpy(D, c, U256_LIMBS * sizeof(uint32_t));

  /* Step 2: D += C_hi * 2^32 (将 C_hi 各 limb 向上偏移一个位置) */
  /* C_hi[0] 放在 D[1], C_hi[1] 放在 D[2], ..., C_hi[6] 放在 D[7] */
  /* C_hi[7] 放在 D[8], 可能产生进位到 D[9] */
  carry = 0;
  for (int i = 0; i < U256_LIMBS; i++) {
    uint64_t sum = (uint64_t)D[i + 1] + (uint64_t)c[8 + i] + carry;
    D[i + 1] = (uint32_t)(sum & 0xFFFFFFFF);
    carry = sum >> 32;
  }
  /* D[9] += 最后的进位 (D[9] 是第 10 个 limb, 索引 9) */
  if (carry) {
    D[9] += (uint32_t)carry;
  }

  /* Step 3: D += C_hi * 977 */
  /* 计算 C_hi * 977: 对每个 limb 乘以 977, 加总到 D */
  {
    carry = 0;
    for (int i = 0; i < U256_LIMBS; i++) {
      uint64_t prod = (uint64_t)c[8 + i] * 977ULL;
      uint64_t sum = (uint64_t)D[i] + (prod & 0xFFFFFFFF) + carry;
      D[i] = (uint32_t)(sum & 0xFFFFFFFF);
      carry = (sum >> 32) + (prod >> 32);
    }
    /* 传播剩余的进位 */
    for (int i = U256_LIMBS; i < U288_LIMBS && carry > 0; i++) {
      uint64_t sum = (uint64_t)D[i] + carry;
      D[i] = (uint32_t)(sum & 0xFFFFFFFF);
      carry = sum >> 32;
    }
  }

  /* Step 4: 将 10-limb 结果压缩到 8-limb uint256_t, 循环减 p */
  /* 先将 D[0..7] 复制到 r */
  memcpy(r->d, D, U256_LIMBS * sizeof(uint32_t));

  /* 处理 D[8] 和 D[9] 的高位: 等价于加上 D[8]*2^256 + D[9]*2^288 */
  /* 使用 2^256 ≡ 2^32 + 977 (mod p) 递归约简 */
  /* D[8] * 2^256 ≡ D[8] * (2^32 + 977) (mod p) */
  if (D[8] != 0) {
    /* 将 D[8] 乘以 (2^32 + 977) 加到 r */
    uint64_t c2 = (uint64_t)D[8] << 32;  /* D[8] * 2^32 */
    /* D[8] * 2^32: 加到 r, 正确提取进位 (先保存 64 位和再截断) */
    {
      uint64_t sum = (uint64_t)r->d[0] + c2;
      r->d[0] = (uint32_t)(sum & 0xFFFFFFFF);
      carry = sum >> 32;
    }
    for (int i = 1; i < U256_LIMBS && carry > 0; i++) {
      carry += r->d[i];
      r->d[i] = (uint32_t)(carry & 0xFFFFFFFF);
      carry >>= 32;
    }
    /* D[8] * 977 */
    uint64_t prod8 = (uint64_t)D[8] * 977ULL;
    carry = (uint64_t)r->d[0] + prod8;
    r->d[0] = (uint32_t)(carry & 0xFFFFFFFF);
    carry >>= 32;
    for (int i = 1; i < U256_LIMBS && carry > 0; i++) {
      carry += r->d[i];
      r->d[i] = (uint32_t)(carry & 0xFFFFFFFF);
      carry >>= 32;
    }
  }

  if (D[9] != 0) {
    /* D[9] * 2^288 = D[9] * 2^256 * 2^32
     * ≡ D[9] * (2^32 + 977) * 2^32  (只应用一次 2^256 ≡ 2^32 + 977)
     * = D[9] * (2^64 + 977*2^32) (mod p)
     *
     * 分解到 limb:
     *   D[9] * 977 * 2^32  → 加到 limb 1 (977 = 0x3D1 < 2^32)
     *   D[9] * 2^64        → 加到 limb 2
     */
    uint64_t d9 = D[9];
    /* D[9] * 977 * 2^32: 贡献到 limb 1 */
    carry = (uint64_t)r->d[1] + d9 * 977ULL;
    r->d[1] = (uint32_t)(carry & 0xFFFFFFFF);
    carry >>= 32;
    /* D[9] * 2^64: 贡献到 limb 2, 加上进位 */
    carry += (uint64_t)r->d[2] + d9;
    r->d[2] = (uint32_t)(carry & 0xFFFFFFFF);
    carry >>= 32;
    /* 传播进位 */
    for (int i = 3; i < U256_LIMBS && carry > 0; i++) {
      carry += r->d[i];
      r->d[i] = (uint32_t)(carry & 0xFFFFFFFF);
      carry >>= 32;
    }
    /* 如果还有进位, 递归约简 */
    if (carry > 0) {
      uint32_t extra = (uint32_t)carry;
      uint64_t c64 = (uint64_t)extra << 32;
      uint64_t c977 = (uint64_t)extra * 977ULL;
      carry = (uint64_t)r->d[0] + c64 + c977;
      r->d[0] = (uint32_t)(carry & 0xFFFFFFFF);
      carry >>= 32;
      for (int i = 1; i < U256_LIMBS && carry > 0; i++) {
        carry += r->d[i];
        r->d[i] = (uint32_t)(carry & 0xFFFFFFFF);
        carry >>= 32;
      }
    }
  }

  /* Step 5: 循环减 p 直到 r < p (最多 2-3 次) */
  while (u256_ge(r, &SECP256K1_P)) {
    u256_sub(r, r, &SECP256K1_P);
  }
}

/**
 * @brief 模乘法 (secp256k1 域): r = (a * b) mod p
 * @param r 结果
 * @param a 乘数
 * @param b 乘数
 */
static void u256_mul_mod_p(uint256_t *r,
                           const uint256_t *a,
                           const uint256_t *b) {
  uint32_t c[U512_LIMBS];
  u256_mul_full(c, a->d, b->d);
  u256_fast_reduce_p(r, c);
}

/**
 * @brief 通用模乘法: r = (a * b) mod m, 使用长除法
 *        用于阶 n 的算术 (n 不是 Solinas 素数, 无法快速约简)
 * @param r 结果
 * @param a 乘数
 * @param b 乘数
 * @param m 模数
 */
static void u256_mul_mod(uint256_t *r,
                         const uint256_t *a,
                         const uint256_t *b,
                         const uint256_t *m) {
  uint32_t c[U512_LIMBS];
  u256_mul_full(c, a->d, b->d);

  /* 长除法: C / m, 商丢弃, 余数作为结果 */
  /* 从高位向低位逐 limb 处理 */
  /* 使用 9 个 limb 的中间值: remainder << 32 可达 ~2^288, 不丢失高位 */
  uint32_t remainder[9] = {0};
  for (int i = U512_LIMBS - 1; i >= 0; i--) {
    /* remainder = (remainder << 32) | c[i] */
    for (int j = 8; j > 0; j--) {
      remainder[j] = remainder[j - 1];
    }
    remainder[0] = c[i];

    /* 比较 9-limb remainder 与 8-limb m:
     *   remainder[8] > 0 则一定 ≥ m (因为 m 只有 8 个 limb) */
    /* 使用 while 循环而非 if: 当商数字>1 时需要多次减 m */
    while (1) {
      int cmp = 0;
      if (remainder[8] > 0) {
        cmp = 1;
      } else {
        for (int j = U256_LIMBS - 1; j >= 0; j--) {
          if (remainder[j] > m->d[j]) { cmp = 1; break; }
          if (remainder[j] < m->d[j]) { cmp = -1; break; }
        }
      }
      if (cmp < 0) break;
      /* remainder -= m (只减低 8 limb, remainder[8] 必然为 0) */
      uint64_t borrow = 0;
      for (int j = 0; j < U256_LIMBS; j++) {
        uint64_t diff = (uint64_t)remainder[j] - (uint64_t)m->d[j] - borrow;
        remainder[j] = (uint32_t)(diff & 0xFFFFFFFF);
        borrow = (diff >> 32) & 1;
      }
      /* 若 cmp=1 且 remainder[8]=0, borrow 一定为 1 (m > low 8 limbs) */
      if (borrow) remainder[8]--;
    }
  }
  memcpy(r->d, remainder, sizeof(r->d));
}

/**
 * @brief 模平方 (secp256k1 域): r = (a * a) mod p
 * @param r 结果
 * @param a 输入
 */
static void u256_sqr_mod_p(uint256_t *r, const uint256_t *a) {
  u256_mul_mod_p(r, a, a);
}

/**
 * @brief 扩展欧几里得算法求模逆: r = a^(-1) mod m
 *        要求 gcd(a, m) = 1
 * @param r 结果
 * @param a 输入
 * @param m 模数
 */
static void u256_inv_mod(uint256_t *r, const uint256_t *a, const uint256_t *m) {
  /* 使用扩展欧几里得算法 */
  uint256_t u, v, x1, x2, q, tmp, prod;

  u256_copy(&u, a);
  u256_copy(&v, m);
  /* x1 = 1, x2 = 0: 满足不变量 a*x1 ≡ u (mod m), a*x2 ≡ v (mod m) */
  memset(x1.d, 0, sizeof(x1.d));
  memset(x2.d, 0, sizeof(x2.d));
  x1.d[0] = 1;

  while (!u256_is_zero(&u)) {
    /* u == 1 时退出优化 */
    if (u.d[0] == 1 && u256_is_zero_partial(&u)) {
      u256_copy(r, &x1);
      return;
    }

    /* 使用大除法求 q = v / u, 用减法除法 */
    uint256_t u_copy;
    u256_copy(&u_copy, &u);

    /* 计算 q = v / u 使用减法 */
    /* 由于 u, v 都在 [0, m) 范围, q 通常很小 */
    memset(q.d, 0, sizeof(q.d));
    uint256_t v_copy;
    u256_copy(&v_copy, &v);

    while (u256_ge(&v_copy, &u_copy)) {
      u256_sub(&v_copy, &v_copy, &u_copy);
      /* q++ */
      uint64_t carry = 1;
      for (int i = 0; i < U256_LIMBS; i++) {
        carry += q.d[i];
        q.d[i] = (uint32_t)(carry & 0xFFFFFFFF);
        carry >>= 32;
      }
    }

    /* v, u = u, v_copy (v mod u) */
    u256_copy(&v, &u_copy);
    u256_copy(&u, &v_copy);

    /* x1, x2 = x2 - q * x1, x1 */
    u256_mul_mod(&prod, &q, &x1, m);
    u256_sub_mod(&tmp, &x2, &prod, m);
    u256_copy(&x2, &x1);
    u256_copy(&x1, &tmp);
  }

  /* a 不可逆 (不应发生, 因为 a 应该在 [1, m-1] 范围内) */
  u256_copy(r, &x2);
}

/**
 * @brief 将 256 位整数从十六进制字符串加载 (大端)
 * @param r   结果
 * @param hex 十六进制字符串 (64 字符)
 * @return 成功返回 0, 失败返回 -1
 */
static int u256_from_hex(uint256_t *r, const char *hex) {
  if (strlen(hex) != 64) return -1;
  uint8_t bytes[32];
  if (hex_to_bin(hex, bytes, 32) != 0) return -1;
  for (int i = 0; i < 8; i++) {
    r->d[7 - i] = ((uint32_t)bytes[i * 4 + 0] << 24) |
                   ((uint32_t)bytes[i * 4 + 1] << 16) |
                   ((uint32_t)bytes[i * 4 + 2] << 8) |
                   ((uint32_t)bytes[i * 4 + 3]);
  }
  return 0;
}

/**
 * @brief 将 uint256_t 转换为大端字节数组
 * @param a  输入
 * @param out 输出 (32 字节)
 */
static void u256_to_bytes(uint8_t out[32], const uint256_t *a) {
  for (int i = 0; i < 8; i++) {
    uint32_t limb = a->d[7 - i];
    out[i * 4 + 0] = (uint8_t)(limb >> 24);
    out[i * 4 + 1] = (uint8_t)(limb >> 16);
    out[i * 4 + 2] = (uint8_t)(limb >> 8);
    out[i * 4 + 3] = (uint8_t)(limb);
  }
}

/**
 * @brief 从大端字节数组加载 uint256_t
 * @param r   结果
 * @param bytes 输入 (32 字节, 大端)
 */
static void u256_from_bytes(uint256_t *r, const uint8_t bytes[32]) {
  for (int i = 0; i < 8; i++) {
    r->d[7 - i] = ((uint32_t)bytes[i * 4 + 0] << 24) |
                   ((uint32_t)bytes[i * 4 + 1] << 16) |
                   ((uint32_t)bytes[i * 4 + 2] << 8) |
                   ((uint32_t)bytes[i * 4 + 3]);
  }
}

/**
 * @brief 生成 [1, n-1] 范围内的密码学安全随机数 (用于私钥或 nonce k)
 * @param r 结果 (随机 256 位数)
 * @param csprng CSPRNG 状态
 */
static void u256_random_mod_n(uint256_t *r, CSPRNG_State *csprng) {
  uint8_t buf[32];
  do {
    csprng_bytes(csprng, buf, 32);
    u256_from_bytes(r, buf);
    /* 确保 r 在 [1, n-1] 范围内 */
  } while (u256_is_zero(r) || u256_ge(r, &SECP256K1_N));
}

/* ========================================================================
 * 椭圆曲线点运算 (secp256k1)
 * ======================================================================== */

/**
 * @brief 复制椭圆曲线点: r = a
 * @param r 目标点
 * @param a 源点
 */
static void ecp_copy(ecc_point *r, const ecc_point *a) {
  u256_copy(&r->x, &a->x);
  u256_copy(&r->y, &a->y);
  r->is_infinity = a->is_infinity;
}

/**
 * @brief 将点设为无穷远点
 * @param r 要设置的点
 */
static void ecp_set_infinity(ecc_point *r) {
  memset(&r->x, 0, sizeof(r->x));
  memset(&r->y, 0, sizeof(r->y));
  r->is_infinity = 1;
}

/**
 * @brief 椭圆曲线点加倍: R = 2 * P (Jacobian 坐标优化)
 *        使用仿射坐标的标准公式:
 *        lambda = (3*x^2 + a) / (2*y) mod p  (secp256k1 a = 0)
 *        x3 = lambda^2 - 2*x
 *        y3 = lambda*(x - x3) - y
 * @param r 结果点 (2*P)
 * @param p 输入点
 */
static void ecp_double(ecc_point *r, const ecc_point *p) {
  if (p->is_infinity) {
    ecp_set_infinity(r);
    return;
  }

  uint256_t x, y, lambda, x2, two, three, tmp, tmp2;

  u256_copy(&x, &p->x);
  u256_copy(&y, &p->y);

  /* 若 y == 0, 则 2P = O (无穷远点) */
  if (u256_is_zero(&y)) {
    ecp_set_infinity(r);
    return;
  }

  /* lambda = 3*x^2 / (2*y) mod p */
  /* 3*x^2 = x^2 + x^2 + x^2 */
  u256_sqr_mod_p(&x2, &x);       /* x2 = x^2 mod p */
  u256_add_mod(&three, &x2, &x2, &SECP256K1_P);  /* three = 2*x^2 */
  u256_add_mod(&three, &three, &x2, &SECP256K1_P); /* three = 3*x^2 */

  /* 2*y */
  u256_add_mod(&two, &y, &y, &SECP256K1_P);

  /* inv(2*y) */
  u256_inv_mod(&tmp, &two, &SECP256K1_P);

  /* lambda = three * inv(2*y) mod p */
  u256_mul_mod_p(&lambda, &three, &tmp);

  /* x3 = lambda^2 - 2*x mod p */
  u256_sqr_mod_p(&tmp, &lambda);     /* tmp = lambda^2 */
  u256_add_mod(&tmp2, &x, &x, &SECP256K1_P); /* tmp2 = 2*x */
  u256_sub_mod(&r->x, &tmp, &tmp2, &SECP256K1_P);  /* x3 = lambda^2 - 2*x */

  /* y3 = lambda * (x - x3) - y mod p */
  u256_sub_mod(&tmp, &x, &r->x, &SECP256K1_P);     /* tmp = x - x3 */
  u256_mul_mod_p(&tmp2, &lambda, &tmp);             /* tmp2 = lambda * (x - x3) */
  u256_sub_mod(&r->y, &tmp2, &y, &SECP256K1_P);     /* y3 = lambda*(x-x3) - y */
  r->is_infinity = 0;
}

/**
 * @brief 椭圆曲线点加法: R = P + Q (仿射坐标)
 *        公式:
 *        lambda = (y2 - y1) / (x2 - x1) mod p
 *        x3 = lambda^2 - x1 - x2
 *        y3 = lambda*(x1 - x3) - y1
 * @param r 结果点
 * @param p 第一个点
 * @param q 第二个点
 */
static void ecp_add(ecc_point *r, const ecc_point *p, const ecc_point *q) {
  if (p->is_infinity) {
    ecp_copy(r, q);
    return;
  }
  if (q->is_infinity) {
    ecp_copy(r, p);
    return;
  }

  /* 检查 P.x == Q.x */
  if (u256_cmp(&p->x, &q->x) == 0) {
    if (u256_cmp(&p->y, &q->y) == 0) {
      /* P == Q, 做加倍 */
      ecp_double(r, p);
    } else {
      /* P == -Q, 结果是无穷远点 */
      ecp_set_infinity(r);
    }
    return;
  }

  /* 复制输入坐标到局部变量, 防止调用方使用 r == p 时的别名问题 */
  uint256_t x1, y1, x2, y2, lambda, num, den, tmp, tmp2;
  u256_copy(&x1, &p->x);
  u256_copy(&y1, &p->y);
  u256_copy(&x2, &q->x);
  u256_copy(&y2, &q->y);

  /* lambda = (y2 - y1) / (x2 - x1) mod p */
  u256_sub_mod(&num, &y2, &y1, &SECP256K1_P);
  u256_sub_mod(&den, &x2, &x1, &SECP256K1_P);
  u256_inv_mod(&tmp, &den, &SECP256K1_P);             /* tmp = den^(-1) */
  u256_mul_mod_p(&lambda, &num, &tmp);                /* lambda */

  /* x3 = lambda^2 - x1 - x2 */
  u256_sqr_mod_p(&tmp, &lambda);                      /* tmp = lambda^2 */
  u256_sub_mod(&tmp2, &tmp, &x1, &SECP256K1_P);      /* tmp2 = lambda^2 - x1 */
  u256_sub_mod(&r->x, &tmp2, &x2, &SECP256K1_P);     /* x3 */

  /* y3 = lambda * (x1 - x3) - y1 */
  u256_sub_mod(&tmp, &x1, &r->x, &SECP256K1_P);      /* tmp = x1 - x3 */
  u256_mul_mod_p(&tmp2, &lambda, &tmp);               /* tmp2 = lambda*(x1-x3) */
  u256_sub_mod(&r->y, &tmp2, &y1, &SECP256K1_P);     /* y3 */
  r->is_infinity = 0;
}

/**
 * @brief 标量乘法: R = k * P (double-and-add 算法)
 * @param r 结果点
 * @param k 标量 (256 位)
 * @param p 基点
 */
static void ecp_mul(ecc_point *r, const uint256_t *k, const ecc_point *p) {
  ecc_point tmp, base;

  ecp_set_infinity(&tmp);
  ecp_copy(&base, p);

  /* 从最高位到最低位遍历 */
  for (int i = 255; i >= 0; i--) {
    ecp_double(&tmp, &tmp);
    /* 检查 k 的第 i 位 */
    int limb_idx = i / 32;
    int bit_idx = i % 32;
    if ((k->d[limb_idx] >> bit_idx) & 1) {
      ecp_add(&tmp, &tmp, &base);
    }
  }

  ecp_copy(r, &tmp);
}

/**
 * @brief 将公钥点编码为十六进制字符串 (未压缩 04||x||y 或压缩 02/03||x)
 * @param L          Lua 状态
 * @param pub        公钥点
 * @param compressed 是否压缩 (1 = 压缩, 0 = 未压缩)
 */
static void push_pubkey_hex(lua_State *L, const ecc_point *pub, int compressed) {
  uint8_t x_bytes[32], y_bytes[32];
  u256_to_bytes(x_bytes, &pub->x);
  u256_to_bytes(y_bytes, &pub->y);

  if (compressed) {
    /* 压缩格式: 前缀(02偶/03奇) + x 坐标 (33 字节) */
    char hex[67];
    uint8_t prefix = (y_bytes[31] & 1) ? PUBKEY_COMPRESSED_ODD : PUBKEY_COMPRESSED_EVEN;
    uint8_t buf[33];
    buf[0] = prefix;
    memcpy(buf + 1, x_bytes, 32);
    bin_to_hex(hex, buf, 33);
    lua_pushstring(L, hex);
  } else {
    /* 未压缩格式: 04 + x + y (65 字节) */
    char hex[131];
    uint8_t buf[65];
    buf[0] = PUBKEY_UNCOMPRESSED;
    memcpy(buf + 1, x_bytes, 32);
    memcpy(buf + 33, y_bytes, 32);
    bin_to_hex(hex, buf, 65);
    lua_pushstring(L, hex);
  }
}

/**
 * @brief 从十六进制编码的公钥字符串解析出点
 * @param r          结果点
 * @param pubkey_hex 公钥十六进制字符串
 * @return 成功返回 0, 失败返回 -1
 */
static int parse_pubkey_hex(ecc_point *r, const char *pubkey_hex) {
  size_t len = strlen(pubkey_hex);

  if (len == 130) {
    /* 未压缩格式: 04 + x(32字节) + y(32字节) = 65字节 = 130 hex字符 */
    uint8_t buf[65];
    if (hex_to_bin(pubkey_hex, buf, 65) != 0) return -1;
    if (buf[0] != PUBKEY_UNCOMPRESSED) return -1;
    u256_from_bytes(&r->x, buf + 1);
    u256_from_bytes(&r->y, buf + 33);
    r->is_infinity = 0;
    return 0;
  } else if (len == 66) {
    /* 压缩格式: 02/03 + x(32字节) = 33字节 */
    uint8_t buf[33];
    if (hex_to_bin(pubkey_hex, buf, 33) != 0) return -1;
    if (buf[0] != PUBKEY_COMPRESSED_EVEN && buf[0] != PUBKEY_COMPRESSED_ODD)
      return -1;
    u256_from_bytes(&r->x, buf + 1);

    /* 从 x 坐标恢复 y: y^2 = x^3 + 7 (secp256k1, a=0, b=7) */
    uint256_t x3, y2, tmp;
    u256_sqr_mod_p(&tmp, &r->x);              /* x^2 */
    u256_mul_mod_p(&x3, &tmp, &r->x);         /* x^3 */
    u256_add_mod(&y2, &x3, &SECP256K1_B, &SECP256K1_P);  /* x^3 + 7 */

    /* 计算模平方根: y = y2^((p+1)/4) mod p (因 p ≡ 3 mod 4) */
    /* (p+1)/4 = 2^254 - 2^30 - 244 */
    /* 简化: 使用指数运算 */
    {
      uint256_t exp, base;
      /* exp = (p+1)/4 */
      /* p+1 = 2^256 - 2^32 - 976 */
      /* (p+1)/4 = 2^254 - 2^30 - 244 */
      memset(exp.d, 0, sizeof(exp.d));
      /* 2^254 → 第 254 位, 即 limb[7] bit(254-224=30) = bit 30, 或 limb[7] = 1<<30 */
      /* limb[7] = 0x40000000, limb[6] = 0 */
      /* 等等, 254/32 = 7 余 30, 所以 limb[7] 的第 30 位 */
      exp.d[7] = 0x40000000;  /* 2^254 */
      /* 减 2^30: 30/32 = 0 余 30, limb[0] */
      exp.d[0] = (uint32_t)(-(int32_t)(1 << 30)); /* -2^30 mod 2^32 */
      /* 减 244 = 0xF4 */
      exp.d[0] -= 244;

      u256_copy(&base, &y2);

      /* 使用 square-and-multiply 计算 base^exp mod p */
      /* 简化: 用 double-and-add 类比, 对 256 位指数 */
      memset(tmp.d, 0, sizeof(tmp.d));
      tmp.d[0] = 1;  /* result = 1 */

      for (int i = 255; i >= 0; i--) {
        u256_mul_mod_p(&tmp, &tmp, &tmp);  /* square */
        int limb_idx = i / 32;
        int bit_idx = i % 32;
        if ((exp.d[limb_idx] >> bit_idx) & 1) {
          u256_mul_mod_p(&tmp, &tmp, &base);  /* multiply */
        }
      }

      u256_copy(&r->y, &tmp);
    }

    /* 验证 y 的奇偶性与前缀匹配 */
    uint8_t y_byte;
    {
      uint32_t limb = r->y.d[0];
      y_byte = (uint8_t)(limb & 1);
    }
    int expected_odd = (buf[0] == PUBKEY_COMPRESSED_ODD) ? 1 : 0;
    if (y_byte != (uint8_t)expected_odd) {
      /* 取 -y mod p */
      u256_sub_mod(&r->y, &SECP256K1_P, &r->y, &SECP256K1_P);
    }

    r->is_infinity = 0;
    return 0;
  }

  return -1;
}

/* ========================================================================
 * 公钥验证: 确保点在曲线上
 * ======================================================================== */

/**
 * @brief 验证点是否在 secp256k1 曲线上: y^2 ≡ x^3 + 7 (mod p)
 * @param p 要验证的点
 * @return 1 如果在曲线上, 0 否则
 */
static int ecp_is_on_curve(const ecc_point *p) {
  if (p->is_infinity) return 1;

  uint256_t x3, y2, rhs;

  /* x >= p 则无效 */
  if (u256_ge(&p->x, &SECP256K1_P)) return 0;

  /* 左: y^2 mod p */
  u256_sqr_mod_p(&y2, &p->y);

  /* 右: x^3 + 7 mod p */
  u256_mul_mod_p(&x3, &p->x, &p->x);       /* x^2 */
  u256_mul_mod_p(&x3, &x3, &p->x);         /* x^3 */
  u256_add_mod(&rhs, &x3, &SECP256K1_B, &SECP256K1_P); /* x^3 + 7 */

  return u256_cmp(&y2, &rhs) == 0;
}

/* ========================================================================
 * ECDSA 签名 (secp256k1)
 * ======================================================================== */

/**
 * @brief ECDSA 签名: signature = sign(data, private_key)
 *        返回 {r = hex, s = hex}
 *        使用密码学安全随机数 k (CSPRNG)
 * @param L Lua 状态
 * @return 栈上有 1 个值 (table {r, s})
 */
static int ecc_sign(lua_State *L,
                    const uint8_t *data, size_t data_len,
                    const uint256_t *priv_key) {
  /* Step 1: 计算数据的 SHA-256 哈希 */
  uint8_t hash[32];
  SHA256(data, data_len, hash);

  /* 将哈希转为 uint256_t */
  uint256_t z;
  u256_from_bytes(&z, hash);

  /* 确保 z < n */
  /* z 是 SHA-256 输出, 256 位。n 也是 256 位。z < n 几乎总是成立 */
  /* 但若 z >= n, 需要截断 (按 SEC1 标准: 取 z 的最左 log2(n) 位) */
  /* 对 secp256k1: n ≈ 0.9999 * 2^256, 碰撞概率极低, 直接比较 */
  if (u256_ge(&z, &SECP256K1_N)) {
    u256_sub(&z, &z, &SECP256K1_N);  /* 简化处理: 减 n */
  }

  CSPRNG_State *csprng = get_csprng();

  uint256_t k, k_inv, r_scalar, s_scalar, tmp;
  ecc_point K;

  /* Step 2: 使用随机 k 生成签名 (循环直到 r != 0 且 s != 0) */
  for (;;) {
    /* 生成密码学安全随机数 k ∈ [1, n-1] */
    u256_random_mod_n(&k, csprng);

    /* 计算 K = k * G */
    ecp_mul(&K, &k, &SECP256K1_G);

    /* r = K.x mod n */
    u256_copy(&r_scalar, &K.x);
    while (u256_ge(&r_scalar, &SECP256K1_N)) {
      u256_sub(&r_scalar, &r_scalar, &SECP256K1_N);
    }

    if (u256_is_zero(&r_scalar)) continue;  /* r 不能为 0, 重新选择 k */

    /* s = k^(-1) * (z + r * d) mod n */
    u256_inv_mod(&k_inv, &k, &SECP256K1_N);

    /* tmp = r * d mod n */
    u256_mul_mod(&tmp, &r_scalar, priv_key, &SECP256K1_N);

    /* tmp = z + r*d mod n */
    u256_add_mod(&tmp, &z, &tmp, &SECP256K1_N);

    /* s = k^(-1) * (z + r*d) mod n */
    u256_mul_mod(&s_scalar, &k_inv, &tmp, &SECP256K1_N);

    if (u256_is_zero(&s_scalar)) continue;  /* s 不能为 0, 重新选择 k */

    break;
  }

  /* Step 3: 返回 {r = hex, s = hex} */
  lua_newtable(L);

  lua_pushstring(L, "r");
  push_uint256_hex(L, &r_scalar);
  lua_settable(L, -3);

  lua_pushstring(L, "s");
  push_uint256_hex(L, &s_scalar);
  lua_settable(L, -3);

  return 1;
}

/**
 * @brief ECDSA 验证: verify(data, signature, public_key) -> boolean
 * @param L Lua 状态
 * @return 栈上有 1 个值 (boolean)
 */
static int ecc_verify_internal(lua_State *L,
                               const uint8_t *data, size_t data_len,
                               const uint256_t *r_scalar,
                               const uint256_t *s_scalar,
                               const ecc_point *pub_key) {
  (void)L;  /* 保留以备将来错误报告使用 */
  /* 验证点是否在曲线上 */
  if (!ecp_is_on_curve(pub_key)) return 0;

  /* 验证 r, s ∈ [1, n-1] */
  if (u256_is_zero(r_scalar) || u256_ge(r_scalar, &SECP256K1_N)) return 0;
  if (u256_is_zero(s_scalar) || u256_ge(s_scalar, &SECP256K1_N)) return 0;

  /* Step 1: 计算数据哈希 */
  uint8_t hash[32];
  SHA256(data, data_len, hash);

  uint256_t z;
  u256_from_bytes(&z, hash);
  if (u256_ge(&z, &SECP256K1_N)) {
    u256_sub(&z, &z, &SECP256K1_N);
  }

  /* Step 2: w = s^(-1) mod n */
  uint256_t w;
  u256_inv_mod(&w, s_scalar, &SECP256K1_N);

  /* Step 3: u1 = z * w mod n, u2 = r * w mod n */
  uint256_t u1, u2;
  u256_mul_mod(&u1, &z, &w, &SECP256K1_N);
  u256_mul_mod(&u2, r_scalar, &w, &SECP256K1_N);

  /* Step 4: 计算点 P = u1*G + u2*Q */
  ecc_point P1, P2, P;
  ecp_mul(&P1, &u1, &SECP256K1_G);
  ecp_mul(&P2, &u2, pub_key);
  ecp_add(&P, &P1, &P2);

  if (P.is_infinity) return 0;

  /* Step 5: 验证 r == P.x mod n */
  uint256_t px_mod_n;
  u256_copy(&px_mod_n, &P.x);
  while (u256_ge(&px_mod_n, &SECP256K1_N)) {
    u256_sub(&px_mod_n, &px_mod_n, &SECP256K1_N);
  }

  return u256_cmp(r_scalar, &px_mod_n) == 0;
}

/* ========================================================================
 * ECDH 密钥交换
 * ======================================================================== */

/**
 * @brief ECDH 密钥交换: shared = (priv_key * peer_pub).x
 * @param L Lua 状态
 * @return 栈上有 1 个值 (shared_secret 十六进制字符串)
 */
static int ecc_ecdh_internal(lua_State *L,
                             const uint256_t *priv_key,
                             const ecc_point *peer_pub) {
  if (!ecp_is_on_curve(peer_pub)) {
    luaL_error(L, "ECDH: peer public key is not on the curve");
    return 0;
  }

  ecc_point shared;
  ecp_mul(&shared, priv_key, peer_pub);

  if (shared.is_infinity) {
    luaL_error(L, "ECDH: shared point is infinity (invalid)");
    return 0;
  }

  /* 返回 shared.x 的十六进制字符串 */
  push_uint256_hex(L, &shared.x);
  return 1;
}

/* ========================================================================
 * 公钥恢复 (Ethereum 风格, recid 0-3)
 * ======================================================================== */

/**
 * @brief 从签名恢复公钥 (Ethereum/Bitcoin 风格)
 *        recid ∈ {0, 1, 2, 3}
 *        0-1: r 对应的 y 是偶数/奇数 (压缩标志)
 *        recid >= 2 表示 r + n >= secp256k1_n, 即 r 需要加 n
 * @param L Lua 状态
 * @return 栈上有 1 个值 (公钥十六进制字符串或 nil)
 */
static int ecc_recover_internal(lua_State *L,
                                const uint8_t *data, size_t data_len,
                                const uint256_t *r_scalar,
                                const uint256_t *s_scalar,
                                int recid) {
  if (recid < 0 || recid > 3) {
    lua_pushnil(L);
    lua_pushstring(L, "invalid recovery id (must be 0-3)");
    return 2;
  }

  /* 检查 s ∈ [1, n-1] */
  if (u256_is_zero(s_scalar) || u256_ge(s_scalar, &SECP256K1_N)) {
    lua_pushnil(L);
    return 1;
  }

  /* 计算消息哈希 */
  uint8_t hash[32];
  SHA256(data, data_len, hash);

  uint256_t z;
  u256_from_bytes(&z, hash);
  if (u256_ge(&z, &SECP256K1_N)) {
    u256_sub(&z, &z, &SECP256K1_N);
  }

  /* r 可能需要加上 n (当 recid >= 2) */
  uint256_t r_x;
  u256_copy(&r_x, r_scalar);
  if (recid >= 2) {
    u256_add_mod(&r_x, &r_x, &SECP256K1_N, &SECP256K1_N);
    recid -= 2;
  }

  /* 检查 r_x < p (用于椭圆曲线 x 坐标) */
  if (u256_ge(&r_x, &SECP256K1_P)) {
    lua_pushnil(L);
    return 1;
  }

  /* 从 x = r_x 恢复椭圆曲线点 R */
  /* y^2 = x^3 + 7 mod p */
  uint256_t x3, y2, y;
  u256_sqr_mod_p(&x3, &r_x);
  u256_mul_mod_p(&x3, &x3, &r_x);                /* x^3 */
  u256_add_mod(&y2, &x3, &SECP256K1_B, &SECP256K1_P);  /* x^3 + 7 */

  /* 模平方根: (p+1)/4 = 2^254 - 2^30 - 244 */
  {
    uint256_t exp;
    memset(exp.d, 0, sizeof(exp.d));
    exp.d[7] = 0x40000000;
    exp.d[0] = (uint32_t)(-(int32_t)(1 << 30));
    exp.d[0] -= 244;

    /* 初始化 result = 1 */
    memset(y.d, 0, sizeof(y.d));
    y.d[0] = 1;

    for (int i = 255; i >= 0; i--) {
      u256_mul_mod_p(&y, &y, &y);
      int limb_idx = i / 32;
      int bit_idx = i % 32;
      if ((exp.d[limb_idx] >> bit_idx) & 1) {
        u256_mul_mod_p(&y, &y, &y2);
      }
    }
  }

  /* 如果 recid 的奇偶与 y 的奇偶不匹配, 取 y = p - y */
  if ((recid & 1) != (int)(y.d[0] & 1)) {
    u256_sub_mod(&y, &SECP256K1_P, &y, &SECP256K1_P);
  }

  /* 构建恢复的点 R = (r_x, y) */
  ecc_point R_point;
  u256_copy(&R_point.x, &r_x);
  u256_copy(&R_point.y, &y);
  R_point.is_infinity = 0;

  /* 计算 r^(-1) mod n 和 s*r^(-1) mod n */
  uint256_t r_inv, s_r_inv, z_neg;
  u256_inv_mod(&r_inv, r_scalar, &SECP256K1_N);

  /* tmp = s * r^(-1) mod n */
  u256_mul_mod(&s_r_inv, s_scalar, &r_inv, &SECP256K1_N);

  /* z_neg = (-z) mod n */
  u256_sub_mod(&z_neg, &U256_ZERO, &z, &SECP256K1_N);

  /* z_r_inv = z * r^(-1) mod n, 但是取负: -z * r^(-1) mod n */
  uint256_t z_r_inv_neg;
  u256_mul_mod(&z_r_inv_neg, &z_neg, &r_inv, &SECP256K1_N);

  /* Q = s*r^(-1)*R + (-z)*r^(-1)*G */
  ecc_point Q1, Q2, Q;
  ecp_mul(&Q1, &s_r_inv, &R_point);
  ecp_mul(&Q2, &z_r_inv_neg, &SECP256K1_G);
  ecp_add(&Q, &Q1, &Q2);

  if (Q.is_infinity || !ecp_is_on_curve(&Q)) {
    lua_pushnil(L);
    return 1;
  }

  /* 返回未压缩公钥十六进制字符串 */
  push_pubkey_hex(L, &Q, 0);
  return 1;
}

/* ========================================================================
 * Lua C 函数绑定
 * ======================================================================== */

/* ------------------------------------------------------------------------
 * ecc.key.generate() -> {private_key, public_key}
 * ------------------------------------------------------------------------ */

/**
 * @brief 生成新的 ECC 密钥对 (secp256k1)
 *        私钥: [1, n-1] 的密码学安全随机数
 *        公钥: 私钥 * G
 * @param L Lua 状态
 * @return 1 (table {private_key = hex, public_key = hex})
 */
static int l_ecc_key_generate(lua_State *L) {
  CSPRNG_State *csprng = get_csprng();

  /* 生成随机私钥 */
  uint256_t priv_key;
  u256_random_mod_n(&priv_key, csprng);

  /* 计算公钥 */
  ecc_point pub_key;
  ecp_mul(&pub_key, &priv_key, &SECP256K1_G);

  /* 返回 table */
  lua_newtable(L);

  lua_pushstring(L, "private_key");
  push_uint256_hex(L, &priv_key);
  lua_settable(L, -3);

  lua_pushstring(L, "public_key");
  push_pubkey_hex(L, &pub_key, 0);
  lua_settable(L, -3);

  return 1;
}

/* ------------------------------------------------------------------------
 * ecc.key.from_private(priv_hex) -> {private_key, public_key}
 * ------------------------------------------------------------------------ */

/**
 * @brief 从已知私钥 (十六进制) 推导公钥
 * @param L Lua 状态
 *   - 参数 1: 私钥十六进制字符串 (64 字符)
 * @return 1 (table {private_key, public_key})
 */
static int l_ecc_key_from_private(lua_State *L) {
  const char *priv_hex = luaL_checkstring(L, 1);

  uint256_t priv_key;
  if (u256_from_hex(&priv_key, priv_hex) != 0) {
    luaL_error(L, "invalid private key hex (must be 64 hex characters)");
    return 0;
  }

  if (u256_is_zero(&priv_key) || u256_ge(&priv_key, &SECP256K1_N)) {
    luaL_error(L, "private key out of range (must be in [1, n-1])");
    return 0;
  }

  /* 计算公钥 */
  ecc_point pub_key;
  ecp_mul(&pub_key, &priv_key, &SECP256K1_G);

  /* 返回 table */
  lua_newtable(L);

  lua_pushstring(L, "private_key");
  push_uint256_hex(L, &priv_key);
  lua_settable(L, -3);

  lua_pushstring(L, "public_key");
  push_pubkey_hex(L, &pub_key, 0);
  lua_settable(L, -3);

  return 1;
}

/* ------------------------------------------------------------------------
 * ecc.sign(data, private_key) -> {r, s}
 * ------------------------------------------------------------------------ */

/**
 * @brief ECDSA 签名
 * @param L Lua 状态
 *   - 参数 1: 待签名数据 (字符串)
 *   - 参数 2: 私钥 (十六进制字符串, 64 字符)
 * @return 1 (table {r = hex, s = hex})
 */
static int l_ecc_sign(lua_State *L) {
  size_t data_len;
  const char *data = luaL_checklstring(L, 1, &data_len);
  const char *priv_hex = luaL_checkstring(L, 2);

  uint256_t priv_key;
  if (u256_from_hex(&priv_key, priv_hex) != 0) {
    luaL_error(L, "invalid private key hex");
    return 0;
  }

  if (u256_is_zero(&priv_key) || u256_ge(&priv_key, &SECP256K1_N)) {
    luaL_error(L, "private key out of range");
    return 0;
  }

  return ecc_sign(L, (const uint8_t *)data, data_len, &priv_key);
}

/* ------------------------------------------------------------------------
 * ecc.verify(data, signature, public_key) -> boolean
 * ------------------------------------------------------------------------ */

/**
 * @brief ECDSA 签名验证
 * @param L Lua 状态
 *   - 参数 1: 原始数据 (字符串)
 *   - 参数 2: 签名 table {r = hex, s = hex}
 *   - 参数 3: 公钥十六进制字符串 (未压缩 130 字符或压缩 66 字符)
 * @return 1 (boolean)
 */
static int l_ecc_verify(lua_State *L) {
  size_t data_len;
  const char *data = luaL_checklstring(L, 1, &data_len);

  /* 参数 2: 签名 table */
  luaL_checktype(L, 2, LUA_TTABLE);

  /* 获取 r */
  lua_getfield(L, 2, "r");
  if (!lua_isstring(L, -1)) {
    luaL_error(L, "signature table must contain field 'r' (hex string)");
  }
  const char *r_hex = lua_tostring(L, -1);
  lua_pop(L, 1);

  /* 获取 s */
  lua_getfield(L, 2, "s");
  if (!lua_isstring(L, -1)) {
    luaL_error(L, "signature table must contain field 's' (hex string)");
  }
  const char *s_hex = lua_tostring(L, -1);
  lua_pop(L, 1);

  /* 参数 3: 公钥 */
  const char *pubkey_hex = luaL_checkstring(L, 3);

  /* 解析 r, s */
  uint256_t r_scalar, s_scalar;
  if (u256_from_hex(&r_scalar, r_hex) != 0 ||
      u256_from_hex(&s_scalar, s_hex) != 0) {
    luaL_error(L, "invalid signature hex (r and s must be 64 hex characters each)");
    return 0;
  }

  /* 解析公钥 */
  ecc_point pub_key;
  if (parse_pubkey_hex(&pub_key, pubkey_hex) != 0) {
    luaL_error(L, "invalid public key hex");
    return 0;
  }

  int valid = ecc_verify_internal(L, (const uint8_t *)data, data_len,
                                  &r_scalar, &s_scalar, &pub_key);
  lua_pushboolean(L, valid);
  return 1;
}

/**
 * @brief ECDSA 调试验证: 返回中间值 table
 *        用于诊断 verify 为何返回 false
 */
static int l_ecc_debug_verify(lua_State *L) {
  size_t data_len;
  const char *data = luaL_checklstring(L, 1, &data_len);

  luaL_checktype(L, 2, LUA_TTABLE);

  lua_getfield(L, 2, "r");
  const char *r_hex = lua_tostring(L, -1);
  lua_pop(L, 1);

  lua_getfield(L, 2, "s");
  const char *s_hex = lua_tostring(L, -1);
  lua_pop(L, 1);

  const char *pubkey_hex = luaL_checkstring(L, 3);

  uint256_t r_scalar, s_scalar;
  if (u256_from_hex(&r_scalar, r_hex) != 0 ||
      u256_from_hex(&s_scalar, s_hex) != 0) {
    luaL_error(L, "invalid signature hex");
    return 0;
  }

  ecc_point pub_key;
  if (parse_pubkey_hex(&pub_key, pubkey_hex) != 0) {
    luaL_error(L, "invalid public key hex");
    return 0;
  }

  /* 创建结果表 */
  lua_createtable(L, 0, 10);

  /* 检查 1: 点在曲线上 */
  int on_curve = ecp_is_on_curve(&pub_key);
  lua_pushstring(L, "on_curve");
  lua_pushboolean(L, on_curve);
  lua_settable(L, -3);
  if (!on_curve) {
    lua_pushstring(L, "valid");
    lua_pushboolean(L, 0);
    lua_settable(L, -3);
    return 1;
  }

  /* 检查 2: r, s 范围 */
  int r_valid = !u256_is_zero(&r_scalar) && !u256_ge(&r_scalar, &SECP256K1_N);
  int s_valid = !u256_is_zero(&s_scalar) && !u256_ge(&s_scalar, &SECP256K1_N);
  lua_pushstring(L, "r_in_range");
  lua_pushboolean(L, r_valid);
  lua_settable(L, -3);
  lua_pushstring(L, "s_in_range");
  lua_pushboolean(L, s_valid);
  lua_settable(L, -3);

  /* 计算哈希 z */
  uint8_t hash[32];
  SHA256((const uint8_t *)data, data_len, hash);
  uint256_t z;
  u256_from_bytes(&z, hash);
  if (u256_ge(&z, &SECP256K1_N)) {
    u256_sub(&z, &z, &SECP256K1_N);
  }
  lua_pushstring(L, "z_hex");
  push_uint256_hex(L, &z);
  lua_settable(L, -3);

  /* w = s^(-1) mod n */
  uint256_t w;
  u256_inv_mod(&w, &s_scalar, &SECP256K1_N);
  lua_pushstring(L, "w_hex");
  push_uint256_hex(L, &w);
  lua_settable(L, -3);

  /* 验证 s * w ≡ 1 (mod n) */
  uint256_t sw;
  u256_mul_mod(&sw, &s_scalar, &w, &SECP256K1_N);
  lua_pushstring(L, "s_times_w_mod_n");
  push_uint256_hex(L, &sw);
  lua_settable(L, -3);

  /* u1 = z * w mod n, u2 = r * w mod n */
  uint256_t u1, u2;
  u256_mul_mod(&u1, &z, &w, &SECP256K1_N);
  u256_mul_mod(&u2, &r_scalar, &w, &SECP256K1_N);
  lua_pushstring(L, "u1_hex");
  push_uint256_hex(L, &u1);
  lua_settable(L, -3);
  lua_pushstring(L, "u2_hex");
  push_uint256_hex(L, &u2);
  lua_settable(L, -3);

  /* P1 = u1 * G */
  ecc_point P1;
  ecp_mul(&P1, &u1, &SECP256K1_G);
  lua_pushstring(L, "P1_x");
  push_uint256_hex(L, &P1.x);
  lua_settable(L, -3);
  lua_pushstring(L, "P1_y");
  push_uint256_hex(L, &P1.y);
  lua_settable(L, -3);

  /* P2 = u2 * Q */
  ecc_point P2;
  ecp_mul(&P2, &u2, &pub_key);
  lua_pushstring(L, "P2_x");
  push_uint256_hex(L, &P2.x);
  lua_settable(L, -3);
  lua_pushstring(L, "P2_y");
  push_uint256_hex(L, &P2.y);
  lua_settable(L, -3);

  /* P = P1 + P2 */
  ecc_point P;
  ecp_add(&P, &P1, &P2);
  lua_pushstring(L, "P_is_infinity");
  lua_pushboolean(L, P.is_infinity);
  lua_settable(L, -3);
  lua_pushstring(L, "P_x");
  push_uint256_hex(L, &P.x);
  lua_settable(L, -3);
  lua_pushstring(L, "P_y");
  push_uint256_hex(L, &P.y);
  lua_settable(L, -3);

  /* P.x mod n */
  uint256_t px_mod_n;
  u256_copy(&px_mod_n, &P.x);
  while (u256_ge(&px_mod_n, &SECP256K1_N)) {
    u256_sub(&px_mod_n, &px_mod_n, &SECP256K1_N);
  }
  lua_pushstring(L, "Px_mod_n");
  push_uint256_hex(L, &px_mod_n);
  lua_settable(L, -3);

  /* 最终比较 */
  int valid = u256_cmp(&r_scalar, &px_mod_n) == 0;
  lua_pushstring(L, "r_equals_Px_mod_n");
  lua_pushboolean(L, valid);
  lua_settable(L, -3);
  lua_pushstring(L, "valid");
  lua_pushboolean(L, valid);
  lua_settable(L, -3);

  return 1;
}

/**
 * @brief 调试点加倍: 逐步计算 2*G 并返回所有中间值
 */
static int l_ecc_debug_double(lua_State *L) {
  (void)L;

  uint256_t x, y, lambda, x2, three, two, two_y_inv, tmp, tmp2;
  ecc_point r;

  u256_copy(&x, &SECP256K1_G.x);
  u256_copy(&y, &SECP256K1_G.y);

  /* lambda = 3*x^2 / (2*y) mod p */
  u256_sqr_mod_p(&x2, &x);
  u256_add_mod(&three, &x2, &x2, &SECP256K1_P);
  u256_add_mod(&three, &three, &x2, &SECP256K1_P);

  u256_add_mod(&two, &y, &y, &SECP256K1_P);
  u256_inv_mod(&two_y_inv, &two, &SECP256K1_P);

  /* 验证 2*y * inv(2*y) ≡ 1 mod p (在覆盖 two_y_inv 之前) */
  uint256_t verify_inv;
  u256_mul_mod_p(&verify_inv, &two, &two_y_inv);

  u256_mul_mod_p(&lambda, &three, &two_y_inv);

  /* x3 = lambda^2 - 2*x mod p */
  u256_sqr_mod_p(&tmp, &lambda);
  u256_add_mod(&tmp2, &x, &x, &SECP256K1_P);
  u256_sub_mod(&r.x, &tmp, &tmp2, &SECP256K1_P);

  /* y3 = lambda * (x - x3) - y mod p */
  u256_sub_mod(&tmp, &x, &r.x, &SECP256K1_P);
  u256_mul_mod_p(&tmp2, &lambda, &tmp);
  u256_sub_mod(&r.y, &tmp2, &y, &SECP256K1_P);
  r.is_infinity = 0;

  int on_curve = ecp_is_on_curve(&r);

  /* 构建返回表 */
  lua_createtable(L, 0, 15);

  lua_pushstring(L, "G_x");
  push_uint256_hex(L, &x);
  lua_settable(L, -3);
  lua_pushstring(L, "G_y");
  push_uint256_hex(L, &y);
  lua_settable(L, -3);

  lua_pushstring(L, "x_squared");
  push_uint256_hex(L, &x2);
  lua_settable(L, -3);
  lua_pushstring(L, "three_x2");
  push_uint256_hex(L, &three);
  lua_settable(L, -3);

  lua_pushstring(L, "two_y");
  push_uint256_hex(L, &two);
  lua_settable(L, -3);
  lua_pushstring(L, "inv_two_y");
  push_uint256_hex(L, &two_y_inv);
  lua_settable(L, -3);
  lua_pushstring(L, "verify_inv");
  push_uint256_hex(L, &verify_inv);
  lua_settable(L, -3);

  lua_pushstring(L, "lambda");
  push_uint256_hex(L, &lambda);
  lua_settable(L, -3);

  {
    uint256_t lsq;
    u256_sqr_mod_p(&lsq, &lambda);
    lua_pushstring(L, "lambda_sq");
    push_uint256_hex(L, &lsq);
    lua_settable(L, -3);
  }

  lua_pushstring(L, "two_x");
  {
    uint256_t tx;
    u256_add_mod(&tx, &x, &x, &SECP256K1_P);
    push_uint256_hex(L, &tx);
  }
  lua_settable(L, -3);

  lua_pushstring(L, "x3");
  push_uint256_hex(L, &r.x);
  lua_settable(L, -3);
  lua_pushstring(L, "y3");
  push_uint256_hex(L, &r.y);
  lua_settable(L, -3);

  lua_pushstring(L, "on_curve");
  lua_pushboolean(L, on_curve);
  lua_settable(L, -3);

  return 1;
}

/* 调试点加法: 计算 2*G + G, 打印中间值 */
static int l_ecc_debug_add(lua_State *L) {
  (void)L;

  /* 2*G 已知坐标: */
  uint256_t G2_x, G2_y;
  u256_from_hex(&G2_x, "c6047f9441ed7d6d3045406e95c07cd85c778e4b8cef3ca7abac09b95c709ee5");
  u256_from_hex(&G2_y, "1ae168fea63dc339a3c58419466ceaeef7f632653266d0e1236431a950cfe52a");

  uint256_t G_x, G_y;
  G_x = SECP256K1_G.x;
  G_y = SECP256K1_G.y;

  uint256_t num, den, den_inv, lambda, tmp, tmp2;
  ecc_point r;

  /* 手动执行 ecp_add 的每一步并暴露中间值 */
  u256_sub_mod(&num, &G_y, &G2_y, &SECP256K1_P);       /* y2 - y1 */
  u256_sub_mod(&den, &G_x, &G2_x, &SECP256K1_P);       /* x2 - x1 */
  u256_inv_mod(&den_inv, &den, &SECP256K1_P);           /* (x2-x1)^(-1) */
  u256_mul_mod_p(&lambda, &num, &den_inv);              /* (y2-y1)/(x2-x1) */

  /* x3 = lambda^2 - x1 - x2 */
  u256_sqr_mod_p(&tmp, &lambda);
  u256_sub_mod(&tmp2, &tmp, &G2_x, &SECP256K1_P);
  u256_sub_mod(&r.x, &tmp2, &G_x, &SECP256K1_P);

  /* y3 = lambda * (x1 - x3) - y1 */
  u256_sub_mod(&tmp, &G2_x, &r.x, &SECP256K1_P);
  u256_mul_mod_p(&tmp2, &lambda, &tmp);
  u256_sub_mod(&r.y, &tmp2, &G2_y, &SECP256K1_P);
  r.is_infinity = 0;

  int on_curve = ecp_is_on_curve(&r);

  /* 验证 lambda * den mod p 应该等于 num */
  uint256_t verify_lam;
  u256_mul_mod_p(&verify_lam, &lambda, &den);

  /* 验证 den * den_inv mod p == 1 */
  uint256_t verify_inv;
  u256_mul_mod_p(&verify_inv, &den, &den_inv);

  lua_createtable(L, 0, 20);

  lua_pushstring(L, "x1"); push_uint256_hex(L, &G2_x); lua_settable(L, -3);
  lua_pushstring(L, "y1"); push_uint256_hex(L, &G2_y); lua_settable(L, -3);
  lua_pushstring(L, "x2"); push_uint256_hex(L, &G_x); lua_settable(L, -3);
  lua_pushstring(L, "y2"); push_uint256_hex(L, &G_y); lua_settable(L, -3);

  lua_pushstring(L, "num"); push_uint256_hex(L, &num); lua_settable(L, -3);
  lua_pushstring(L, "den"); push_uint256_hex(L, &den); lua_settable(L, -3);
  lua_pushstring(L, "den_inv"); push_uint256_hex(L, &den_inv); lua_settable(L, -3);
  lua_pushstring(L, "verify_inv"); push_uint256_hex(L, &verify_inv); lua_settable(L, -3);
  lua_pushstring(L, "lambda"); push_uint256_hex(L, &lambda); lua_settable(L, -3);
  lua_pushstring(L, "verify_lam"); push_uint256_hex(L, &verify_lam); lua_settable(L, -3);
  lua_pushstring(L, "lambda_sq"); push_uint256_hex(L, &tmp); lua_settable(L, -3);

  lua_pushstring(L, "x3"); push_uint256_hex(L, &r.x); lua_settable(L, -3);
  lua_pushstring(L, "y3"); push_uint256_hex(L, &r.y); lua_settable(L, -3);

  /* 输出 x1 - x3 (用于 y3 的乘法因子) */
  {
    uint256_t diff;
    u256_sub_mod(&diff, &G2_x, &r.x, &SECP256K1_P);
    lua_pushstring(L, "x1_minus_x3"); push_uint256_hex(L, &diff); lua_settable(L, -3);
  }

  /* 输出 lambda * (x1 - x3) */
  {
    uint256_t product;
    u256_sub_mod(&tmp, &G2_x, &r.x, &SECP256K1_P);
    u256_mul_mod_p(&product, &lambda, &tmp);
    lua_pushstring(L, "lam_times_diff"); push_uint256_hex(L, &product); lua_settable(L, -3);
  }

  lua_pushstring(L, "on_curve"); lua_pushboolean(L, on_curve); lua_settable(L, -3);

  return 1;
}

/* ------------------------------------------------------------------------
 * ecc.ecdh(private_key, peer_public_key) -> shared_secret_hex
 * ------------------------------------------------------------------------ */

/**
 * @brief ECDH 密钥交换
 * @param L Lua 状态
 *   - 参数 1: 己方私钥 (十六进制字符串)
 *   - 参数 2: 对方公钥 (十六进制字符串)
 * @return 1 (共享密钥的 x 坐标, 十六进制字符串)
 */
static int l_ecc_ecdh(lua_State *L) {
  const char *priv_hex = luaL_checkstring(L, 1);
  const char *pubkey_hex = luaL_checkstring(L, 2);

  uint256_t priv_key;
  if (u256_from_hex(&priv_key, priv_hex) != 0) {
    luaL_error(L, "invalid private key hex");
    return 0;
  }

  ecc_point peer_pub;
  if (parse_pubkey_hex(&peer_pub, pubkey_hex) != 0) {
    luaL_error(L, "invalid peer public key hex");
    return 0;
  }

  return ecc_ecdh_internal(L, &priv_key, &peer_pub);
}

/* ------------------------------------------------------------------------
 * ecc.recover(data, signature, recovery_id) -> public_key_hex | nil
 * ------------------------------------------------------------------------ */

/**
 * @brief 从签名恢复公钥 (Ethereum 风格)
 * @param L Lua 状态
 *   - 参数 1: 原始数据 (字符串)
 *   - 参数 2: 签名 table {r = hex, s = hex}
 *   - 参数 3: recovery_id (整数, 0-3)
 * @return 1-2 (公钥十六进制字符串或 nil [, error_message])
 */
static int l_ecc_recover(lua_State *L) {
  size_t data_len;
  const char *data = luaL_checklstring(L, 1, &data_len);

  luaL_checktype(L, 2, LUA_TTABLE);

  lua_getfield(L, 2, "r");
  if (!lua_isstring(L, -1)) luaL_error(L, "signature table must contain field 'r'");
  const char *r_hex = lua_tostring(L, -1);
  lua_pop(L, 1);

  lua_getfield(L, 2, "s");
  if (!lua_isstring(L, -1)) luaL_error(L, "signature table must contain field 's'");
  const char *s_hex = lua_tostring(L, -1);
  lua_pop(L, 1);

  int recid = (int)luaL_checkinteger(L, 3);

  uint256_t r_scalar, s_scalar;
  if (u256_from_hex(&r_scalar, r_hex) != 0 ||
      u256_from_hex(&s_scalar, s_hex) != 0) {
    luaL_error(L, "invalid signature hex");
    return 0;
  }

  return ecc_recover_internal(L, (const uint8_t *)data, data_len,
                              &r_scalar, &s_scalar, recid);
}

/* ------------------------------------------------------------------------
 * ecc.encode.public(pubkey, compressed) -> hex_string
 * ------------------------------------------------------------------------ */

/**
 * @brief 编码公钥为十六进制字符串
 *        pubkey 参数可以是:
 *          - 字符串 (已编码的十六进制公钥, 原样返回或转换格式)
 *          - table {x = hex, y = hex} (坐标表)
 * @param L Lua 状态
 *   - 参数 1: 公钥 (table 或 hex 字符串)
 *   - 参数 2: compressed (可选 boolean, 默认 false)
 * @return 1 (十六进制字符串)
 */
static int l_ecc_encode_public(lua_State *L) {
  int compressed = 0;
  if (lua_gettop(L) >= 2) {
    compressed = lua_toboolean(L, 2);
  }

  ecc_point pub;

  if (lua_istable(L, 1)) {
    /* 从 {x = hex, y = hex} 格式解析 */
    lua_getfield(L, 1, "x");
    if (!lua_isstring(L, -1)) luaL_error(L, "public key table must contain field 'x' (hex string)");
    const char *x_hex = lua_tostring(L, -1);
    lua_pop(L, 1);

    lua_getfield(L, 1, "y");
    if (!lua_isstring(L, -1)) luaL_error(L, "public key table must contain field 'y' (hex string)");
    const char *y_hex = lua_tostring(L, -1);
    lua_pop(L, 1);

    if (u256_from_hex(&pub.x, x_hex) != 0 || u256_from_hex(&pub.y, y_hex) != 0) {
      luaL_error(L, "invalid public key coordinates (must be 64 hex characters each)");
      return 0;
    }
    pub.is_infinity = 0;
  } else if (lua_isstring(L, 1)) {
    /* 从十六进制字符串解析 */
    const char *hex = lua_tostring(L, 1);
    if (parse_pubkey_hex(&pub, hex) != 0) {
      luaL_error(L, "invalid public key hex string");
      return 0;
    }
  } else {
    luaL_error(L, "public key must be a hex string or table {x, y}");
    return 0;
  }

  push_pubkey_hex(L, &pub, compressed);
  return 1;
}

/* ------------------------------------------------------------------------
 * ecc.encode.private(privkey) -> hex_string
 * ------------------------------------------------------------------------ */

/**
 * @brief 编码私钥为十六进制字符串 (实际上就是标准化格式)
 * @param L Lua 状态
 *   - 参数 1: 私钥 (十六进制字符串 或 原始字节)
 * @return 1 (十六进制字符串)
 */
static int l_ecc_encode_private(lua_State *L) {
  size_t len;
  const char *priv = luaL_checklstring(L, 1, &len);

  if (len == 64) {
    /* 已经是十六进制字符串, 小写标准化 */
    char lower[65];
    for (size_t i = 0; i < 64; i++) {
      char c = priv[i];
      if (c >= 'A' && c <= 'F') c = c - 'A' + 'a';
      lower[i] = c;
    }
    lower[64] = '\0';

    /* 验证是否为有效十六进制 */
    uint256_t tmp;
    if (u256_from_hex(&tmp, lower) != 0) {
      luaL_error(L, "invalid private key hex string");
      return 0;
    }
    lua_pushstring(L, lower);
  } else if (len == 32) {
    /* 原始 32 字节 */
    uint256_t key;
    u256_from_bytes(&key, (const uint8_t *)priv);
    push_uint256_hex(L, &key);
  } else {
    luaL_error(L, "private key must be 32 bytes (raw) or 64 hex characters");
  }
  return 1;
}

/* ------------------------------------------------------------------------
 * ecc.decode.public(pubkey_hex) -> public_key_table {x, y}
 * ------------------------------------------------------------------------ */

/**
 * @brief 解码公钥十六进制字符串为坐标表
 * @param L Lua 状态
 *   - 参数 1: 公钥十六进制字符串
 * @return 1 (table {x = hex, y = hex})
 */
static int l_ecc_decode_public(lua_State *L) {
  const char *hex = luaL_checkstring(L, 1);

  ecc_point pub;
  if (parse_pubkey_hex(&pub, hex) != 0) {
    luaL_error(L, "invalid public key hex string");
    return 0;
  }

  lua_newtable(L);

  lua_pushstring(L, "x");
  push_uint256_hex(L, &pub.x);
  lua_settable(L, -3);

  lua_pushstring(L, "y");
  push_uint256_hex(L, &pub.y);
  lua_settable(L, -3);

  return 1;
}

/* ------------------------------------------------------------------------
 * ecc.decode.private(privkey_hex) -> raw_private_key
 * ------------------------------------------------------------------------ */

/**
 * @brief 解码私钥十六进制字符串为原始字节
 * @param L Lua 状态
 *   - 参数 1: 私钥十六进制字符串 (64 字符)
 * @return 1 (32 字节原始字符串)
 */
static int l_ecc_decode_private(lua_State *L) {
  const char *hex = luaL_checkstring(L, 1);

  uint256_t key;
  if (u256_from_hex(&key, hex) != 0) {
    luaL_error(L, "invalid private key hex string (must be 64 hex characters)");
    return 0;
  }

  push_uint256_raw(L, &key);
  return 1;
}

/* ------------------------------------------------------------------------
 * 子表函数注册表
 * ------------------------------------------------------------------------ */

/** ecc.key 子表 */
static const luaL_Reg ecc_key_funcs[] = {
  {"generate",      l_ecc_key_generate},
  {"from_private",  l_ecc_key_from_private},
  {NULL, NULL}
};

/* ------------------------------------------------------------------------
 * ecc._debug_on_curve(pubkey_hex) -> {y2, rhs, on_curve, x_hex, y_hex}
 * 直接计算 y^2 和 x^3+7 来诊断 on_curve 失败原因
 * ------------------------------------------------------------------------ */
static int l_ecc_debug_on_curve(lua_State *L) {
  const char *pubkey_hex = luaL_checkstring(L, 1);

  ecc_point pub;
  if (parse_pubkey_hex(&pub, pubkey_hex) != 0) {
    luaL_error(L, "invalid public key hex");
    return 0;
  }

  lua_createtable(L, 0, 8);

  lua_pushstring(L, "x_hex");
  push_uint256_hex(L, &pub.x);
  lua_settable(L, -3);

  lua_pushstring(L, "y_hex");
  push_uint256_hex(L, &pub.y);
  lua_settable(L, -3);

  int x_ge_p = u256_ge(&pub.x, &SECP256K1_P);
  lua_pushstring(L, "x_ge_p");
  lua_pushboolean(L, x_ge_p);
  lua_settable(L, -3);

  uint256_t y2, x2, x3, rhs;
  u256_sqr_mod_p(&y2, &pub.y);
  u256_mul_mod_p(&x2, &pub.x, &pub.x);
  u256_mul_mod_p(&x3, &x2, &pub.x);
  u256_add_mod(&rhs, &x3, &SECP256K1_B, &SECP256K1_P);

  lua_pushstring(L, "y_squared");
  push_uint256_hex(L, &y2);
  lua_settable(L, -3);

  lua_pushstring(L, "x_squared");
  push_uint256_hex(L, &x2);
  lua_settable(L, -3);

  lua_pushstring(L, "x_cubed");
  push_uint256_hex(L, &x3);
  lua_settable(L, -3);

  lua_pushstring(L, "rhs");
  push_uint256_hex(L, &rhs);
  lua_settable(L, -3);

  int on_curve = (u256_cmp(&y2, &rhs) == 0);
  lua_pushstring(L, "on_curve");
  lua_pushboolean(L, on_curve);
  lua_settable(L, -3);

  return 1;
}

/* ------------------------------------------------------------------------
 * ecc._debug_mul_test(scalar, pubkey_hex) -> {result_x, result_y, on_curve}
 * 测试 ecp_mul 对指定标量和基点的结果
 * ------------------------------------------------------------------------ */
static int l_ecc_debug_mul_test(lua_State *L) {
  const char *scalar_hex = luaL_checkstring(L, 1);
  const char *pubkey_hex = luaL_checkstring(L, 2);

  uint256_t k;
  if (u256_from_hex(&k, scalar_hex) != 0) {
    luaL_error(L, "invalid scalar hex");
    return 0;
  }

  ecc_point base;
  if (parse_pubkey_hex(&base, pubkey_hex) != 0) {
    luaL_error(L, "invalid base point hex");
    return 0;
  }

  ecc_point result;
  ecp_mul(&result, &k, &base);

  lua_createtable(L, 0, 5);

  lua_pushstring(L, "x");
  push_uint256_hex(L, &result.x);
  lua_settable(L, -3);

  lua_pushstring(L, "y");
  push_uint256_hex(L, &result.y);
  lua_settable(L, -3);

  lua_pushstring(L, "is_infinity");
  lua_pushboolean(L, result.is_infinity);
  lua_settable(L, -3);

  int on_curve = ecp_is_on_curve(&result);
  lua_pushstring(L, "on_curve");
  lua_pushboolean(L, on_curve);
  lua_settable(L, -3);

  return 1;
}

/** ecc.encode 子表 */
static const luaL_Reg ecc_encode_funcs[] = {
  {"public",   l_ecc_encode_public},
  {"private",  l_ecc_encode_private},
  {NULL, NULL}
};

/** ecc.decode 子表 */
static const luaL_Reg ecc_decode_funcs[] = {
  {"public",   l_ecc_decode_public},
  {"private",  l_ecc_decode_private},
  {NULL, NULL}
};

/* ========================================================================
 * 模块入口: 打开 ecc 模块
 * ======================================================================== */

/**
 * @brief 打开 ecc 模块
 *        模块结构:
 *          ecc.key.generate()             -> {private_key, public_key}
 *          ecc.key.from_private(hex)      -> {private_key, public_key}
 *          ecc.sign(data, priv)           -> {r, s}
 *          ecc.verify(data, sig, pub)     -> boolean
 *          ecc.ecdh(priv, peer_pub)       -> hex
 *          ecc.recover(data, sig, recid)  -> pub_hex | nil
 *          ecc.encode.public(pub, comp)   -> hex
 *          ecc.encode.private(priv)       -> hex
 *          ecc.decode.public(hex)         -> table
 *          ecc.decode.private(hex)        -> raw
 * @param L Lua 状态
 * @return 1 (ecc 模块表)
 */
LUAMOD_API int luaopen_ecc(lua_State *L) {
  /* 创建主表 */
  lua_createtable(L, 0, 9);  /* 9 个条目: key, encode, decode 子表 + 6 个函数 */

  /* -- ecc.key 子表 -- */
  luaL_newlib(L, ecc_key_funcs);
  lua_setfield(L, -2, "key");

  /* -- ecc.encode 子表 -- */
  luaL_newlib(L, ecc_encode_funcs);
  lua_setfield(L, -2, "encode");

  /* -- ecc.decode 子表 -- */
  luaL_newlib(L, ecc_decode_funcs);
  lua_setfield(L, -2, "decode");

  /* -- ecc.sign 函数 -- */
  lua_pushcfunction(L, l_ecc_sign);
  lua_setfield(L, -2, "sign");

  /* -- ecc.verify 函数 -- */
  lua_pushcfunction(L, l_ecc_verify);
  lua_setfield(L, -2, "verify");

  /* -- ecc._debug_verify 函数 (调试用) -- */
  lua_pushcfunction(L, l_ecc_debug_verify);
  lua_setfield(L, -2, "_debug_verify");

  /* -- ecc._debug_double 函数 (调试用) -- */
  lua_pushcfunction(L, l_ecc_debug_double);
  lua_setfield(L, -2, "_debug_double");

  /* -- ecc._debug_add 函数 (调试用) -- */
  lua_pushcfunction(L, l_ecc_debug_add);
  lua_setfield(L, -2, "_debug_add");

  /* -- ecc._debug_on_curve 函数 (调试用) -- */
  lua_pushcfunction(L, l_ecc_debug_on_curve);
  lua_setfield(L, -2, "_debug_on_curve");

  /* -- ecc._debug_mul_test 函数 (调试用) -- */
  lua_pushcfunction(L, l_ecc_debug_mul_test);
  lua_setfield(L, -2, "_debug_mul_test");

  /* -- ecc.ecdh 函数 -- */
  lua_pushcfunction(L, l_ecc_ecdh);
  lua_setfield(L, -2, "ecdh");

  /* -- ecc.recover 函数 -- */
  lua_pushcfunction(L, l_ecc_recover);
  lua_setfield(L, -2, "recover");

  return 1;
}