/**
 * Test known secp256k1 ECDSA verify
 * Uses deterministic test vectors to trace the verify flow
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* Include sha256.h */
#include "src/utils/sha256.h"

/* Copy minimal type definitions */
#define U256_LIMBS 8
#define U512_LIMBS 16

typedef struct {
    uint32_t d[U256_LIMBS];
} uint256_t;

/* Forward declarations */
static void u256_copy(uint256_t *r, const uint256_t *a);
static int u256_cmp(const uint256_t *a, const uint256_t *b);
static int u256_ge(const uint256_t *a, const uint256_t *b);
static int u256_is_zero(const uint256_t *a);
static void u256_from_hex(uint256_t *r, const char *hex);
static void u256_to_hex(char *out, const uint256_t *a);
static void u256_from_bytes(uint256_t *r, const uint8_t bytes[32]);
static uint32_t u256_add(uint256_t *r, const uint256_t *a, const uint256_t *b);
static uint32_t u256_sub(uint256_t *r, const uint256_t *a, const uint256_t *b);
static void u256_add_mod(uint256_t *r, const uint256_t *a, const uint256_t *b, const uint256_t *m);
static void u256_sub_mod(uint256_t *r, const uint256_t *a, const uint256_t *b, const uint256_t *m);
static void u256_mul_full(uint32_t r[U512_LIMBS], const uint32_t a[U256_LIMBS], const uint32_t b[U256_LIMBS]);
static void u256_fast_reduce_p(uint256_t *r, const uint32_t c[U512_LIMBS]);
static void u256_mul_mod_p(uint256_t *r, const uint256_t *a, const uint256_t *b);
static void u256_mul_mod(uint256_t *r, const uint256_t *a, const uint256_t *b, const uint256_t *m);
static void u256_sqr_mod_p(uint256_t *r, const uint256_t *a);
static void u256_inv_mod(uint256_t *r, const uint256_t *a, const uint256_t *m);

extern const uint256_t SECP256K1_N;
extern const uint256_t SECP256K1_P;
extern const uint256_t SECP256K1_B;

/* Include the actual source (hacky but works for testing) */
#include "src/utils/lecc.c"

/* Test known value: 3^(-1) mod n */
int test_inv(void) {
    uint256_t a, inv, prod, n;
    
    u256_copy(&n, &SECP256K1_N);
    
    /* Test 3^(-1) mod n */
    memset(a.d, 0, sizeof(a.d));
    a.d[0] = 3;
    
    u256_inv_mod(&inv, &a, &n);
    
    char buf[65];
    u256_to_hex(buf, &inv);
    printf("3^(-1) mod n = %s\n", buf);
    
    /* Verify: 3 * inv ≡ 1 (mod n) */
    u256_mul_mod(&prod, &a, &inv, &n);
    u256_to_hex(buf, &prod);
    printf("3 * inv mod n = %s (should be 0000...0001)\n", buf);
    
    if (u256_cmp(&prod, &(uint256_t){{1,0,0,0,0,0,0,0}}) == 0) {
        printf("PASS: u256_inv_mod\n");
        return 0;
    } else {
        printf("FAIL: u256_inv_mod\n");
        return 1;
    }
}

/* Test a * b mod n where a='hello' hash and b is from test vector */
int test_mul_mod(void) {
    uint256_t a, b, r;
    char buf[65];
    
    /* Use hash of "hello" */
    uint8_t hash[32];
    SHA256((const uint8_t*)"hello", 5, hash);
    
    u256_from_bytes(&a, hash);
    u256_to_hex(buf, &a);
    printf("hash('hello') = %s\n", buf);
    
    /* Test: a * 1 ≡ a mod n */
    memset(b.d, 0, sizeof(b.d));
    b.d[0] = 1;
    u256_mul_mod(&r, &a, &b, &SECP256K1_N);
    u256_to_hex(buf, &r);
    printf("hash * 1 mod n = %s\n", buf);
    
    if (u256_cmp(&r, &a) == 0) {
        printf("PASS: u256_mul_mod identity\n");
    } else {
        printf("FAIL: u256_mul_mod identity\n");
        return 1;
    }
    
    /* Test: a * n ≡ 0 mod n */
    u256_mul_mod(&r, &a, &SECP256K1_N, &SECP256K1_N);
    if (u256_is_zero(&r)) {
        printf("PASS: u256_mul_mod zero\n");
    } else {
        printf("FAIL: u256_mul_mod zero\n");
        return 1;
    }
    
    return 0;
}

/* Test fast_reduce_p with known values */
int test_fast_reduce(void) {
    /* Test (p-1) * (p-1) mod p = 1 */
    uint256_t pm1;
    u256_copy(&pm1, &SECP256K1_P);
    pm1.d[0]--; /* p-1 */
    
    uint256_t r;
    uint32_t c[U512_LIMBS];
    u256_mul_full(c, pm1.d, pm1.d);
    u256_fast_reduce_p(&r, c);
    
    char buf[65];
    u256_to_hex(buf, &r);
    printf("(p-1)^2 mod p = %s\n", buf);
    
    if (r.d[0] == 1 && u256_is_zero((uint256_t*)&r.d[0] + 1)) {
    /* Actually: if r.d[0]==1 and all other limbs are 0 */
    int all_zero = 1;
    for (int i = 1; i < U256_LIMBS; i++) {
        if (r.d[i] != 0) all_zero = 0;
    }
    if (r.d[0] == 1 && all_zero) {
        printf("PASS: u256_fast_reduce_p\n");
    } else {
        printf("FAIL: u256_fast_reduce_p (expected 1)\n");
        return 1;
    }
    
    return 0;
}

int main(void) {
    int failures = 0;
    
    printf("=== Test u256_inv_mod ===\n");
    failures += test_inv();
    
    printf("\n=== Test u256_mul_mod ===\n");
    failures += test_mul_mod();
    
    printf("\n=== Test u256_fast_reduce_p ===\n");
    failures += test_fast_reduce();
    
    printf("\n=== Total failures: %d ===\n", failures);
    return failures;
}