#ifndef CSPRNG_H
#define CSPRNG_H

#include <stdint.h>
#include <stddef.h>  /* 定义 size_t 类型（WASM/跨平台兼容必需） */

/**
 * @brief 密码学安全伪随机数生成器 (CSPRNG)
 * 基于 xorshift128+ 算法，提供高质量随机数用于安全敏感场景
 * 
 * 特性：
 * - 周期: 2^128-1 (远超rand()的2^31)
 * - 通过BigCrush测试套件
 * - 无设备绑定，纯软件实现，全平台兼容
 * - 使用多熵源初始化（时间戳 + 编译器优化屏障）
 */

/* xorshift128+ 状态结构体 */
typedef struct {
    uint64_t s[2];  /* 内部状态（128位） */
} CSPRNG_State;

/**
 * @brief 初始化CSPRNG状态
 * 使用多熵源种子生成器，确保每次运行产生不同序列
 *
 * @param state   CSPRNG状态指针（必须已分配）
 * @param seed    基础种子值（通常使用时间戳）
 *
 * @note 内部使用：
 *       - 基础种子
 *       - 指针地址熵（防止相同种子产生相同序列）
 *       - 固定常量混合（增加扩散）
 */
void csprng_init(CSPRNG_State* state, uint64_t seed);

/**
 * @brief 生成下一个64位随机数
 * xorshift128+ 核心算法，产生均匀分布的随机数
 *
 * @param state  CSPRNG状态指针（会被更新）
 * @return       64位随机数值
 *
 * @warning 必须先调用 csprng_init() 初始化状态
 */
uint64_t csprng_next64(CSPRNG_State* state);

/**
 * @brief 生成32位随机数（截取高32位以获得更好分布）
 *
 * @param state  CSPRNG状态指针
 * @return       32位随机数值
 */
uint32_t csprng_next32(CSPRNG_State* state);

/**
 * @brief 生成 [0, upper_bound) 范围内的随机整数
 * 使用拒绝采样法避免模偏差（modulo bias）
 *
 * @param state        CSPRNG状态指针
 * @param upper_bound  上界（不包含）
 * @return             [0, upper_bound) 范围内的随机整数
 *
 * @example
 *   // 生成 0-99 的随机数（无偏差）
 *   int r = csprng_range(&rng, 100);
 */
uint64_t csprng_range(CSPRNG_State* state, uint64_t upper_bound);

/**
 * @brief 用随机数据填充缓冲区
 * 用于生成随机密钥、盐值等场景
 *
 * @param state   CSPRNG状态指针
 * @param buf     目标缓冲区指针
 * @param len     要填充的字节数
 */
void csprng_bytes(CSPRNG_State* state, void* buf, size_t len);

#endif /* CSPRNG_H */
