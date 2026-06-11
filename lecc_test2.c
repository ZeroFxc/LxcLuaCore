// lecc_test2.c - detailed fast reduction trace
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define U256_LIMBS 8
#define U512_LIMBS 16

typedef struct { uint32_t d[U256_LIMBS]; } uint256_t;

static const uint256_t SECP256K1_P = {{
  0xFFFFFC2F, 0xFFFFFFFE, 0xFFFFFFFF, 0xFFFFFFFF,
  0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF
}};

void u256_mul_full(uint32_t r[U512_LIMBS], const uint32_t a[U256_LIMBS], const uint32_t b[U256_LIMBS]) {
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

void print_limbs(const char *name, const uint32_t *d, int n) {
  printf("%s = [", name);
  for (int i = 0; i < n; i++) {
    printf("0x%08X%s", d[i], i < n-1 ? ", " : "");
  }
  printf("]\n");
}

int u256_ge(const uint256_t *a, const uint256_t *b) {
  for (int i = U256_LIMBS - 1; i >= 0; i--) {
    if (a->d[i] > b->d[i]) return 1;
    if (a->d[i] < b->d[i]) return 0;
  }
  return 1;
}
uint32_t u256_sub(uint256_t *r, const uint256_t *a, const uint256_t *b) {
  uint64_t borrow = 0;
  for (int i = 0; i < U256_LIMBS; i++) {
    uint64_t diff = (uint64_t)a->d[i] - (uint64_t)b->d[i] - borrow;
    r->d[i] = (uint32_t)(diff & 0xFFFFFFFF);
    borrow = (diff >> 32) & 1;
  }
  return (uint32_t)borrow;
}

// FIXED version
void u256_fast_reduce_p_v2(uint256_t *r, const uint32_t c[U512_LIMBS]) {
  uint32_t D[10];
  uint64_t carry;
  memset(D, 0, sizeof(D));
  memcpy(D, c, U256_LIMBS * sizeof(uint32_t));

  // Step 2: D += C_hi * 2^32
  carry = 0;
  for (int i = 0; i < U256_LIMBS; i++) {
    uint64_t sum = (uint64_t)D[i + 1] + (uint64_t)c[8 + i] + carry;
    D[i + 1] = (uint32_t)(sum & 0xFFFFFFFF);
    carry = sum >> 32;
  }
  if (carry) { D[9] += (uint32_t)carry; }

  // Step 3: D += C_hi * 977
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

  printf("\nAfter steps 1-3:\n");
  print_limbs("r (low)", r->d, 8);
  printf("D[8]=0x%08X D[9]=0x%08X\n", D[8], D[9]);

  // Step 4: D[8] * 2^256 ≡ D[8] * (2^32 + 977)
  if (D[8] != 0) {
    printf("\nD[8] = 0x%08X (non-zero)\n", D[8]);
    {
      uint64_t sum = (uint64_t)r->d[0] + ((uint64_t)D[8] << 32);
      printf("Before: r->d[0]=0x%08X, D[8]<<32=0x%016llX, sum=0x%016llX\n",
             r->d[0], (unsigned long long)((uint64_t)D[8] << 32), (unsigned long long)sum);
      r->d[0] = (uint32_t)(sum & 0xFFFFFFFF);
      carry = sum >> 32;
      printf("After: r->d[0]=0x%08X, carry=0x%016llX\n", r->d[0], (unsigned long long)carry);
    }
    for (int i = 1; i < U256_LIMBS && carry > 0; i++) {
      carry += r->d[i];
      r->d[i] = (uint32_t)(carry & 0xFFFFFFFF);
      carry >>= 32;
    }
    {
      uint64_t prod8 = (uint64_t)D[8] * 977ULL;
      printf("D[8]*977=0x%016llX\n", (unsigned long long)prod8);
      carry = (uint64_t)r->d[0] + prod8;
      r->d[0] = (uint32_t)(carry & 0xFFFFFFFF);
      carry >>= 32;
    }
    for (int i = 1; i < U256_LIMBS && carry > 0; i++) {
      carry += r->d[i];
      r->d[i] = (uint32_t)(carry & 0xFFFFFFFF);
      carry >>= 32;
    }
    if (carry) printf("WARNING: carry after D[8]*977: 0x%llX\n", (unsigned long long)carry);
    print_limbs("r after D[8]", r->d, 8);
  }

  // Step 5: D[9] handling
  if (D[9] != 0) {
    printf("\nD[9] = 0x%08X (non-zero)\n", D[9]);
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
      // recurse...
      printf("D[9] overflow carry=0x%llX\n", (unsigned long long)carry);
    }
  }

  while (u256_ge(r, &SECP256K1_P)) {
    u256_sub(r, r, &SECP256K1_P);
  }
}

int main() {
  // Compute (p-1)^2
  uint256_t p1;
  memcpy(&p1, &SECP256K1_P, sizeof(p1));
  p1.d[0] -= 1; // p-1

  uint32_t c[U512_LIMBS];
  u256_mul_full(c, p1.d, p1.d);

  print_limbs("p", SECP256K1_P.d, 8);
  print_limbs("p-1", p1.d, 8);
  print_limbs("(p-1)^2 [0..7]", &c[0], 8);
  print_limbs("(p-1)^2 [8..15]", &c[8], 8);

  uint256_t result;
  u256_fast_reduce_p_v2(&result, c);
  print_limbs("result", result.d, 8);

  // Expected: 1
  printf("\nExpected result: [1, 0, 0, 0, 0, 0, 0, 0]\n");

  return 0;
}