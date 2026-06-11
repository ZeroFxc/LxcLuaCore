// lecc_test.c - 底层字段运算验证
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define U256_LIMBS 8
#define U512_LIMBS 16

typedef struct { uint32_t d[U256_LIMBS]; } uint256_t;

static const uint256_t SECP256K1_GX = {{
  0x16F81798, 0x59F2815B, 0x2DCE28D9, 0x029BFCDB,
  0xCE870B07, 0x55A06295, 0xF9DCBBAC, 0x79BE667E
}};
static const uint256_t SECP256K1_GY = {{
  0xFB10D4B8, 0x9C47D08F, 0xA6855419, 0xFD17B448,
  0x0E1108A8, 0x5DA4FBFC, 0x26A3C465, 0x483ADA77
}};
static const uint256_t SECP256K1_P = {{
  0xFFFFFC2F, 0xFFFFFFFE, 0xFFFFFFFF, 0xFFFFFFFF,
  0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF
}};
static const uint256_t SECP256K1_B = {{7, 0, 0, 0, 0, 0, 0, 0}};

// Include the actual functions from lecc.c (simplified copy)
static void bin_to_hex(char *dst, const uint8_t *src, size_t len) {
  static const char HEX[] = "0123456789abcdef";
  for (size_t i = 0; i < len; i++) {
    dst[i * 2]     = HEX[src[i] >> 4];
    dst[i * 2 + 1] = HEX[src[i] & 0x0F];
  }
  dst[len * 2] = '\0';
}

static void u256_to_hex(char *hex, const uint256_t *a) {
  uint8_t bytes[32];
  for (int i = 0; i < 8; i++) {
    uint32_t limb = a->d[7 - i];
    bytes[i * 4 + 0] = (uint8_t)(limb >> 24);
    bytes[i * 4 + 1] = (uint8_t)(limb >> 16);
    bytes[i * 4 + 2] = (uint8_t)(limb >> 8);
    bytes[i * 4 + 3] = (uint8_t)(limb);
  }
  bin_to_hex(hex, bytes, 32);
}

static int u256_cmp(const uint256_t *a, const uint256_t *b) {
  for (int i = U256_LIMBS - 1; i >= 0; i--) {
    if (a->d[i] > b->d[i]) return 1;
    if (a->d[i] < b->d[i]) return -1;
  }
  return 0;
}
static int u256_ge(const uint256_t *a, const uint256_t *b) { return u256_cmp(a, b) >= 0; }

static uint32_t u256_add(uint256_t *r, const uint256_t *a, const uint256_t *b) {
  uint64_t carry = 0;
  for (int i = 0; i < U256_LIMBS; i++) {
    carry += (uint64_t)a->d[i] + (uint64_t)b->d[i];
    r->d[i] = (uint32_t)(carry & 0xFFFFFFFF);
    carry >>= 32;
  }
  return (uint32_t)carry;
}
static uint32_t u256_sub(uint256_t *r, const uint256_t *a, const uint256_t *b) {
  uint64_t borrow = 0;
  for (int i = 0; i < U256_LIMBS; i++) {
    uint64_t diff = (uint64_t)a->d[i] - (uint64_t)b->d[i] - borrow;
    r->d[i] = (uint32_t)(diff & 0xFFFFFFFF);
    borrow = (diff >> 32) & 1;
  }
  return (uint32_t)borrow;
}
static void u256_add_mod(uint256_t *r, const uint256_t *a, const uint256_t *b, const uint256_t *m) {
  uint256_t tmp;
  uint32_t carry = u256_add(&tmp, a, b);
  if (carry || u256_ge(&tmp, m)) {
    u256_sub(r, &tmp, m);
  } else {
    memcpy(r->d, tmp.d, sizeof(r->d));
  }
}
static void u256_sub_mod(uint256_t *r, const uint256_t *a, const uint256_t *b, const uint256_t *m) {
  uint256_t tmp;
  uint32_t borrow = u256_sub(&tmp, a, b);
  if (borrow) {
    u256_add(r, &tmp, m);
  } else {
    memcpy(r->d, tmp.d, sizeof(r->d));
  }
}

// FIXED: incremental carry propagation
static void u256_mul_full(uint32_t r[U512_LIMBS], const uint32_t a[U256_LIMBS], const uint32_t b[U256_LIMBS]) {
  memset(r, 0, U512_LIMBS * sizeof(uint32_t));
  for (int i = 0; i < U256_LIMBS; i++) {
    uint64_t carry = 0;
    for (int j = 0; j < U256_LIMBS; j++) {
      uint64_t prod = (uint64_t)a[i] * (uint64_t)b[j] + (uint64_t)r[i + j] + carry;
      r[i + j] = (uint32_t)(prod & 0xFFFFFFFF);
      carry = prod >> 32;
    }
    for (int j = i + U256_LIMBS; carry > 0 && j < U512_LIMBS; j++) {
      uint64_t sum = (uint64_t)r[j] + carry;
      r[j] = (uint32_t)(sum & 0xFFFFFFFF);
      carry = sum >> 32;
    }
  }
}

// FIXED: carry extraction
static void u256_fast_reduce_p(uint256_t *r, const uint32_t c[U512_LIMBS]) {
  uint32_t D[10];
  uint64_t carry;
  memset(D, 0, sizeof(D));
  memcpy(D, c, U256_LIMBS * sizeof(uint32_t));

  carry = 0;
  for (int i = 0; i < U256_LIMBS; i++) {
    uint64_t sum = (uint64_t)D[i + 1] + (uint64_t)c[8 + i] + carry;
    D[i + 1] = (uint32_t)(sum & 0xFFFFFFFF);
    carry = sum >> 32;
  }
  if (carry) { D[9] += (uint32_t)carry; }

  carry = 0;
  for (int i = 0; i < U256_LIMBS; i++) {
    uint64_t prod = (uint64_t)c[8 + i] * 977ULL;
    uint64_t sum = (uint64_t)D[i] + (prod & 0xFFFFFFFF) + carry;
    D[i] = (uint32_t)(sum & 0xFFFFFFFF);
    carry = (sum >> 32) + (prod >> 32);
  }
  for (int i = U256_LIMBS; i < 10 && carry > 0; i++) {
    uint64_t sum = (uint64_t)D[i] + carry;
    D[i] = (uint32_t)(sum & 0xFFFFFFFF);
    carry = sum >> 32;
  }

  memcpy(r->d, D, U256_LIMBS * sizeof(uint32_t));

  // FIXED: D[8] processing - correctly extract carry before masking
  if (D[8] != 0) {
    {
      uint64_t sum = (uint64_t)r->d[0] + ((uint64_t)D[8] << 32);
      r->d[0] = (uint32_t)(sum & 0xFFFFFFFF);
      carry = sum >> 32;
    }
    for (int i = 1; i < U256_LIMBS && carry > 0; i++) {
      carry += r->d[i];
      r->d[i] = (uint32_t)(carry & 0xFFFFFFFF);
      carry >>= 32;
    }
    {
      uint64_t prod8 = (uint64_t)D[8] * 977ULL;
      carry = (uint64_t)r->d[0] + prod8;
      r->d[0] = (uint32_t)(carry & 0xFFFFFFFF);
      carry >>= 32;
    }
    for (int i = 1; i < U256_LIMBS && carry > 0; i++) {
      carry += r->d[i];
      r->d[i] = (uint32_t)(carry & 0xFFFFFFFF);
      carry >>= 32;
    }
  }

  if (D[9] != 0) {
    uint64_t d9 = D[9];
    carry = (uint64_t)r->d[0] + d9 * 954529ULL;
    r->d[0] = (uint32_t)(carry & 0xFFFFFFFF);
    carry >>= 32;
    carry += (uint64_t)r->d[1] + d9 * 1954ULL;
    r->d[1] = (uint32_t)(carry & 0xFFFFFFFF);
    carry >>= 32;
    carry += (uint64_t)r->d[2] + d9;
    r->d[2] = (uint32_t)(carry & 0xFFFFFFFF);
    carry >>= 32;
    for (int i = 3; i < U256_LIMBS && carry > 0; i++) {
      carry += r->d[i];
      r->d[i] = (uint32_t)(carry & 0xFFFFFFFF);
      carry >>= 32;
    }
    if (carry > 0) {
      uint32_t extra = (uint32_t)carry;
      carry = (uint64_t)r->d[0] + ((uint64_t)extra << 32) + (uint64_t)extra * 977ULL;
      r->d[0] = (uint32_t)(carry & 0xFFFFFFFF);
      carry >>= 32;
      for (int i = 1; i < U256_LIMBS && carry > 0; i++) {
        carry += r->d[i];
        r->d[i] = (uint32_t)(carry & 0xFFFFFFFF);
        carry >>= 32;
      }
    }
  }

  while (u256_ge(r, &SECP256K1_P)) {
    u256_sub(r, r, &SECP256K1_P);
  }
}

static void u256_mul_mod_p(uint256_t *r, const uint256_t *a, const uint256_t *b) {
  uint32_t c[U512_LIMBS];
  u256_mul_full(c, a->d, b->d);
  u256_fast_reduce_p(r, c);
}
static void u256_sqr_mod_p(uint256_t *r, const uint256_t *a) {
  u256_mul_mod_p(r, a, a);
}

int main() {
  // Test 1: Gx² + 7 mod p should equal Gy² mod p (generator is on curve)
  uint256_t x2, x3, rhs, y2;
  u256_sqr_mod_p(&x2, &SECP256K1_GX);
  u256_mul_mod_p(&x3, &x2, &SECP256K1_GX);
  u256_add_mod(&rhs, &x3, &SECP256K1_B, &SECP256K1_P);
  u256_sqr_mod_p(&y2, &SECP256K1_GY);

  char hx2[65], hx3[65], hrhs[65], hy2[65];
  u256_to_hex(hx2, &x2);
  u256_to_hex(hx3, &x3);
  u256_to_hex(hrhs, &rhs);
  u256_to_hex(hy2, &y2);

  printf("Gx² mod p = %s\n", hx2);
  printf("Gx³ mod p = %s\n", hx3);
  printf("x³+7 mod p = %s\n", hrhs);
  printf("Gy² mod p = %s\n", hy2);
  printf("On curve: %s\n", u256_cmp(&y2, &rhs) == 0 ? "YES" : "NO");

  // Test 2: basic multiplication
  uint256_t a = {{2, 0, 0, 0, 0, 0, 0, 0}};
  uint256_t b = {{3, 0, 0, 0, 0, 0, 0, 0}};
  uint256_t prod;
  u256_mul_mod_p(&prod, &a, &b);
  char hprod[65];
  u256_to_hex(hprod, &prod);
  printf("\n2*3 mod p = %s (expected: 6)\n", hprod);

  // Test 3: p-1 * p-1 mod p
  uint256_t p_minus_1;
  u256_sub(&p_minus_1, &SECP256K1_P, &a); // p-2
  p_minus_1.d[0] += 1; // p-2+1 = p-1
  u256_mul_mod_p(&prod, &p_minus_1, &p_minus_1);
  u256_to_hex(hprod, &prod);
  printf("(p-1)² mod p = %s (expected: 1)\n", hprod);

  return 0;
}