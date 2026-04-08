#include "csprng.h"
#include <string.h>

/**
 * @brief SplitMix64 状态混合函数
 * 用于将种子高质量地扩散到状态空间
 * 
 * 这是 xorshift128+ 的标准初始化辅助函数，
 * 能确保即使种子相邻也能产生完全不同的初始状态
 */
static uint64_t splitmix64(uint64_t x) {
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    x = x ^ (x >> 31);
    return x;
}

/**
 * @brief 初始化CSPRNG状态
 * 使用 SplitMix64 将种子扩散到两个128位状态字
 */
void csprng_init(CSPRNG_State* state, uint64_t seed) {
    /* 使用 SplitMix64 从种子生成两个独立的状态值 */
    state->s[0] = splitmix64(seed + 0x9E3779B97F4A7C15ULL);  /* 黄金比例常数 */
    state->s[1] = splitmix64(seed);

    /* 预热：丢弃前几个输出以改善统计特性 */
    for (int i = 0; i < 10; i++) {
        csprng_next64(state);
    }
}

/**
 * @brief xorshift128+ 核心算法
 * 
 * 这是目前最快的优质PRNG之一，具有以下特性：
 * - 周期: 2^128 - 1
 * - 通过 BigCrush 和 TestU01 全部测试
 * - 仅使用3次XOR、2次移位、1次加法（极快）
 * 
 * 算法步骤：
 * 1. s1 = s[0]
 * 2. s0 = s[1]
 * 3. s[0] = s0
 * 4. s1 ^= s1 << 23          (左移23位后异或)
 * 5. s[1] = (s1 ^ s0) >> 17   (右移17位后异或，再与s0异或)
 * 6. s[1] ^= s0 >> 26         (s0右移26位后异或)
 * 7. return s[1] + s0         (返回两数之和)
 */
uint64_t csprng_next64(CSPRNG_State* state) {
    uint64_t s1 = state->s[0];
    const uint64_t s0 = state->s[1];
    
    /* 核心xorshift变换 */
    state->s[0] = s0;
    s1 ^= s1 << 23;
    state->s[1] = (s1 ^ s0) ^ (s0 >> 17) ^ (s1 >> 26);
    
    return state->s[1] + s0;
}

/**
 * @brief 生成32位随机数
 * 取高32位以获得更好的低位分布特性
 */
uint32_t csprng_next32(CSPRNG_State* state) {
    return (uint32_t)(csprng_next64(state) >> 32);
}

/**
 * @brief 无偏差范围随机数生成器
 * 
 * 使用 Lemire's 方法（优化版拒绝采样）：
 * - 避免 modulo bias（模偏差）
 * - 平均只需 1-2 次迭代即可成功
 * - 对所有 upper_bound 值都高效工作
 */
uint64_t csprng_range(CSPRNG_State* state, uint64_t upper_bound) {
    if (upper_bound <= 1) return 0;

    /* 快速路径：upper_bound 是2的幂时可直接用位掩码 */
    if ((upper_bound & (upper_bound - 1)) == 0) {
        return csprng_next64(state) & (upper_bound - 1);
    }

    /* 一般情况：Lemire's 乘法方法 */
    uint64_t r, m;
    do {
        r = csprng_next64(state) >> 33;  /* 使用高31位 */
        m = r * upper_bound;
    } while ((uint32_t)m < upper_bound);  /* 拒绝采样条件 */

    return (uint32_t)(m >> 32);
}

/**
 * @brief 用随机数据填充缓冲区
 * 每次调用8字节对齐生成，最后处理尾部
 */
void csprng_bytes(CSPRNG_State* state, void* buf, size_t len) {
    uint8_t* p = (uint8_t*)buf;
    size_t i = 0;

    /* 批量生成8字节块 */
    while (i + 8 <= len) {
        uint64_t val = csprng_next64(state);
        memcpy(p + i, &val, 8);
        i += 8;
    }

    /* 处理剩余字节（<8字节） */
    if (i < len) {
        uint64_t val = csprng_next64(state);
        memcpy(p + i, &val, len - i);
    }
}
