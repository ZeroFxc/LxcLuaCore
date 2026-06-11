/**
 * @file lrsa.c
 * @brief LXCLUA RSA 密码库 - Lua C 模块
 *
 * 模块结构 (子表设计):
 *   rsa.key.generate(bits)              -> {public_key, private_key}
 *   rsa.key.from_components(n, e, d)    -> {public_key, private_key}
 *   rsa.public.encrypt(data, pubkey)    -> encrypted_bytes
 *   rsa.public.verify(data, sig, pubkey)-> boolean
 *   rsa.private.decrypt(data, privkey)  -> decrypted_bytes
 *   rsa.private.sign(data, privkey)     -> signature_bytes
 *   rsa.encode.public(pubkey)           -> PEM 格式字符串
 *   rsa.encode.private(privkey)         -> PEM 格式字符串
 *
 * 技术实现:
 *   - 自定义大整数 (uint32_t 数组, MSB first, 支持 32~4096 位)
 *   - Barrett 约简用于模幂运算 (高性能)
 *   - Miller-Rabin 素数检测 (40 轮)
 *   - PKCS#1 v1.5 填充 (SHA-256)
 *   - CSPRNG 密钥生成
 *   - Binary GCD / Binary Extended GCD 避免除法
 */

#define LUA_LIB

#include "lprefix.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "sha256.h"
#include "csprng.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ========================================================================
 * 全局 CSPRNG 状态 (惰性初始化)
 * ======================================================================== */

static CSPRNG_State g_rsa_csprng;
static int g_rsa_csprng_inited = 0;

/**
 * @brief 获取全局 CSPRNG 实例，首次调用时自动初始化
 * @return 指向全局 CSPRNG 状态的指针
 */
static CSPRNG_State* get_rsa_csprng(void) {
  if (!g_rsa_csprng_inited) {
    uint64_t seed = (uint64_t)time(NULL);
    csprng_init(&g_rsa_csprng, seed);
    g_rsa_csprng_inited = 1;
  }
  return &g_rsa_csprng;
}

/* ========================================================================
 * Part 1: 大整数 (BigInt) 库
 *
 * 表示方式: uint32_t 数组, MSB first
 * 结构体: bigint_t { uint32_t *d; int nlimbs; }
 * d[0] 是最高有效 limb, d[nlimbs-1] 是最低有效 limb
 * 零值: nlimbs = 0, d = NULL (或 nlimbs=1 且 d[0]=0)
 * ======================================================================== */

#define BI_MAX_LIMBS  ((4096 + 31) / 32)  /* 4096 位 = 最多 128 个 32 位 limb */

typedef struct {
  uint32_t *d;   /* limb 数组指针 (MSB first) */
  int      n;    /* limb 数量 */
} bigint_t;

/* ---------- 内存管理 ---------- */

/**
 * @brief 初始化大整数，分配 n 个 limb 并清零
 * @param a   大整数指针
 * @param n   limb 数量
 */
static void bi_init(bigint_t *a, int n) {
  a->n = n;
  if (n > 0) {
    a->d = (uint32_t *)calloc((size_t)n, sizeof(uint32_t));
  } else {
    a->d = NULL;
  }
}

/**
 * @brief 释放大整数占用的内存
 * @param a 大整数指针
 */
static void bi_free(bigint_t *a) {
  if (a->d) { free(a->d); a->d = NULL; }
  a->n = 0;
}

/**
 * @brief 调整大整数的 limb 数量 (保留高位/MSB 数据)
 * @param a    大整数指针
 * @param newn 新的 limb 数量
 */
static void bi_resize(bigint_t *a, int newn) {
  if (newn == a->n) return;
  uint32_t *nd = (uint32_t *)calloc((size_t)newn, sizeof(uint32_t));
  if (a->d && a->n > 0) {
    int copy_n = (a->n < newn) ? a->n : newn;
    /* MSB first: 复制高位部分 */
    int src_off = (a->n > newn) ? (a->n - newn) : 0;
    int dst_off = (newn > a->n) ? (newn - a->n) : 0;
    memcpy(nd + dst_off, a->d + src_off, (size_t)copy_n * sizeof(uint32_t));
    free(a->d);
  }
  a->d = nd;
  a->n = newn;
}

/**
 * @brief 规范化: 去除前导零 limb
 * @param a 大整数指针
 */
static void bi_normalize(bigint_t *a) {
  int lead = 0;
  while (lead < a->n && a->d[lead] == 0) lead++;
  if (lead == a->n) {
    /* 值为零 */
    bi_resize(a, 0);
  } else if (lead > 0) {
    int newn = a->n - lead;
    memmove(a->d, a->d + lead, (size_t)newn * sizeof(uint32_t));
    bi_resize(a, newn);
  }
}

/**
 * @brief 复制大整数
 * @param dst 目标
 * @param src 源
 */
static void bi_copy(bigint_t *dst, const bigint_t *src) {
  bi_free(dst);
  bi_init(dst, src->n);
  if (src->n > 0)
    memcpy(dst->d, src->d, (size_t)src->n * sizeof(uint32_t));
}

/* ---------- 基本查询 ---------- */

/**
 * @brief 判断大整数是否为零
 * @param a 大整数指针
 * @return 1 为零, 0 非零
 */
static int bi_is_zero(const bigint_t *a) {
  return (a->n == 0) || (a->n == 1 && a->d[0] == 0);
}

/**
 * @brief 判断大整数是否为 1
 * @param a 大整数指针
 * @return 1 为 1, 0 非 1
 */
static int bi_is_one(const bigint_t *a) {
  return (a->n == 1 && a->d[0] == 1);
}

/**
 * @brief 判断大整数是否为偶数
 * @param a 大整数指针
 * @return 1 是偶数, 0 是奇数
 */
static int bi_is_even(const bigint_t *a) {
  if (a->n == 0) return 1;
  return (a->d[a->n - 1] & 1) == 0;
}

/**
 * @brief 获取大整数的位长度
 * @param a 大整数指针
 * @return 位长度 (值为 0 返回 0)
 */
static int bi_nbits(const bigint_t *a) {
  if (a->n == 0) return 0;
  uint32_t msb = a->d[0];
  int bits = (a->n - 1) * 32;
  while (msb) { bits++; msb >>= 1; }
  return bits;
}

/* ---------- 比较 ---------- */

/**
 * @brief 比较两个大整数
 * @param a 第一个大整数
 * @param b 第二个大整数
 * @return -1 (a < b), 0 (a == b), 1 (a > b)
 */
static int bi_cmp(const bigint_t *a, const bigint_t *b) {
  if (a->n != b->n)
    return (a->n > b->n) ? 1 : -1;
  for (int i = 0; i < a->n; i++) {
    if (a->d[i] != b->d[i])
      return (a->d[i] > b->d[i]) ? 1 : -1;
  }
  return 0;
}

/* ---------- 设值 ---------- */

/**
 * @brief 将大整数设为零
 * @param a 大整数指针
 */
static void bi_set_zero(bigint_t *a) {
  bi_free(a);
  bi_init(a, 1);
  a->d[0] = 0;
}

/**
 * @brief 将大整数设为 1
 * @param a 大整数指针
 */
static void bi_set_one(bigint_t *a) {
  bi_free(a);
  bi_init(a, 1);
  a->d[0] = 1;
}

/**
 * @brief 将大整数设为单个 uint32_t 值
 * @param a 大整数指针
 * @param v 要设置的值
 */
static void bi_set_uint32(bigint_t *a, uint32_t v) {
  bi_free(a);
  if (v == 0) {
    bi_init(a, 1);
    a->d[0] = 0;
  } else {
    bi_init(a, 1);
    a->d[0] = v;
  }
}

/**
 * @brief 将大整数设为 2^exp (即只有第 exp 位为 1)
 * @param a   大整数指针
 * @param exp 指数 (位位置)
 */
static void bi_set_pow2(bigint_t *a, int exp) {
  int n = (exp / 32) + 1;
  bi_free(a);
  bi_init(a, n);
  int limb_idx = n - 1 - (exp / 32);
  int bit_idx  = exp % 32;
  a->d[limb_idx] = (uint32_t)1 << bit_idx;
}

/* ---------- 位移 ---------- */

/**
 * @brief 左移 bits 位 (r = a << bits)
 * @param r    结果 (需预先释放或为空)
 * @param a    操作数
 * @param bits 左移位数
 */
static void bi_shl(bigint_t *r, const bigint_t *a, int bits) {
  if (a->n == 0 || bi_is_zero(a)) { bi_set_zero(r); return; }
  if (bits == 0) { bi_copy(r, a); return; }

  int limb_shift = bits / 32;
  int bit_shift  = bits % 32;
  int newn = a->n + limb_shift + (bit_shift ? 1 : 0);

  bi_free(r);
  bi_init(r, newn);

  /* 复制 a 到 r 的低位部分, 然后将 r 左移 bit_shift */
  int a_start = newn - limb_shift - a->n;
  memcpy(r->d + a_start, a->d, (size_t)a->n * sizeof(uint32_t));

  if (bit_shift > 0) {
    uint32_t carry = 0;
    for (int i = newn - 1; i >= 0; i--) {
      uint64_t val = ((uint64_t)r->d[i] << bit_shift) | carry;
      r->d[i] = (uint32_t)val;
      carry = (uint32_t)(val >> 32);
    }
  }
}

/**
 * @brief 右移 bits 位 (r = a >> bits)
 * @param r    结果 (需预先释放或为空)
 * @param a    操作数
 * @param bits 右移位数
 */
static void bi_shr(bigint_t *r, const bigint_t *a, int bits) {
  if (a->n == 0 || bi_is_zero(a)) { bi_set_zero(r); return; }
  if (bits == 0) { bi_copy(r, a); return; }

  int limb_shift = bits / 32;
  int bit_shift  = bits % 32;

  if (limb_shift >= a->n) { bi_set_zero(r); return; }

  int newn = a->n - limb_shift;

  bi_free(r);
  bi_init(r, newn);
  memcpy(r->d, a->d + limb_shift, (size_t)newn * sizeof(uint32_t));

  if (bit_shift > 0) {
    for (int i = 0; i < newn - 1; i++) {
      r->d[i] = (r->d[i] >> bit_shift) | (r->d[i + 1] << (32 - bit_shift));
    }
    r->d[newn - 1] >>= bit_shift;
    bi_normalize(r);
  }

  if (r->n > 1 && r->d[0] == 0) bi_normalize(r);
  if (r->n == 0) bi_set_zero(r);
}

/* ---------- 加减法 ---------- */

/**
 * @brief 加法: r = a + b
 * @param r 结果 (需预先释放或为空)
 * @param a 加数
 * @param b 加数
 */
static void bi_add(bigint_t *r, const bigint_t *a, const bigint_t *b) {
  int maxn = (a->n > b->n) ? a->n : b->n;
  int newn = maxn + 1;  /* 可能产生进位 */

  bi_free(r);
  bi_init(r, newn);

  uint64_t carry = 0;
  int ri = newn - 1;
  int ai = a->n - 1;
  int bi_idx = b->n - 1;

  for (int i = 0; i < maxn; i++) {
    uint64_t av = (ai >= 0) ? a->d[ai] : 0;
    uint64_t bv = (bi_idx >= 0) ? b->d[bi_idx] : 0;
    uint64_t sum = av + bv + carry;
    r->d[ri] = (uint32_t)sum;
    carry = sum >> 32;
    ri--; ai--; bi_idx--;
  }
  r->d[ri] = (uint32_t)carry;
  bi_normalize(r);
}

/**
 * @brief 减法: r = a - b (要求 a >= b)
 * @param r 结果 (需预先释放或为空)
 * @param a 被减数
 * @param b 减数 (必须 <= a)
 */
static void bi_sub(bigint_t *r, const bigint_t *a, const bigint_t *b) {
  bi_free(r);
  bi_init(r, a->n);

  int64_t borrow = 0;
  int ri = a->n - 1;
  int bi_idx = b->n - 1;

  for (int i = 0; i < a->n; i++) {
    int64_t av = a->d[ri];
    int64_t bv = (bi_idx >= 0) ? b->d[bi_idx] : 0;
    int64_t diff = av - bv - borrow;
    if (diff < 0) { diff += (int64_t)0x100000000ULL; borrow = 1; }
    else { borrow = 0; }
    r->d[ri] = (uint32_t)diff;
    ri--; bi_idx--;
  }
  bi_normalize(r);
}

/* ---------- 乘法 ---------- */

/**
 * @brief 乘法: r = a * b (schoolbook)
 * @param r 结果 (需预先释放或为空)
 * @param a 乘数
 * @param b 乘数
 */
static void bi_mul(bigint_t *r, const bigint_t *a, const bigint_t *b) {
  if (bi_is_zero(a) || bi_is_zero(b)) { bi_set_zero(r); return; }

  int newn = a->n + b->n;
  bi_free(r);
  bi_init(r, newn);

  for (int i = 0; i < a->n; i++) {
    uint64_t carry = 0;
    uint64_t av = a->d[a->n - 1 - i];
    for (int j = 0; j < b->n; j++) {
      uint64_t bv = b->d[b->n - 1 - j];
      int pos = newn - 1 - (i + j);
      uint64_t prod = av * bv + r->d[pos] + carry;
      r->d[pos] = (uint32_t)prod;
      carry = prod >> 32;
    }
    /* 处理剩余的进位 */
    int pos = newn - 1 - (i + b->n);
    while (carry && pos >= 0) {
      uint64_t sum = r->d[pos] + carry;
      r->d[pos] = (uint32_t)sum;
      carry = sum >> 32;
      pos--;
    }
  }
  bi_normalize(r);
}

/* ---------- 除法 (二进制长除法, 用于 Barrett μ 计算) ---------- */

/**
 * @brief 二进制长除法: 计算 q = u / v, r = u % v
 *        用于非性能关键路径 (如 Barrett μ 初始化)
 * @param q 商 (可为 NULL 表示不需要)
 * @param r 余数 (不可为 NULL)
 * @param u 被除数
 * @param v 除数 (必须 > 0)
 */
static void bi_divmod_binary(bigint_t *q, bigint_t *r, const bigint_t *u, const bigint_t *v) {
  if (bi_is_zero(v)) return;  /* 除零错误 */

  int cmp = bi_cmp(u, v);
  if (cmp < 0) {
    if (q) bi_set_zero(q);
    bi_copy(r, u);
    return;
  }
  if (cmp == 0) {
    if (q) bi_set_one(q);
    bi_set_zero(r);
    return;
  }

  int shift = bi_nbits(u) - bi_nbits(v);

  /* r = u */
  bi_copy(r, u);
  if (q) bi_set_zero(q);

  bigint_t v_shifted;
  bi_init(&v_shifted, 1);

  for (int k = shift; k >= 0; k--) {
    bi_shl(&v_shifted, v, k);
    if (bi_cmp(r, &v_shifted) >= 0) {
      bigint_t tmp;
      bi_init(&tmp, 1);
      bi_sub(&tmp, r, &v_shifted);
      bi_copy(r, &tmp);
      bi_free(&tmp);
      if (q) {
        /* q |= (1 << k) */
        bigint_t bit, tmp_q;
        bi_init(&bit, 1);
        bi_set_pow2(&bit, k);
        bi_init(&tmp_q, 1);
        bi_add(&tmp_q, q, &bit);
        bi_copy(q, &tmp_q);
        bi_free(&tmp_q);
        bi_free(&bit);
      }
    }
    bi_free(&v_shifted);
    bi_init(&v_shifted, 1);
  }
  bi_free(&v_shifted);
}

/* ---------- Barrett 约简 ---------- */

/**
 * @brief 计算 Barrett 约简参数 μ = floor(2^(2*k) / m)
 *        其中 k = ceil(log2(m))
 * @param mu 输出的 μ 值 (需预先释放)
 * @param m  模数
 */
static void bi_barrett_mu(bigint_t *mu, const bigint_t *m) {
  int k = bi_nbits(m);
  /* μ = floor(2^(2*k) / m) */
  bigint_t pow2, rem;
  bi_init(&pow2, 1);
  bi_set_pow2(&pow2, 2 * k);
  bi_init(&rem, 1);
  bi_divmod_binary(mu, &rem, &pow2, m);
  bi_free(&pow2);
  bi_free(&rem);
}

/**
 * @brief Barrett 约简: r = x mod m
 *        要求: x < m^2 且 m 已规范化
 * @param r  结果 (需预先释放)
 * @param x  被约简值 (x < m^2)
 * @param m  模数
 * @param mu 预计算的 Barrett μ 值
 */
static void bi_barrett_reduce(bigint_t *r, const bigint_t *x,
                              const bigint_t *m, const bigint_t *mu) {
  int k = bi_nbits(m);
  if (bi_cmp(x, m) < 0) {
    bi_copy(r, x);
    return;
  }

  /* q1 = x >> (k-1) */
  bigint_t q1;
  bi_init(&q1, 1);
  bi_shr(&q1, x, k - 1);

  /* q2 = q1 * μ */
  bigint_t q2;
  bi_init(&q2, 1);
  bi_mul(&q2, &q1, mu);

  /* q3 = q2 >> (k+1) */
  bigint_t q3;
  bi_init(&q3, 1);
  bi_shr(&q3, &q2, k + 1);

  /* r = x - q3 * m */
  bigint_t q3m;
  bi_init(&q3m, 1);
  bi_mul(&q3m, &q3, m);

  if (bi_cmp(x, &q3m) >= 0) {
    bi_sub(r, x, &q3m);
  } else {
    /* 不应该发生, 但防御性处理 */
    bigint_t tmp;
    bi_init(&tmp, 1);
    bi_sub(&tmp, &q3m, x);
    bi_sub(r, m, &tmp);
    bi_free(&tmp);
  }

  /* 确保 r < m (最多两次减法) */
  while (bi_cmp(r, m) >= 0) {
    bigint_t tmp;
    bi_init(&tmp, 1);
    bi_sub(&tmp, r, m);
    bi_copy(r, &tmp);
    bi_free(&tmp);
  }

  bi_free(&q1); bi_free(&q2); bi_free(&q3); bi_free(&q3m);
}

/* ---------- 模幂运算 (Barrett 约简 + 平方乘) ---------- */

/**
 * @brief 模幂运算: r = base^exp mod mod (使用 Barrett 约简)
 * @param r    结果 (需预先释放)
 * @param base 底数
 * @param exp  指数
 * @param mod  模数
 */
static void bi_mod_exp(bigint_t *r, const bigint_t *base,
                       const bigint_t *exp, const bigint_t *mod) {
  if (bi_is_zero(mod)) return;

  /* 处理 base >= mod 的情况 */
  bigint_t b;
  bi_init(&b, 1);
  if (bi_cmp(base, mod) >= 0) {
    bigint_t rem;
    bi_init(&rem, 1);
    bi_divmod_binary(NULL, &rem, base, mod);
    bi_copy(&b, &rem);
    bi_free(&rem);
  } else {
    bi_copy(&b, base);
  }

  /* 预计算 Barrett μ */
  bigint_t mu;
  bi_init(&mu, 1);
  bi_barrett_mu(&mu, mod);

  /* 平方乘算法: 从 MSB 到 LSB */
  bi_set_one(r);

  int exp_bits = bi_nbits(exp);
  if (exp_bits == 0) { bi_free(&b); bi_free(&mu); return; }

  for (int i = 0; i < exp_bits; i++) {
    int limb_idx = i / 32;
    int bit_idx  = 31 - (i % 32);  /* MSB first → 从 limb 高位开始 */
    int exp_limb_count = exp->n;

    /* 从 exp 的最高位开始扫描 */
    int real_limb = exp_limb_count - 1 - limb_idx;
    if (real_limb < 0) continue;
    uint32_t bit = (exp->d[real_limb] >> bit_idx) & 1;

    /* 平方: r = r^2 mod mod */
    if (i > 0) {
      bigint_t r2;
      bi_init(&r2, 1);
      bi_mul(&r2, r, r);
      bigint_t tmp;
      bi_init(&tmp, 1);
      bi_barrett_reduce(&tmp, &r2, mod, &mu);
      bi_copy(r, &tmp);
      bi_free(&tmp);
      bi_free(&r2);
    }

    /* 如果当前位为 1, 乘底数: r = r * b mod mod */
    if (bit) {
      bigint_t rb;
      bi_init(&rb, 1);
      bi_mul(&rb, r, &b);
      bigint_t tmp;
      bi_init(&tmp, 1);
      bi_barrett_reduce(&tmp, &rb, mod, &mu);
      bi_copy(r, &tmp);
      bi_free(&tmp);
      bi_free(&rb);
    }
  }

  bi_free(&b);
  bi_free(&mu);
}

/* ---------- 最大公约数 (Binary GCD) ---------- */

/**
 * @brief 二进制 GCD: r = gcd(a, b)
 * @param r 结果 (需预先释放)
 * @param a 第一个数
 * @param b 第二个数
 */
static void bi_gcd(bigint_t *r, const bigint_t *a, const bigint_t *b) {
  if (bi_is_zero(a)) { bi_copy(r, b); return; }
  if (bi_is_zero(b)) { bi_copy(r, a); return; }

  bigint_t u, v;
  bi_init(&u, 1); bi_copy(&u, a);
  bi_init(&v, 1); bi_copy(&v, b);

  /* 计算公共的 2 的幂次 */
  int k = 0;
  while (bi_is_even(&u) && bi_is_even(&v)) {
    bigint_t tu, tv;
    bi_init(&tu, 1); bi_shr(&tu, &u, 1); bi_copy(&u, &tu); bi_free(&tu);
    bi_init(&tv, 1); bi_shr(&tv, &v, 1); bi_copy(&v, &tv); bi_free(&tv);
    k++;
  }

  /* 使 u 为奇数 */
  while (bi_is_even(&u)) {
    bigint_t tu;
    bi_init(&tu, 1); bi_shr(&tu, &u, 1); bi_copy(&u, &tu); bi_free(&tu);
  }

  while (!bi_is_zero(&v)) {
    while (bi_is_even(&v)) {
      bigint_t tv;
      bi_init(&tv, 1); bi_shr(&tv, &v, 1); bi_copy(&v, &tv); bi_free(&tv);
    }
    if (bi_cmp(&u, &v) > 0) {
      bigint_t tmp;
      bi_init(&tmp, 1); bi_sub(&tmp, &u, &v); bi_copy(&u, &tmp); bi_free(&tmp);
    } else {
      bigint_t tmp;
      bi_init(&tmp, 1); bi_sub(&tmp, &v, &u); bi_copy(&v, &tmp); bi_free(&tmp);
    }
  }

  /* r = u * 2^k */
  bi_shl(r, &u, k);
  bi_free(&u); bi_free(&v);
}

/* ---------- 模逆 (Binary Extended GCD) ---------- */

/**
 * @brief 模逆: r = a^(-1) mod m
 *        使用二进制扩展欧几里得算法 (无除法, 仅减法+右移)
 *        要求 gcd(a, m) = 1
 * @param r 结果 a^(-1) mod m (需预先释放)
 * @param a 需要求逆的数
 * @param m 模数 (必须 > 1)
 */
static void bi_mod_inv(bigint_t *r, const bigint_t *a, const bigint_t *m) {
  if (bi_is_one(m)) { bi_set_zero(r); return; }
  if (bi_is_zero(a)) { bi_set_zero(r); return; }

  bigint_t u, v, x1, x2;
  bi_init(&u, 0); bi_copy(&u, a);       /* u = a */
  bi_init(&v, 0); bi_copy(&v, m);       /* v = m */
  bi_init(&x1, 0); bi_set_one(&x1);     /* x1 = 1 */
  bi_init(&x2, 0); bi_set_zero(&x2);    /* x2 = 0 */

  while (!bi_is_one(&u) && !bi_is_zero(&v)) {
    /* 去除 u 中的因子 2 */
    while (bi_is_even(&u)) {
      { bigint_t tu; bi_init(&tu, 0); bi_shr(&tu, &u, 1); bi_copy(&u, &tu); bi_free(&tu); }
      if (bi_is_even(&x1)) {
        bigint_t tx;
        bi_init(&tx, 0); bi_shr(&tx, &x1, 1); bi_copy(&x1, &tx); bi_free(&tx);
      } else {
        bigint_t sum, tx;
        bi_init(&sum, 0); bi_add(&sum, &x1, m);
        bi_init(&tx, 0); bi_shr(&tx, &sum, 1); bi_copy(&x1, &tx);
        bi_free(&tx); bi_free(&sum);
      }
    }

    /* 去除 v 中的因子 2 */
    while (bi_is_even(&v)) {
      { bigint_t tv; bi_init(&tv, 0); bi_shr(&tv, &v, 1); bi_copy(&v, &tv); bi_free(&tv); }
      if (bi_is_even(&x2)) {
        bigint_t tx;
        bi_init(&tx, 0); bi_shr(&tx, &x2, 1); bi_copy(&x2, &tx); bi_free(&tx);
      } else {
        bigint_t sum, tx;
        bi_init(&sum, 0); bi_add(&sum, &x2, m);
        bi_init(&tx, 0); bi_shr(&tx, &sum, 1); bi_copy(&x2, &tx);
        bi_free(&tx); bi_free(&sum);
      }
    }

    if (bi_cmp(&u, &v) >= 0) {
      /* u = u - v */
      { bigint_t tu; bi_init(&tu, 0); bi_sub(&tu, &u, &v); bi_copy(&u, &tu); bi_free(&tu); }
      /* x1 = x1 - x2 (mod m) */
      if (bi_cmp(&x1, &x2) >= 0) {
        bigint_t tx;
        bi_init(&tx, 0); bi_sub(&tx, &x1, &x2); bi_copy(&x1, &tx); bi_free(&tx);
      } else {
        bigint_t diff, tx;
        bi_init(&diff, 0); bi_sub(&diff, &x2, &x1);
        bi_init(&tx, 0); bi_sub(&tx, m, &diff); bi_copy(&x1, &tx);
        bi_free(&tx); bi_free(&diff);
      }
    } else {
      /* v = v - u */
      { bigint_t tv; bi_init(&tv, 0); bi_sub(&tv, &v, &u); bi_copy(&v, &tv); bi_free(&tv); }
      /* x2 = x2 - x1 (mod m) */
      if (bi_cmp(&x2, &x1) >= 0) {
        bigint_t tx;
        bi_init(&tx, 0); bi_sub(&tx, &x2, &x1); bi_copy(&x2, &tx); bi_free(&tx);
      } else {
        bigint_t diff, tx;
        bi_init(&diff, 0); bi_sub(&diff, &x1, &x2);
        bi_init(&tx, 0); bi_sub(&tx, m, &diff); bi_copy(&x2, &tx);
        bi_free(&tx); bi_free(&diff);
      }
    }
  }

  /* 结果: 如果 u == 1 则为 x1, 否则为 x2 */
  if (bi_is_one(&u)) {
    bi_copy(r, &x1);
  } else {
    bi_copy(r, &x2);
  }

  /* 确保 0 <= r < m */
  if (bi_cmp(r, m) >= 0) {
    bigint_t tmp;
    bi_init(&tmp, 0);
    bi_divmod_binary(NULL, &tmp, r, m);
    bi_copy(r, &tmp);
    bi_free(&tmp);
  }

  bi_free(&u); bi_free(&v); bi_free(&x1); bi_free(&x2);
}

/* ---------- Miller-Rabin 素数检测 ---------- */

/**
 * @brief 生成随机大整数, 位数为 bits (最高位确保为 1)
 * @param r    结果 (需预先释放)
 * @param bits 位数
 * @param rng  CSPRNG 状态
 */
static void bi_rand_bits(bigint_t *r, int bits, CSPRNG_State *rng) {
  int nlimbs = (bits + 31) / 32;
  bi_free(r);
  bi_init(r, nlimbs);

  uint8_t *raw = (uint8_t *)malloc((size_t)nlimbs * 4);
  csprng_bytes(rng, raw, (size_t)nlimbs * 4);

  for (int i = 0; i < nlimbs; i++) {
    r->d[i] = ((uint32_t)raw[i*4] << 24) | ((uint32_t)raw[i*4+1] << 16) |
              ((uint32_t)raw[i*4+2] << 8)  |  raw[i*4+3];
  }

  /* 确保最高位为 1 */
  int msb_bit = bits - 1;
  int msb_limb = nlimbs - 1 - (msb_bit / 32);
  int msb_pos  = msb_bit % 32;
  r->d[msb_limb] |= ((uint32_t)1 << msb_pos);

  /* 将高于 bits 的位清零 */
  int extra_bits = nlimbs * 32 - bits;
  if (extra_bits > 0) {
    uint32_t mask = (extra_bits >= 32) ? 0 : (~((uint32_t)0) >> extra_bits);
    r->d[0] &= mask;
  }

  /* 确保为奇数 */
  r->d[nlimbs - 1] |= 1;

  free(raw);
  bi_normalize(r);
}

/* 小素数表用于试除 (前 54 个素数) */
static const uint16_t small_primes[] = {
  2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53,
  59, 61, 67, 71, 73, 79, 83, 89, 97, 101, 103, 107, 109, 113,
  127, 131, 137, 139, 149, 151, 157, 163, 167, 173, 179, 181,
  191, 193, 197, 199, 211, 223, 227, 229, 233, 239, 241, 251
};
#define SMALL_PRIMES_COUNT (sizeof(small_primes) / sizeof(small_primes[0]))

/**
 * @brief 试除检查: 返回 0 如果 n 能被任何小素数整除
 * @param n 待检测的大整数
 * @return 1 通过试除, 0 被某个小素数整除
 */
static int bi_trial_division(const bigint_t *n) {
  bigint_t rem;
  bi_init(&rem, 1);

  for (size_t i = 0; i < SMALL_PRIMES_COUNT; i++) {
    bigint_t p;
    bi_init(&p, 1);
    bi_set_uint32(&p, small_primes[i]);

    /* 跳过素数等于 n 的情况 */
    if (bi_cmp(n, &p) == 0) { bi_free(&p); bi_free(&rem); return 1; }

    bi_divmod_binary(NULL, &rem, n, &p);
    if (bi_is_zero(&rem)) {
      bi_free(&p); bi_free(&rem);
      return 0;  /* 能被小素数整除 */
    }
    bi_free(&p);
  }
  bi_free(&rem);
  return 1;
}

/**
 * @brief Miller-Rabin 素数检测
 * @param n      待检测的大整数 (奇数, > 3)
 * @param rounds 测试轮数 (建议 >= 40)
 * @return 1 很可能是素数, 0 是合数
 */
static int bi_miller_rabin(const bigint_t *n, int rounds) {
  if (bi_is_even(n)) return 0;

  /* n - 1 = 2^s * d, 其中 d 为奇数 */
  bigint_t n_minus_1, d;
  bi_init(&n_minus_1, 1);
  bi_init(&d, 1);

  bigint_t one;
  bi_init(&one, 1); bi_set_one(&one);
  bi_sub(&n_minus_1, n, &one);

  int s = 0;
  bi_copy(&d, &n_minus_1);
  while (bi_is_even(&d)) {
    bigint_t td;
    bi_init(&td, 1); bi_shr(&td, &d, 1); bi_copy(&d, &td); bi_free(&td);
    s++;
  }

  /* 预计算 Barrett μ */
  bigint_t mu;
  bi_init(&mu, 1);
  bi_barrett_mu(&mu, n);

  bigint_t a, x, n_minus_2;
  bi_init(&n_minus_2, 1);
  bigint_t two;
  bi_init(&two, 1); bi_set_uint32(&two, 2);
  bi_sub(&n_minus_2, n, &two);

  bi_init(&a, 1);
  bi_init(&x, 1);

  CSPRNG_State *rng = get_rsa_csprng();

  for (int round = 0; round < rounds; round++) {
    /* 随机选择 a ∈ [2, n-2] */
    int nbits = bi_nbits(n);
    int found = 0;
    int retries = 0;
    while (!found && retries < 100) {
      bi_rand_bits(&a, nbits, rng);
      /* 确保 a < n-1 */
      if (bi_cmp(&a, &n_minus_1) >= 0) {
        /* a = a mod (n-1) */
        bigint_t tmp;
        bi_init(&tmp, 1);
        bi_divmod_binary(NULL, &tmp, &a, &n_minus_1);
        bi_copy(&a, &tmp);
        bi_free(&tmp);
      }
      /* 确保 a >= 2 */
      if (bi_cmp(&a, &two) >= 0) found = 1;
      retries++;
    }
    if (!found) continue;

    /* x = a^d mod n */
    bi_mod_exp(&x, &a, &d, n);

    if (bi_is_one(&x) || bi_cmp(&x, &n_minus_1) == 0) continue;

    int is_composite = 1;
    for (int r = 0; r < s - 1; r++) {
      /* x = x^2 mod n */
      bigint_t x2;
      bi_init(&x2, 1);
      bi_mul(&x2, &x, &x);

      bigint_t tmp;
      bi_init(&tmp, 1);
      bi_barrett_reduce(&tmp, &x2, n, &mu);
      bi_copy(&x, &tmp);
      bi_free(&tmp);
      bi_free(&x2);

      if (bi_cmp(&x, &n_minus_1) == 0) { is_composite = 0; break; }
    }

    if (is_composite) {
      bi_free(&n_minus_1); bi_free(&d); bi_free(&one);
      bi_free(&mu); bi_free(&n_minus_2); bi_free(&two);
      bi_free(&a); bi_free(&x);
      return 0;
    }
  }

  bi_free(&n_minus_1); bi_free(&d); bi_free(&one);
  bi_free(&mu); bi_free(&n_minus_2); bi_free(&two);
  bi_free(&a); bi_free(&x);
  return 1;
}

/**
 * @brief 全面素数检测 (试除 + Miller-Rabin)
 * @param n      待检测的大整数
 * @param rounds Miller-Rabin 轮数
 * @return 1 很可能是素数, 0 合数
 */
static int bi_is_prime(const bigint_t *n, int rounds) {
  if (bi_is_zero(n)) return 0;
  if (bi_is_even(n)) {
    /* 检查是否为 2 */
    bigint_t two;
    bi_init(&two, 1); bi_set_uint32(&two, 2);
    int is_two = (bi_cmp(n, &two) == 0);
    bi_free(&two);
    if (is_two) return 1;
    return 0;
  }

  /* 试除小素数 */
  if (!bi_trial_division(n)) return 0;

  /* Miller-Rabin */
  return bi_miller_rabin(n, rounds);
}

/* ---------- 生成随机素数 ---------- */

/**
 * @brief 生成指定位数的随机素数
 * @param r     结果 (需预先释放)
 * @param bits  素数位数
 * @param rng   CSPRNG 状态
 * @param rounds Miller-Rabin 检测轮数
 */
static void bi_rand_prime(bigint_t *r, int bits, CSPRNG_State *rng, int rounds) {
  int attempts = 0;
  while (1) {
    bi_rand_bits(r, bits, rng);
    /* 确保奇数 */
    r->d[r->n - 1] |= 1;
    bi_normalize(r);

    if (bi_is_prime(r, rounds)) break;
    attempts++;
    if (attempts > 1000) {
      /* 重新初始化随机数生成器以防种子偏差 */
      csprng_reseed(rng, (uint64_t)time(NULL) + (uint64_t)attempts);
    }
  }
}

/* ---------- 十六进制转换 ---------- */

/**
 * @brief 从大整数中提取单个比特值
 * @param a       大整数指针
 * @param bit_pos 位位置 (0 = LSB)
 * @return 0 或 1
 */
static int bi_get_bit(const bigint_t *a, int bit_pos) {
  if (bit_pos < 0) return 0;
  int limb_idx = a->n - 1 - (bit_pos / 32);
  int shift    = bit_pos % 32;
  if (limb_idx >= 0 && limb_idx < a->n)
    return (int)((a->d[limb_idx] >> shift) & 1);
  return 0;
}

/**
 * @brief 在大整数中设置单个比特值
 * @param a       大整数指针
 * @param bit_pos 位位置 (0 = LSB)
 * @param val     值 (0 或 1)
 */
static void bi_set_bit(bigint_t *a, int bit_pos, int val) {
  if (bit_pos < 0 || !val) return;
  int limb_idx = a->n - 1 - (bit_pos / 32);
  int shift    = bit_pos % 32;
  if (limb_idx >= 0 && limb_idx < a->n)
    a->d[limb_idx] |= ((uint32_t)1 << shift);
}

/**
 * @brief 将大整数转换为十六进制字符串 (小写, 无前导零)
 *        逐比特安全提取, 正确处理跨 limb 边界
 * @param a   大整数指针
 * @param len 输出字符串长度指针
 * @return 十六进制字符串 (调用者需 free)
 */
static char* bi_to_hex(const bigint_t *a, size_t *len) {
  if (bi_is_zero(a)) {
    char *s = (char *)malloc(2);
    s[0] = '0'; s[1] = '\0';
    if (len) *len = 1;
    return s;
  }

  static const char hex_chars[] = "0123456789abcdef";
  int bits = bi_nbits(a);
  int hex_len = (bits + 3) / 4;
  char *hex = (char *)malloc((size_t)hex_len + 1);
  if (!hex) return NULL;

  /* hex[i] (i=0 是最高位十六进制字符) 对应值比特位:
     bit_hi = bits - 1 - i*4,  bit_lo = bit_hi - 3 */
  for (int i = 0; i < hex_len; i++) {
    int nibble = 0;
    for (int j = 0; j < 4; j++) {
      int bit_pos = bits - 1 - i * 4 - j;
      if (bit_pos < 0) continue;
      if (bi_get_bit(a, bit_pos))
        nibble |= (1 << (3 - j));
    }
    hex[i] = hex_chars[nibble];
  }
  hex[hex_len] = '\0';
  if (len) *len = (size_t)hex_len;
  return hex;
}

/**
 * @brief 十六进制字符转数值
 */
static int hex_digit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return 0;
}

/**
 * @brief 从十六进制字符串解析大整数
 *        逐比特安全设置, 正确处理跨 limb 边界
 * @param a   结果 (需预先释放)
 * @param hex 十六进制字符串 (小写或大写)
 * @param len 字符串长度
 */
static void bi_from_hex(bigint_t *a, const char *hex, size_t len) {
  bi_free(a);

  /* 跳过前导零 */
  while (len > 1 && *hex == '0') { hex++; len--; }

  int total_bits = (int)len * 4;
  int nlimbs = (total_bits + 31) / 32;
  bi_init(a, nlimbs);

  /* hex[0] 是最高位十六进制字符, 对应值的高 4 位 */
  for (size_t i = 0; i < len; i++) {
    int nibble = hex_digit(hex[i]);
    for (int j = 0; j < 4; j++) {
      if (nibble & (1 << (3 - j))) {
        int bit_pos = total_bits - 1 - (int)i * 4 - j;
        bi_set_bit(a, bit_pos, 1);
      }
    }
  }

  bi_normalize(a);
}

/**
 * @brief 将大整数转换为大端序字节数组 (逐比特安全提取)
 * @param a       大整数指针
 * @param out     输出缓冲区 (调用者分配, 至少 out_len 字节)
 * @param out_len 输出字节长度 (前导零填充)
 */
static void bi_to_bytes(const bigint_t *a, uint8_t *out, int out_len) {
  int bits = bi_nbits(a);
  int byte_len = (bits + 7) / 8;
  if (byte_len > out_len) byte_len = out_len;
  (void)memset(out, 0, (size_t)out_len);

  /* 字节从 MSB 到 LSB: out[out_len - byte_len + i] (i=0 是最高有效字节) */
  int out_offset = out_len - byte_len;

  for (int i = 0; i < byte_len; i++) {
    uint8_t byte_val = 0;
    for (int j = 0; j < 8; j++) {
      int bit_pos = bits - 1 - i * 8 - j;
      if (bit_pos < 0) continue;
      if (bi_get_bit(a, bit_pos))
        byte_val |= (uint8_t)(1 << (7 - j));
    }
    out[out_offset + i] = byte_val;
  }
}

/**
 * @brief 从大端序字节数组解析大整数 (逐比特安全设置)
 * @param a    结果 (需预先释放)
 * @param data 字节数据 (MSB first)
 * @param len  字节长度
 */
static void bi_from_bytes(bigint_t *a, const uint8_t *data, int len) {
  int total_bits = len * 8;
  int nlimbs = (total_bits + 31) / 32;
  bi_free(a);
  bi_init(a, nlimbs);

  for (int i = 0; i < len; i++) {
    uint8_t byte_val = data[i];
    for (int j = 0; j < 8; j++) {
      if (byte_val & (1 << (7 - j))) {
        int bit_pos = total_bits - 1 - i * 8 - j;
        bi_set_bit(a, bit_pos, 1);
      }
    }
  }
  bi_normalize(a);
}

/* ========================================================================
 * Part 2: RSA 密钥结构与操作
 * ======================================================================== */

/**
 * @brief RSA 公钥结构
 */
typedef struct {
  int       bits;      /* 密钥位数 */
  bigint_t  n;         /* 模数 n */
  bigint_t  e;         /* 公钥指数 e */
} rsa_public_key_t;

/**
 * @brief RSA 私钥结构 (含 CRT 加速分量)
 */
typedef struct {
  int       bits;      /* 密钥位数 */
  bigint_t  n;         /* 模数 n */
  bigint_t  e;         /* 公钥指数 e */
  bigint_t  d;         /* 私钥指数 d */
  bigint_t  p;         /* 素数 p (CRT) */
  bigint_t  q;         /* 素数 q (CRT) */
  bigint_t  dp;        /* d mod (p-1) (CRT) */
  bigint_t  dq;        /* d mod (q-1) (CRT) */
  bigint_t  qinv;      /* q^(-1) mod p (CRT) */
} rsa_private_key_t;

/* ---------- 密钥初始化与释放 ---------- */

/**
 * @brief 初始化 RSA 公钥
 * @param key 公钥指针
 */
static void rsa_public_key_init(rsa_public_key_t *key) {
  key->bits = 0;
  bi_init(&key->n, 0);
  bi_init(&key->e, 0);
}

/**
 * @brief 释放 RSA 公钥内存
 * @param key 公钥指针
 */
static void rsa_public_key_free(rsa_public_key_t *key) {
  bi_free(&key->n);
  bi_free(&key->e);
  key->bits = 0;
}

/**
 * @brief 初始化 RSA 私钥
 * @param key 私钥指针
 */
static void rsa_private_key_init(rsa_private_key_t *key) {
  key->bits = 0;
  bi_init(&key->n, 0);
  bi_init(&key->e, 0);
  bi_init(&key->d, 0);
  bi_init(&key->p, 0);
  bi_init(&key->q, 0);
  bi_init(&key->dp, 0);
  bi_init(&key->dq, 0);
  bi_init(&key->qinv, 0);
}

/**
 * @brief 释放 RSA 私钥内存
 * @param key 私钥指针
 */
static void rsa_private_key_free(rsa_private_key_t *key) {
  bi_free(&key->n);
  bi_free(&key->e);
  bi_free(&key->d);
  bi_free(&key->p);
  bi_free(&key->q);
  bi_free(&key->dp);
  bi_free(&key->dq);
  bi_free(&key->qinv);
  key->bits = 0;
}

/* ---------- RSA 密钥生成 ---------- */

#define RSA_DEFAULT_E  65537

/**
 * @brief 生成 RSA 密钥对
 * @param pub  输出的公钥 (需已初始化)
 * @param priv 输出的私钥 (需已初始化)
 * @param bits 密钥位数 (如 1024, 2048, 4096)
 * @param rng  CSPRNG 状态
 */
static void rsa_keygen(rsa_public_key_t *pub, rsa_private_key_t *priv,
                       int bits, CSPRNG_State *rng) {
  int pbits = bits / 2;
  int qbits = bits - pbits;

  bigint_t p, q, n, phi, e, d, one;
  bigint_t p_minus_1, q_minus_1;

  bi_init(&p, 0); bi_init(&q, 0);
  bi_init(&n, 0); bi_init(&phi, 0);
  bi_init(&e, 0); bi_init(&d, 0);
  bi_init(&one, 0); bi_set_one(&one);
  bi_init(&p_minus_1, 0); bi_init(&q_minus_1, 0);

  /* 生成素数 p */
  bi_rand_prime(&p, pbits, rng, 40);

  /* 生成素数 q, 确保 p != q */
  do {
    bi_rand_prime(&q, qbits, rng, 40);
  } while (bi_cmp(&p, &q) == 0);

  /* n = p * q */
  bi_mul(&n, &p, &q);
  int actual_bits = bi_nbits(&n);

  /* phi = (p-1) * (q-1) */
  bigint_t pm1, qm1;
  bi_init(&pm1, 0); bi_init(&qm1, 0);
  bi_sub(&pm1, &p, &one);
  bi_sub(&qm1, &q, &one);
  bi_mul(&phi, &pm1, &qm1);

  /* e = 65537 */
  bi_set_uint32(&e, RSA_DEFAULT_E);

  /* 验证 gcd(e, phi) == 1 */
  bigint_t g;
  bi_init(&g, 0);
  bi_gcd(&g, &e, &phi);

  /* 如果 gcd != 1, 重新生成 q (极端罕见) */
  int retry = 0;
  while (!bi_is_one(&g) && retry < 10) {
    bi_rand_prime(&q, qbits, rng, 40);
    bi_mul(&n, &p, &q);
    bi_sub(&qm1, &q, &one);
    bi_mul(&phi, &pm1, &qm1);
    bi_gcd(&g, &e, &phi);
    retry++;
  }

  /* d = e^(-1) mod phi */
  bi_mod_inv(&d, &e, &phi);

  /* 填充公钥 */
  pub->bits = actual_bits;
  bi_copy(&pub->n, &n);
  bi_copy(&pub->e, &e);

  /* 填充私钥 */
  priv->bits = actual_bits;
  bi_copy(&priv->n, &n);
  bi_copy(&priv->e, &e);
  bi_copy(&priv->d, &d);
  bi_copy(&priv->p, &p);
  bi_copy(&priv->q, &q);

  /* CRT 分量: dp = d mod (p-1), dq = d mod (q-1), qinv = q^(-1) mod p */
  bi_sub(&p_minus_1, &p, &one);
  bi_sub(&q_minus_1, &q, &one);

  bigint_t tmp;
  bi_init(&tmp, 0);
  bi_divmod_binary(NULL, &tmp, &d, &p_minus_1);
  bi_copy(&priv->dp, &tmp);
  bi_divmod_binary(NULL, &tmp, &d, &q_minus_1);
  bi_copy(&priv->dq, &tmp);
  bi_mod_inv(&tmp, &q, &p);
  bi_copy(&priv->qinv, &tmp);

  /* 清理 */
  bi_free(&tmp); bi_free(&g);
  bi_free(&pm1); bi_free(&qm1);
  bi_free(&p_minus_1); bi_free(&q_minus_1);
  bi_free(&p); bi_free(&q); bi_free(&n); bi_free(&phi);
  bi_free(&e); bi_free(&d); bi_free(&one);
}

/* ---------- 从组件构造密钥 ---------- */

/**
 * @brief 从十六进制组件构造 RSA 密钥对
 * @param pub    输出的公钥 (需已初始化)
 * @param priv   输出的私钥 (需已初始化)
 * @param n_hex  模数 n 的十六进制字符串
 * @param e_hex  公钥指数 e 的十六进制字符串
 * @param d_hex  私钥指数 d 的十六进制字符串 (可为 NULL)
 */
static void rsa_from_components(rsa_public_key_t *pub, rsa_private_key_t *priv,
                                const char *n_hex, const char *e_hex,
                                const char *d_hex) {
  bi_from_hex(&pub->n, n_hex, strlen(n_hex));
  bi_from_hex(&pub->e, e_hex, strlen(e_hex));
  pub->bits = bi_nbits(&pub->n);

  bi_copy(&priv->n, &pub->n);
  bi_copy(&priv->e, &pub->e);
  priv->bits = pub->bits;

  if (d_hex) {
    bi_from_hex(&priv->d, d_hex, strlen(d_hex));
  }
}

/* ---------- RSA 裸操作 (m^e mod n) ---------- */

/**
 * @brief RSA 裸加密/解密操作: result = msg^exp mod n
 * @param result  输出 (需已初始化)
 * @param msg     消息 (大整数)
 * @param exp     指数
 * @param n       模数
 */
static void rsa_raw(bigint_t *result, const bigint_t *msg,
                    const bigint_t *exp, const bigint_t *n) {
  bi_mod_exp(result, msg, exp, n);
}

/**
 * @brief RSA-CRT 解密: 使用中国剩余定理加速
 * @param result 输出 (需已初始化)
 * @param msg    密文 (大整数)
 * @param priv   私钥 (含 CRT 分量)
 */
static void rsa_crt_decrypt(bigint_t *result, const bigint_t *msg,
                            const rsa_private_key_t *priv) {
  /* m1 = c^dp mod p */
  bigint_t m1, m2, h;
  bi_init(&m1, 0);
  bi_init(&m2, 0);
  bi_init(&h, 0);

  bi_mod_exp(&m1, msg, &priv->dp, &priv->p);
  bi_mod_exp(&m2, msg, &priv->dq, &priv->q);

  /* h = (m1 - m2) * qinv mod p */
  bigint_t diff;
  bi_init(&diff, 0);

  if (bi_cmp(&m1, &m2) >= 0) {
    bi_sub(&diff, &m1, &m2);
  } else {
    /* m1 < m2, 需要加上 p */
    bigint_t tmp;
    bi_init(&tmp, 0);
    bi_add(&tmp, &m1, &priv->p);
    bi_sub(&diff, &tmp, &m2);
    bi_free(&tmp);
  }

  bi_mul(&h, &diff, &priv->qinv);

  /* h = h mod p */
  bigint_t h_mod;
  bi_init(&h_mod, 0);
  bi_divmod_binary(NULL, &h_mod, &h, &priv->p);
  bi_copy(&h, &h_mod);
  bi_free(&h_mod);

  /* m = m2 + h * q */
  bigint_t hq;
  bi_init(&hq, 0);
  bi_mul(&hq, &h, &priv->q);
  bi_add(result, &m2, &hq);

  bi_free(&m1); bi_free(&m2); bi_free(&h);
  bi_free(&diff); bi_free(&hq);
}

/* ========================================================================
 * Part 3: PKCS#1 v1.5 填充
 * ======================================================================== */

/* SHA-256 的 DigestInfo 前缀 (DER 编码) */
static const uint8_t SHA256_DIGEST_PREFIX[] = {
  0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86,
  0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01, 0x05,
  0x00, 0x04, 0x20
};
#define SHA256_DIGEST_PREFIX_LEN  19

/**
 * @brief PKCS#1 v1.5 加密填充 (类型 2)
 *        格式: 0x00 0x02 [非零随机字节] 0x00 [数据]
 * @param out      输出缓冲区 (调用者分配, 大小为 k 字节)
 * @param k        密钥模数字节数
 * @param data     原始数据
 * @param data_len 数据长度
 * @param rng      CSPRNG 状态
 * @return 实际填充后的长度 (= k), 失败返回 -1
 */
static int pkcs1_encrypt_pad(uint8_t *out, int k, const uint8_t *data,
                             int data_len, CSPRNG_State *rng) {
  int ps_len = k - data_len - 3;
  if (ps_len < 8) return -1;  /* 消息太长 */

  out[0] = 0x00;
  out[1] = 0x02;

  /* 生成非零随机填充字节 */
  uint8_t *ps = out + 2;
  csprng_bytes(rng, ps, (size_t)ps_len);
  for (int i = 0; i < ps_len; i++) {
    while (ps[i] == 0) {
      csprng_bytes(rng, ps + i, 1);
    }
  }

  out[2 + ps_len] = 0x00;
  memcpy(out + 2 + ps_len + 1, data, (size_t)data_len);
  return k;
}

/**
 * @brief PKCS#1 v1.5 解密去填充
 * @param out      输出缓冲区 (调用者分配)
 * @param out_len  输出缓冲区大小
 * @param data     填充后的数据
 * @param k        密钥模数字节数
 * @return 实际数据长度, 失败返回 -1
 */
static int pkcs1_decrypt_unpad(uint8_t *out, int out_len,
                               const uint8_t *data, int k) {
  if (k < 11) return -1;
  if (data[0] != 0x00 || data[1] != 0x02) return -1;

  /* 查找分隔符 0x00 */
  int sep = -1;
  for (int i = 2; i < k; i++) {
    if (data[i] == 0x00) { sep = i; break; }
  }
  if (sep < 10) return -1;  /* 至少 8 字节随机填充 */

  int msg_len = k - sep - 1;
  if (msg_len > out_len) return -1;
  memcpy(out, data + sep + 1, (size_t)msg_len);
  return msg_len;
}

/**
 * @brief PKCS#1 v1.5 签名填充 (类型 1, SHA-256)
 *        格式: 0x00 0x01 [0xFF 字节] 0x00 [DigestInfo]
 * @param out      输出缓冲区 (调用者分配, 大小为 k 字节)
 * @param k        密钥模数字节数
 * @param data     待签名数据
 * @param data_len 数据长度
 * @return 实际填充后的长度 (= k), 失败返回 -1
 */
static int pkcs1_sign_pad(uint8_t *out, int k, const uint8_t *data,
                          int data_len) {
  /* DigestInfo = prefix || SHA256(data) */
  uint8_t digest[SHA256_DIGEST_SIZE];
  SHA256(data, (size_t)data_len, digest);

  int di_len = SHA256_DIGEST_PREFIX_LEN + SHA256_DIGEST_SIZE;
  int ps_len = k - di_len - 3;
  if (ps_len < 8) return -1;

  out[0] = 0x00;
  out[1] = 0x01;
  memset(out + 2, 0xFF, (size_t)ps_len);
  out[2 + ps_len] = 0x00;
  memcpy(out + 2 + ps_len + 1, SHA256_DIGEST_PREFIX, SHA256_DIGEST_PREFIX_LEN);
  memcpy(out + 2 + ps_len + 1 + SHA256_DIGEST_PREFIX_LEN, digest, SHA256_DIGEST_SIZE);
  return k;
}

/**
 * @brief PKCS#1 v1.5 验签去填充 (SHA-256)
 * @param data     原始数据
 * @param data_len 原始数据长度
 * @param em       解密后的填充消息 (k 字节)
 * @param k        密钥模数字节数
 * @return 1 验证通过, 0 验证失败
 */
static int pkcs1_verify_unpad(const uint8_t *data, int data_len,
                              const uint8_t *em, int k) {
  if (k < 11) return 0;
  if (em[0] != 0x00 || em[1] != 0x01) return 0;

  /* 查找分隔符 0x00 */
  int sep = -1;
  for (int i = 2; i < k; i++) {
    if (em[i] == 0x00) { sep = i; break; }
  }
  if (sep < 10) return 0;

  /* 验证填充字节都是 0xFF */
  for (int i = 2; i < sep; i++) {
    if (em[i] != 0xFF) return 0;
  }

  /* 验证 DigestInfo */
  int di_offset = sep + 1;
  int di_len = k - di_offset;
  int expected_di_len = SHA256_DIGEST_PREFIX_LEN + SHA256_DIGEST_SIZE;

  if (di_len != expected_di_len) return 0;
  if (memcmp(em + di_offset, SHA256_DIGEST_PREFIX, SHA256_DIGEST_PREFIX_LEN) != 0)
    return 0;

  /* 验证 SHA-256 哈希 */
  uint8_t expected_digest[SHA256_DIGEST_SIZE];
  SHA256((const uint8_t *)data, (size_t)data_len, expected_digest);

  if (memcmp(em + di_offset + SHA256_DIGEST_PREFIX_LEN,
             expected_digest, SHA256_DIGEST_SIZE) != 0)
    return 0;

  return 1;
}

/* ========================================================================
 * Part 4: Base64 编码 (用于 PEM)
 * ======================================================================== */

static const char b64_table[] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/**
 * @brief Base64 编码
 * @param out      输出缓冲区 (调用者分配, 大小 = ((in_len+2)/3)*4 + 1)
 * @param in       输入数据
 * @param in_len   输入长度
 * @return 编码后字符串长度 (不含末尾 null)
 */
static int base64_encode(char *out, const uint8_t *in, int in_len) {
  int out_len = 0;
  for (int i = 0; i < in_len; i += 3) {
    int remaining = in_len - i;
    uint32_t triple = 0;

    triple |= ((uint32_t)in[i]) << 16;
    if (remaining > 1) triple |= ((uint32_t)in[i + 1]) << 8;
    if (remaining > 2) triple |= (uint32_t)in[i + 2];

    out[out_len++] = b64_table[(triple >> 18) & 0x3F];
    out[out_len++] = b64_table[(triple >> 12) & 0x3F];
    out[out_len++] = (remaining > 1) ? b64_table[(triple >> 6) & 0x3F] : '=';
    out[out_len++] = (remaining > 2) ? b64_table[triple & 0x3F] : '=';
  }
  out[out_len] = '\0';
  return out_len;
}

/* ========================================================================
 * Part 5: Lua 绑定 - 辅助函数
 * ======================================================================== */

/**
 * @brief 将 bigint_t 以十六进制字符串推入 Lua 栈
 * @param L Lua 状态
 * @param a 大整数指针
 */
static void push_bigint_hex(lua_State *L, const bigint_t *a) {
  size_t len;
  char *hex = bi_to_hex(a, &len);
  if (hex) {
    lua_pushlstring(L, hex, len);
    free(hex);
  } else {
    lua_pushstring(L, "0");
  }
}

/**
 * @brief 将 bigint_t 以大端序字节推入 Lua 栈
 * @param L     Lua 状态
 * @param a     大整数指针
 * @param k     期望的字节长度 (填充前导零)
 */
static void push_bigint_bytes(lua_State *L, const bigint_t *a, int k) {
  uint8_t *buf = (uint8_t *)malloc((size_t)k);
  if (!buf) luaL_error(L, "RSA: memory allocation failed");
  bi_to_bytes(a, buf, k);
  lua_pushlstring(L, (const char *)buf, (size_t)k);
  free(buf);
}

/**
 * @brief 从 Lua 栈读取 bigint_t (十六进制字符串)
 * @param L   Lua 状态
 * @param idx 栈索引
 * @param a   输出大整数 (需已初始化)
 */
static void get_bigint_hex(lua_State *L, int idx, bigint_t *a) {
  size_t len;
  const char *hex = luaL_checklstring(L, idx, &len);
  bi_from_hex(a, hex, len);
}

/**
 * @brief 从 Lua 栈读取 bigint_t (原始字节)
 * @param L   Lua 状态
 * @param idx 栈索引
 * @param a   输出大整数 (需已初始化)
 */
static void get_bigint_bytes(lua_State *L, int idx, bigint_t *a) {
  size_t len;
  const char *data = luaL_checklstring(L, idx, &len);
  bi_from_bytes(a, (const uint8_t *)data, (int)len);
}

/**
 * @brief 将 RSA 公钥推为 Lua 表: { bits=N, n="hex", e="hex" }
 * @param L   Lua 状态
 * @param pub 公钥指针
 */
static void push_public_key_table(lua_State *L, const rsa_public_key_t *pub) {
  lua_newtable(L);
  lua_pushinteger(L, pub->bits);
  lua_setfield(L, -2, "bits");
  push_bigint_hex(L, &pub->n);
  lua_setfield(L, -2, "n");
  push_bigint_hex(L, &pub->e);
  lua_setfield(L, -2, "e");
}

/**
 * @brief 将 RSA 私钥推为 Lua 表: { bits=N, n="hex", e="hex", d="hex", p="hex", q="hex" }
 * @param L    Lua 状态
 * @param priv 私钥指针
 */
static void push_private_key_table(lua_State *L, const rsa_private_key_t *priv) {
  lua_newtable(L);
  lua_pushinteger(L, priv->bits);
  lua_setfield(L, -2, "bits");
  push_bigint_hex(L, &priv->n);
  lua_setfield(L, -2, "n");
  push_bigint_hex(L, &priv->e);
  lua_setfield(L, -2, "e");
  push_bigint_hex(L, &priv->d);
  lua_setfield(L, -2, "d");
  push_bigint_hex(L, &priv->p);
  lua_setfield(L, -2, "p");
  push_bigint_hex(L, &priv->q);
  lua_setfield(L, -2, "q");
}

/**
 * @brief 从 Lua 表解析 RSA 公钥
 * @param L   Lua 状态
 * @param idx 表在栈中的索引
 * @param pub 输出公钥 (需已初始化, 会被覆盖)
 */
static void get_public_key_from_table(lua_State *L, int idx, rsa_public_key_t *pub) {
  lua_getfield(L, idx, "n");
  get_bigint_hex(L, -1, &pub->n);
  lua_pop(L, 1);

  lua_getfield(L, idx, "e");
  get_bigint_hex(L, -1, &pub->e);
  lua_pop(L, 1);

  lua_getfield(L, idx, "bits");
  pub->bits = (int)luaL_optinteger(L, -1, 0);
  lua_pop(L, 1);

  if (pub->bits == 0) pub->bits = bi_nbits(&pub->n);
}

/**
 * @brief 从 Lua 表解析 RSA 私钥
 * @param L    Lua 状态
 * @param idx  表在栈中的索引
 * @param priv 输出私钥 (需已初始化, 会被覆盖)
 */
static void get_private_key_from_table(lua_State *L, int idx, rsa_private_key_t *priv) {
  lua_getfield(L, idx, "n");
  get_bigint_hex(L, -1, &priv->n);
  lua_pop(L, 1);

  lua_getfield(L, idx, "e");
  get_bigint_hex(L, -1, &priv->e);
  lua_pop(L, 1);

  lua_getfield(L, idx, "d");
  get_bigint_hex(L, -1, &priv->d);
  lua_pop(L, 1);

  /* p, q 为可选 (CRT 加速) */
  lua_getfield(L, idx, "p");
  if (!lua_isnil(L, -1)) get_bigint_hex(L, -1, &priv->p);
  lua_pop(L, 1);

  lua_getfield(L, idx, "q");
  if (!lua_isnil(L, -1)) get_bigint_hex(L, -1, &priv->q);
  lua_pop(L, 1);

  lua_getfield(L, idx, "bits");
  priv->bits = (int)luaL_optinteger(L, -1, 0);
  lua_pop(L, 1);

  if (priv->bits == 0) priv->bits = bi_nbits(&priv->n);

  /* 如果提供了 p, q, 补全 CRT 分量 */
  if (!bi_is_zero(&priv->p) && !bi_is_zero(&priv->q)) {
    bigint_t one, pm1, qm1, tmp;
    bi_init(&one, 0); bi_set_one(&one);
    bi_init(&pm1, 0); bi_init(&qm1, 0);
    bi_init(&tmp, 0);

    bi_sub(&pm1, &priv->p, &one);
    bi_sub(&qm1, &priv->q, &one);
    bi_divmod_binary(NULL, &tmp, &priv->d, &pm1);
    bi_copy(&priv->dp, &tmp);
    bi_divmod_binary(NULL, &tmp, &priv->d, &qm1);
    bi_copy(&priv->dq, &tmp);
    bi_mod_inv(&tmp, &priv->q, &priv->p);
    bi_copy(&priv->qinv, &tmp);

    bi_free(&one); bi_free(&pm1); bi_free(&qm1); bi_free(&tmp);
  }
}

/* ========================================================================
 * Part 6: Lua 绑定 - rsa.key 子表
 * ======================================================================== */

/**
 * @brief rsa.key.generate(bits) -> {public_key, private_key}
 *        生成指定位数的 RSA 密钥对
 * @param bits 密钥位数 (如 1024, 2048, 4096)
 */
static int l_rsa_key_generate(lua_State *L) {
  int bits = (int)luaL_checkinteger(L, 1);

  /* 验证位数 */
  if (bits < 32 || bits > 4096)
    luaL_error(L, "RSA key.generate: bits must be between 32 and 4096");
  if (bits % 32 != 0)
    luaL_error(L, "RSA key.generate: bits must be a multiple of 32 (e.g., 1024, 2048, 4096)");

  rsa_public_key_t  pub;
  rsa_private_key_t priv;
  rsa_public_key_init(&pub);
  rsa_private_key_init(&priv);

  rsa_keygen(&pub, &priv, bits, get_rsa_csprng());

  /* 返回 {public_key, private_key} */
  lua_newtable(L);

  push_public_key_table(L, &pub);
  lua_setfield(L, -2, "public_key");

  push_private_key_table(L, &priv);
  lua_setfield(L, -2, "private_key");

  rsa_public_key_free(&pub);
  rsa_private_key_free(&priv);
  return 1;
}

/**
 * @brief rsa.key.from_components(n_hex, e_hex [, d_hex]) -> {public_key, private_key}
 *        从原始组件构造密钥对
 * @param n_hex 模数 n 的十六进制字符串
 * @param e_hex 公钥指数 e 的十六进制字符串
 * @param d_hex 私钥指数 d 的十六进制字符串 (可选)
 */
static int l_rsa_key_from_components(lua_State *L) {
  size_t n_len, e_len;
  const char *n_hex = luaL_checklstring(L, 1, &n_len);
  const char *e_hex = luaL_checklstring(L, 2, &e_len);
  const char *d_hex = NULL;

  if (lua_gettop(L) >= 3 && !lua_isnil(L, 3)) {
    d_hex = luaL_checklstring(L, 3, NULL);
  }

  rsa_public_key_t  pub;
  rsa_private_key_t priv;
  rsa_public_key_init(&pub);
  rsa_private_key_init(&priv);

  rsa_from_components(&pub, &priv, n_hex, e_hex, d_hex);

  /* 返回 {public_key, private_key} */
  lua_newtable(L);

  push_public_key_table(L, &pub);
  lua_setfield(L, -2, "public_key");

  push_private_key_table(L, &priv);
  lua_setfield(L, -2, "private_key");

  rsa_public_key_free(&pub);
  rsa_private_key_free(&priv);
  return 1;
}

static const luaL_Reg rsa_key_funcs[] = {
  {"generate",        l_rsa_key_generate},
  {"from_components", l_rsa_key_from_components},
  {NULL, NULL}
};

/* ========================================================================
 * Part 7: Lua 绑定 - rsa.public 子表
 * ======================================================================== */

/**
 * @brief rsa.public.encrypt(data, pubkey) -> encrypted_bytes
 *        使用 PKCS#1 v1.5 填充加密数据
 * @param data   原始数据 (Lua 字符串)
 * @param pubkey 公钥表 { n=hex, e=hex, bits=N }
 */
static int l_rsa_public_encrypt(lua_State *L) {
  size_t data_len;
  const char *data = luaL_checklstring(L, 1, &data_len);

  rsa_public_key_t pub;
  rsa_public_key_init(&pub);
  get_public_key_from_table(L, 2, &pub);

  int k = (pub.bits + 7) / 8;  /* 模数字节数 */

  /* 检查消息长度 */
  if ((int)data_len > k - 11)
    luaL_error(L, "RSA public.encrypt: data too long (max %d bytes for %d-bit key)",
               k - 11, pub.bits);

  /* PKCS#1 v1.5 填充 */
  uint8_t *padded = (uint8_t *)malloc((size_t)k);
  if (!padded) {
    rsa_public_key_free(&pub);
    luaL_error(L, "RSA: memory allocation failed");
  }

  int pad_len = pkcs1_encrypt_pad(padded, k, (const uint8_t *)data,
                                  (int)data_len, get_rsa_csprng());
  if (pad_len < 0) {
    free(padded);
    rsa_public_key_free(&pub);
    luaL_error(L, "RSA public.encrypt: padding failed (data too long)");
  }

  /* 将填充数据转为大整数 */
  bigint_t msg;
  bi_init(&msg, 0);
  bi_from_bytes(&msg, padded, k);
  free(padded);

  /* RSA 加密: cipher = msg^e mod n */
  bigint_t cipher;
  bi_init(&cipher, 0);
  rsa_raw(&cipher, &msg, &pub.e, &pub.n);

  /* 输出密文字节 */
  push_bigint_bytes(L, &cipher, k);

  bi_free(&msg);
  bi_free(&cipher);
  rsa_public_key_free(&pub);
  return 1;
}

/**
 * @brief rsa.public.verify(data, signature, pubkey) -> boolean
 *        验证 PKCS#1 v1.5 签名
 * @param data      原始数据
 * @param signature 签名数据 (k 字节)
 * @param pubkey    公钥表
 */
static int l_rsa_public_verify(lua_State *L) {
  size_t data_len, sig_len;
  const char *data = luaL_checklstring(L, 1, &data_len);
  const char *sig  = luaL_checklstring(L, 2, &sig_len);

  rsa_public_key_t pub;
  rsa_public_key_init(&pub);
  get_public_key_from_table(L, 3, &pub);

  int k = (pub.bits + 7) / 8;
  if ((int)sig_len != k) {
    rsa_public_key_free(&pub);
    lua_pushboolean(L, 0);
    return 1;
  }

  /* RSA 验证: em = sig^e mod n */
  bigint_t sig_bi, em_bi;
  bi_init(&sig_bi, 0);
  bi_init(&em_bi, 0);
  bi_from_bytes(&sig_bi, (const uint8_t *)sig, k);
  rsa_raw(&em_bi, &sig_bi, &pub.e, &pub.n);

  /* 转为字节数组 */
  uint8_t *em = (uint8_t *)malloc((size_t)k);
  if (!em) {
    bi_free(&sig_bi); bi_free(&em_bi);
    rsa_public_key_free(&pub);
    luaL_error(L, "RSA: memory allocation failed");
  }
  bi_to_bytes(&em_bi, em, k);

  /* PKCS#1 v1.5 验签 */
  int valid = pkcs1_verify_unpad((const uint8_t *)data, (int)data_len, em, k);

  free(em);
  bi_free(&sig_bi);
  bi_free(&em_bi);
  rsa_public_key_free(&pub);

  lua_pushboolean(L, valid);
  return 1;
}

static const luaL_Reg rsa_public_funcs[] = {
  {"encrypt", l_rsa_public_encrypt},
  {"verify",  l_rsa_public_verify},
  {NULL, NULL}
};

/* ========================================================================
 * Part 8: Lua 绑定 - rsa.private 子表
 * ======================================================================== */

/**
 * @brief rsa.private.decrypt(data, privkey) -> decrypted_bytes
 *        使用 PKCS#1 v1.5 去填充解密数据 (优先使用 CRT)
 * @param data    密文数据 (k 字节)
 * @param privkey 私钥表
 */
static int l_rsa_private_decrypt(lua_State *L) {
  size_t data_len;
  const char *data = luaL_checklstring(L, 1, &data_len);

  rsa_private_key_t priv;
  rsa_private_key_init(&priv);
  get_private_key_from_table(L, 2, &priv);

  int k = (priv.bits + 7) / 8;
  if ((int)data_len != k) {
    rsa_private_key_free(&priv);
    luaL_error(L, "RSA private.decrypt: ciphertext length must be %d bytes (got %d)",
               k, (int)data_len);
  }

  /* 密文转大整数 */
  bigint_t cipher, plain;
  bi_init(&cipher, 0);
  bi_init(&plain, 0);
  bi_from_bytes(&cipher, (const uint8_t *)data, k);

  /* RSA 解密 */
  if (!bi_is_zero(&priv.p) && !bi_is_zero(&priv.q)) {
    rsa_crt_decrypt(&plain, &cipher, &priv);
  } else {
    rsa_raw(&plain, &cipher, &priv.d, &priv.n);
  }

  /* 转为字节数组并去填充 */
  uint8_t *em = (uint8_t *)malloc((size_t)k);
  if (!em) {
    bi_free(&cipher); bi_free(&plain);
    rsa_private_key_free(&priv);
    luaL_error(L, "RSA: memory allocation failed");
  }
  bi_to_bytes(&plain, em, k);

  uint8_t *out = (uint8_t *)malloc((size_t)k);
  if (!out) {
    free(em);
    bi_free(&cipher); bi_free(&plain);
    rsa_private_key_free(&priv);
    luaL_error(L, "RSA: memory allocation failed");
  }

  int msg_len = pkcs1_decrypt_unpad(out, k, em, k);
  free(em);

  if (msg_len < 0) {
    free(out);
    bi_free(&cipher); bi_free(&plain);
    rsa_private_key_free(&priv);
    luaL_error(L, "RSA private.decrypt: PKCS#1 v1.5 unpadding failed");
  }

  lua_pushlstring(L, (const char *)out, (size_t)msg_len);
  free(out);
  bi_free(&cipher); bi_free(&plain);
  rsa_private_key_free(&priv);
  return 1;
}

/**
 * @brief rsa.private.sign(data, privkey) -> signature_bytes
 *        使用 PKCS#1 v1.5 签名 (SHA-256)
 * @param data    待签名数据
 * @param privkey 私钥表
 */
static int l_rsa_private_sign(lua_State *L) {
  size_t data_len;
  const char *data = luaL_checklstring(L, 1, &data_len);

  rsa_private_key_t priv;
  rsa_private_key_init(&priv);
  get_private_key_from_table(L, 2, &priv);

  int k = (priv.bits + 7) / 8;

  /* PKCS#1 v1.5 签名填充 */
  uint8_t *padded = (uint8_t *)malloc((size_t)k);
  if (!padded) {
    rsa_private_key_free(&priv);
    luaL_error(L, "RSA: memory allocation failed");
  }

  int pad_len = pkcs1_sign_pad(padded, k, (const uint8_t *)data, (int)data_len);
  if (pad_len < 0) {
    free(padded);
    rsa_private_key_free(&priv);
    luaL_error(L, "RSA private.sign: padding failed (key too small for SHA-256)");
  }

  /* 填充数据转大整数 */
  bigint_t msg;
  bi_init(&msg, 0);
  bi_from_bytes(&msg, padded, k);
  free(padded);

  /* RSA 签名: sig = msg^d mod n */
  bigint_t sig;
  bi_init(&sig, 0);

  if (!bi_is_zero(&priv.p) && !bi_is_zero(&priv.q)) {
    rsa_crt_decrypt(&sig, &msg, &priv);
  } else {
    rsa_raw(&sig, &msg, &priv.d, &priv.n);
  }

  /* 输出签名字节 */
  push_bigint_bytes(L, &sig, k);

  bi_free(&msg);
  bi_free(&sig);
  rsa_private_key_free(&priv);
  return 1;
}

static const luaL_Reg rsa_private_funcs[] = {
  {"decrypt", l_rsa_private_decrypt},
  {"sign",    l_rsa_private_sign},
  {NULL, NULL}
};

/* ========================================================================
 * Part 9: Lua 绑定 - rsa.encode 子表 (PEM 编码)
 * ======================================================================== */

/**
 * @brief 将公钥序列化为简约 DER 格式并 Base64 编码
 *        格式: 4 字节 bits (大端) + 2 字节 n_len + n 字节 + 2 字节 e_len + e 字节
 * @param pub 公钥指针
 * @param out_len 输出二进制长度指针
 * @return 二进制数据 (调用者需 free), NULL 表示失败
 */
static uint8_t* rsa_encode_public_der(const rsa_public_key_t *pub, int *out_len) {
  /* 序列化 n 和 e 为大端字节 */
  int k = (pub->bits + 7) / 8;
  int n_len = k;
  int e_bits = bi_nbits(&pub->e);
  int e_len = (e_bits + 7) / 8;

  int total = 4 + 2 + n_len + 2 + e_len;
  uint8_t *der = (uint8_t *)malloc((size_t)total);
  if (!der) return NULL;

  /* bits (大端) */
  der[0] = (uint8_t)((pub->bits >> 24) & 0xFF);
  der[1] = (uint8_t)((pub->bits >> 16) & 0xFF);
  der[2] = (uint8_t)((pub->bits >> 8) & 0xFF);
  der[3] = (uint8_t)(pub->bits & 0xFF);

  /* n_len (大端) */
  der[4] = (uint8_t)((n_len >> 8) & 0xFF);
  der[5] = (uint8_t)(n_len & 0xFF);

  /* n 字节 */
  bi_to_bytes(&pub->n, der + 6, n_len);

  /* e_len (大端) */
  der[6 + n_len]     = (uint8_t)((e_len >> 8) & 0xFF);
  der[6 + n_len + 1] = (uint8_t)(e_len & 0xFF);

  /* e 字节 */
  bi_to_bytes(&pub->e, der + 6 + n_len + 2, e_len);

  *out_len = total;
  return der;
}

/**
 * @brief 将私钥序列化为简约 DER 格式并 Base64 编码
 * @param priv 私钥指针
 * @param out_len 输出二进制长度指针
 * @return 二进制数据 (调用者需 free), NULL 表示失败
 */
static uint8_t* rsa_encode_private_der(const rsa_private_key_t *priv, int *out_len) {
  int k = (priv->bits + 7) / 8;
  int d_bits = bi_nbits(&priv->d);
  int d_len = (d_bits + 7) / 8;
  int p_len = bi_is_zero(&priv->p) ? 0 : k / 2;
  int q_len = bi_is_zero(&priv->q) ? 0 : k - k / 2;

  /* n(2+k) + e(2+k) + d(2+k) + p(2+k/2) + q(2+k-k/2) */
  int total = 4 + (2 + k) * 2 + (2 + d_len) + (2 + p_len) + (2 + q_len);
  uint8_t *der = (uint8_t *)malloc((size_t)total);
  if (!der) return NULL;

  int pos = 0;

  /* bits */
  der[pos++] = (uint8_t)((priv->bits >> 24) & 0xFF);
  der[pos++] = (uint8_t)((priv->bits >> 16) & 0xFF);
  der[pos++] = (uint8_t)((priv->bits >> 8) & 0xFF);
  der[pos++] = (uint8_t)(priv->bits & 0xFF);

  /* n */
  der[pos++] = (uint8_t)((k >> 8) & 0xFF);
  der[pos++] = (uint8_t)(k & 0xFF);
  bi_to_bytes(&priv->n, der + pos, k); pos += k;

  /* e */
  int e_len = (bi_nbits(&priv->e) + 7) / 8;
  der[pos++] = (uint8_t)((e_len >> 8) & 0xFF);
  der[pos++] = (uint8_t)(e_len & 0xFF);
  bi_to_bytes(&priv->e, der + pos, e_len); pos += e_len;

  /* d */
  der[pos++] = (uint8_t)((d_len >> 8) & 0xFF);
  der[pos++] = (uint8_t)(d_len & 0xFF);
  bi_to_bytes(&priv->d, der + pos, d_len); pos += d_len;

  /* p */
  der[pos++] = (uint8_t)((p_len >> 8) & 0xFF);
  der[pos++] = (uint8_t)(p_len & 0xFF);
  if (p_len > 0) { bi_to_bytes(&priv->p, der + pos, p_len); pos += p_len; }

  /* q */
  der[pos++] = (uint8_t)((q_len >> 8) & 0xFF);
  der[pos++] = (uint8_t)(q_len & 0xFF);
  if (q_len > 0) { bi_to_bytes(&priv->q, der + pos, q_len); pos += q_len; }

  *out_len = pos;
  return der;
}

/**
 * @brief rsa.encode.public(pubkey) -> PEM 格式字符串
 * @param pubkey 公钥表
 */
static int l_rsa_encode_public(lua_State *L) {
  rsa_public_key_t pub;
  rsa_public_key_init(&pub);
  get_public_key_from_table(L, 1, &pub);

  int der_len;
  uint8_t *der = rsa_encode_public_der(&pub, &der_len);
  rsa_public_key_free(&pub);

  if (!der) luaL_error(L, "RSA encode.public: memory allocation failed");

  /* Base64 编码 */
  int b64_line_len = (der_len + 2) / 3 * 4;
  int num_lines = (b64_line_len + 63) / 64;
  /* PEM 格式: 头部 + Base64 (每行 64 字符 + \n) + 尾部 */
  int pem_size = 28 + b64_line_len + num_lines + 26 + 1;  /* "-----BEGIN RSA PUBLIC KEY-----\n...\n-----END RSA PUBLIC KEY-----\0" */

  char *pem = (char *)malloc((size_t)pem_size);
  if (!pem) {
    free(der);
    luaL_error(L, "RSA encode.public: memory allocation failed");
  }

  char *b64 = (char *)malloc((size_t)(b64_line_len + 1));
  if (!b64) {
    free(der); free(pem);
    luaL_error(L, "RSA encode.public: memory allocation failed");
  }

  int b64_len = base64_encode(b64, der, der_len);
  free(der);

  /* 构建 PEM */
  int pos = 0;
  pos += sprintf(pem + pos, "-----BEGIN RSA PUBLIC KEY-----\n");
  for (int i = 0; i < b64_len; i += 64) {
    int chunk = (b64_len - i > 64) ? 64 : (b64_len - i);
    memcpy(pem + pos, b64 + i, (size_t)chunk);
    pos += chunk;
    pem[pos++] = '\n';
  }
  pos += sprintf(pem + pos, "-----END RSA PUBLIC KEY-----");
  pem[pos] = '\0';

  free(b64);
  lua_pushstring(L, pem);
  free(pem);
  return 1;
}

/**
 * @brief rsa.encode.private(privkey) -> PEM 格式字符串
 * @param privkey 私钥表
 */
static int l_rsa_encode_private(lua_State *L) {
  rsa_private_key_t priv;
  rsa_private_key_init(&priv);
  get_private_key_from_table(L, 1, &priv);

  int der_len;
  uint8_t *der = rsa_encode_private_der(&priv, &der_len);
  rsa_private_key_free(&priv);

  if (!der) luaL_error(L, "RSA encode.private: memory allocation failed");

  /* Base64 编码 */
  int b64_line_len = (der_len + 2) / 3 * 4;
  int num_lines = (b64_line_len + 63) / 64;
  int pem_size = 32 + b64_line_len + num_lines + 30 + 1;

  char *pem = (char *)malloc((size_t)pem_size);
  char *b64 = (char *)malloc((size_t)(b64_line_len + 1));

  if (!pem || !b64) {
    free(der); free(pem); free(b64);
    luaL_error(L, "RSA encode.private: memory allocation failed");
  }

  int b64_len = base64_encode(b64, der, der_len);
  free(der);

  /* 构建 PEM */
  int pos = 0;
  pos += sprintf(pem + pos, "-----BEGIN RSA PRIVATE KEY-----\n");
  for (int i = 0; i < b64_len; i += 64) {
    int chunk = (b64_len - i > 64) ? 64 : (b64_len - i);
    memcpy(pem + pos, b64 + i, (size_t)chunk);
    pos += chunk;
    pem[pos++] = '\n';
  }
  pos += sprintf(pem + pos, "-----END RSA PRIVATE KEY-----");
  pem[pos] = '\0';

  free(b64);
  lua_pushstring(L, pem);
  free(pem);
  return 1;
}

static const luaL_Reg rsa_encode_funcs[] = {
  {"public",  l_rsa_encode_public},
  {"private", l_rsa_encode_private},
  {NULL, NULL}
};

/* ========================================================================
 * Part 10: 模块入口
 * ======================================================================== */

/**
 * @brief 打开 rsa 模块
 *
 * 创建子表结构:
 *   rsa.key     - 密钥管理
 *   rsa.public  - 公钥操作
 *   rsa.private - 私钥操作
 *   rsa.encode  - 密钥编码
 *
 * @param L Lua 状态
 * @return 1 (推入模块表)
 */
LUAMOD_API int luaopen_rsa(lua_State *L) {
  lua_createtable(L, 0, 4);  /* rsa 主表: key, public, private, encode */

  /* -- rsa.key 子表 -- */
  luaL_newlib(L, rsa_key_funcs);
  lua_setfield(L, -2, "key");

  /* -- rsa.public 子表 -- */
  luaL_newlib(L, rsa_public_funcs);
  lua_setfield(L, -2, "public");

  /* -- rsa.private 子表 -- */
  luaL_newlib(L, rsa_private_funcs);
  lua_setfield(L, -2, "private");

  /* -- rsa.encode 子表 -- */
  luaL_newlib(L, rsa_encode_funcs);
  lua_setfield(L, -2, "encode");

  return 1;
}