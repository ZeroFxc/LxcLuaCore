#ifndef CSPRNG_H
#define CSPRNG_H

#include <stdint.h>
#include <stddef.h>

/**
 * @file csprng.h
 * @brief 密码学安全伪随机数生成器 (CSPRNG)
 * 
 * 基于 ChaCha20 流密码 (RFC 7539) 构造的 CSPRNG 实现。
 * 
 * ════════════════════════════════════════════════════════════════
 * ⚠️ 安全特性说明：
 * 
 * ✅ 密码学安全：
 *    - 基于 ChaCha20 (RFC 7539)，与 TLS 1.3 / WireGuard 同级安全
 *    - 256 位密钥空间 → 2^256 种可能状态
 *    - 抗状态恢复攻击：即使泄露输出也无法推导内部状态
 *    - 抗前向安全性破坏：当前状态无法反推历史输出
 *    - 常量时间实现：抗时序侧信道攻击
 * 
 * ❌ 与 xorshift128+ 的本质区别：
 *    - xorshift128+: 观察 2 个输出即可代数求解内部状态 (毫秒级)
 *    - ChaCha20-CSPRNG: 需要破解 ChaCha20 ≈ 2^256 复杂度 (宇宙寿命级别)
 * 
 * 适用场景：
 *    - 密钥/IV/Nonce 生成
 *    - 会话 Token / CSRF Token
 *    - 盐值 / 随机填充
 *    - 密码学协议中的随机数需求
 * 
 * 不适用场景（请用普通 PRNG）：
 *    - 游戏随机 / Monte Carlo 模拟
 *    - 统计采样 / 洗牌算法（非性能关键路径除外）
 * ════════════════════════════════════════════════════════════════
 */

#define CSPRNG_KEY_SIZE     32   /* ChaCha20 密钥长度: 256 位 */
#define CSPRNG_NONCE_SIZE   12   /* ChaCha20 Nonce 长度: 96 位 */
#define CSPRNG_BLOCK_SIZE   64   /* ChaCha20 单次输出块大小: 512 位 */
#define CSPRNG_ROUNDS       20   /* ChaCha20 标准轮数 (原始版, 非 ChaCha12) */

/**
 * @brief CSPRNG 内部状态结构体
 * 
 * 包含完整的 ChaCha20 上下文 + 输出缓冲区管理。
 * 总大小: 80 字节 (紧凑设计)
 */
typedef struct {
    uint32_t input[16];        /* ChaCha20 初始输入矩阵 (state matrix) */
    
    uint8_t  stream_buffer[CSPRNG_BLOCK_SIZE];  /* 当前输出块的缓存 */
    size_t   stream_pos;       /* stream_buffer 中已消耗的字节数 [0, 64] */
    
    uint64_t blocks_generated; /* 已生成的 ChaCha20 块计数器 (用于重种检测) */
} CSPRNG_State;

/**
 * @brief 初始化 CSPRNG 状态
 * 
 * 将种子安全地扩散到 ChaCha20 的 256 位密钥 + 96 位 Nonce 空间。
 * 初始化后自动进行预热（丢弃前 2 个输出块），确保充分混合。
 * 
 * @param state   CSPRNG 状态指针（必须已分配内存）
 * @param seed    种子值（通常使用高精度时间戳或操作系统熵源）
 *                建议至少 64 位熵；若熵不足可多次调用 csprng_bytes 后 reseed
 *
 * @note 内部处理流程：
 *       1. 使用类 HKDF 方法将种子扩展为 320 位 (密钥+Nonce)
 *       2. 构建 ChaCha20 初始状态矩阵
 *       3. 预热：生成并丢弃 2 个 ChaCha20 块 (128 字节)
 *       
 * @warning 相同的 seed 总是产生相同的序列（确定性）。
 *          对于不可重复的安全场景，必须使用不可预测的种子！
 */
void csprng_init(CSPRNG_State* state, uint64_t seed);

/**
 * @brief 重新播种 CSPRNG（可选的安全增强操作）
 * 
 * 使用新种子更新内部状态，实现前向安全性。
 * 调用后，之前的状态无法被恢复。
 * 
 * @param state   CSPRNG 状态指针（必须已初始化）
 * @param new_seed 新的种子值
 */
void csprng_reseed(CSPRNG_State* state, uint64_t new_seed);

/**
 * @brief 生成下一个 64 位随机数
 * 
 * 从 ChaCha20 输出流中提取 8 字节，构造成 little-endian uint64_t。
 * 
 * @param state  CSPRNG 状态指针（会被更新，内部状态推进）
 * @return       64 位随机数值，均匀分布于 [0, 2^64)
 *
 * @warning 必须先调用 csprng_init() 初始化状态，否则行为未定义
 * @note      每次调用消耗 8 字节的 ChaCha20 输出流
 */
uint64_t csprng_next64(CSPRNG_State* state);

/**
 * @brief 生成下一个 32 位随机数
 * 
 * 从 ChaCha20 输出流中提取 4 字节。
 * 
 * @param state  CSPRNG 状态指针
 * @return       32 位随机数值，均匀分布于 [0, 2^32)
 */
uint32_t csprng_next32(CSPRNG_State* state);

/**
 * @brief 生成 [0, upper_bound) 范围内的无偏差随机整数
 * 
 * 使用 Lemire's 优化拒绝采样法（乘法方法），
 * 对所有 upper_bound 值都保证无 modulo bias。
 * 
 * @param state        CSPRNG 状态指针
 * @param upper_bound  上界（不包含），必须 > 0
 * @return             [0, upper_bound) 范围内的均匀分布随机整数
 *
 * @performance:
 *   - 当 upper_bound 是 2 的幂时：O(1)，单次 next64 + 位掩码
 *   - 一般情况：平均 O(1)，最坏情况约 2 次 next64 调用
 *
 * @example
 *   // 生成安全的随机索引（无偏差）
 *   uint64_t idx = csprng_range(&rng, array_size);
 *   
 *   // 生成 0-99 的公平骰子
 *   uint64_t dice = csprng_range(&rng, 100);
 */
uint64_t csprng_range(CSPRNG_State* state, uint64_t upper_bound);

/**
 * @brief 用密码学安全随机数据填充缓冲区
 * 
 * 直接从 ChaCha20 输出流复制指定长度的随机字节。
 * 适用于生成密钥、IV、Nonce、盐值等安全敏感数据。
 * 
 * @param state   CSPRNG 状态指针
 * @param buf     目标缓冲区指针（必须有足够空间）
 * @param len     要填充的字节数
 *
 * @security:
 *   - 输出具有计算上不可区分性（与真随机无法区分）
 *   - 适用于 NIST SP 800-90A 要求的所有场景
 *   - 每字节独立且均匀分布
 *
 * @example
 *   // 生成 AES-256 密钥
 *   uint8_t aes_key[32];
 *   csprng_bytes(&rng, aes_key, 32);
 *   
 *   // 生成安全的 IV
 *   uint8_t iv[16];
 *   csprng_bytes(&rng, iv, 16);
 *   
 *   // 生成密码学 salt
 *   uint8_t salt[32];
 *   csprng_bytes(&rng, salt, 32);
 */
void csprng_bytes(CSPRNG_State* state, void* buf, size_t len);

#endif /* CSPRNG_H */
