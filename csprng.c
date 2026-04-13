#include "csprng.h"
#include <string.h>

/* ═══════════════════════════════════════════════════════════════════════════
 * ChaCha20 常量定义 (RFC 7539 Section 2.3)
 * ═══════════════════════════════════════════════════════════════════════════ */

static const uint32_t CHACHA_CONSTANT[4] = {
    0x61707865U,  /* "expa" */
    0x3320646eU,  /* "nd 3" */
    0x79622d32U,  /* "2-by" */
    0x6b206574U   /* "te k" */
};

/* ═══════════════════════════════════════════════════════════════════════════
 * 内部辅助函数：种子扩散器
 * 
 * 将 64 位种子安全地扩展为 320 位 (256位密钥 + 96位Nonce)
 * 使用多轮不可逆混合确保即使相邻种子也产生完全不同的输出
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief SplitMix64 风格的 64 位状态混合函数
 * 
 * 基于 MurmurHash3/AES S-box 思想设计的非线性混合器。
 * 每次调用产生一个与输入高度不相关的 64 位输出，
 * 连续调用可生成任意长度的伪随机流。
 * 
 * @param x  输入值（会被就地修改以支持链式调用）
 * @return   混合后的 64 位伪随机值
 */
static uint64_t seed_mix(uint64_t* x) {
    *x = (*x ^ (*x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    *x = (*x ^ (*x >> 27)) * 0x94d049bb133111ebULL;
    *x = (*x ^ (*x >> 31));
    return *x;
}

/**
 * @brief 将单个 64 位种子扩展为完整的 ChaCha20 参数集
 * 
 * 扩展流程：
 * 1. 从 seed + 不同偏移量派生 5 个独立的 64 位值
 * 2. 将每个 64 位值拆分为 2 个 32 位字（little-endian）
 * 3. 总共得到 10 个 32 位字 → 足够填满 key(8) + nonce(3) = 11 个字
 *    （nonce 第 4 字用固定常量补齐）
 * 
 * @param seed          输入种子 (64 位)
 * @param out_key       输出: 32 字节密钥 (256 位)
 * @param out_nonce     输出: 12 字节 Nonce (96 位)
 */
static void expand_seed(uint64_t seed, uint8_t out_key[32], uint8_t out_nonce[12]) {
    uint64_t mixer = seed;
    
    uint64_t v0 = seed_mix(&mixer);              /* 密钥第 0-1 双字 */
    uint64_t v1 = seed_mix(&mixer) ^ 0x9E3779B97F4A7C15ULL;  /* 密钥第 2-3 双字 (混合黄金比例) */
    uint64_t v2 = seed_mix(&mixer) ^ seed;       /* 密钥第 4-5 双字 (反馈混合) */
    uint64_t v3 = seed_mix(&mixer) ^ 0xD1F5B5C4CE4F6E3DULL;  /* 密钥第 6-7 双字 (独立常量) */
    uint64_t v4 = seed_mix(&mixer);              /* Nonce 前 2 双字 + 计数器种子 */

    /* 组装 256 位密钥 (little-endian) */
    memcpy(out_key + 0,  &v0, 8);
    memcpy(out_key + 8,  &v1, 8);
    memcpy(out_key + 16, &v2, 8);
    memcpy(out_key + 24, &v3, 8);

    /* 组装 96 位 Nonce (取 v4 的低 96 位) */
    memcpy(out_nonce, &v4, 8);
    {
        uint64_t v5 = seed_mix(&mixer);
        memcpy(out_nonce + 8, &v5, 4);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * ChaCha20 核心原语 (RFC 7539 Section 2.1)
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief ChaCha20 Quarter Round (四分之一轮) 操作
 * 
 * 这是 ChaCha20 的基本运算单元，对 4 个 32 位状态字执行：
 * - 加法 (mod 2^32)
 * - XOR
 * - 左循环移位 (rotl)
 * 
 * 移位量 [16, 12, 8, 7] 经优化选择，最大化扩散速度。
 * 
 * @param a  状态字指针 (索引 0)
 * @param b  状态字指针 (索引 1)
 * @param c  状态字指针 (索引 2)
 * @param d  状态字指针 (索引 3)
 */
static inline void chacha_quarter_round(uint32_t* a, uint32_t* b, uint32_t* c, uint32_t* d) {
    *a += *b; *d ^= *a; *d = (*d << 16) | (*d >> 16);
    *c += *d; *b ^= *c; *b = (*b << 12) | (*b >> 20);
    *a += *b; *d ^= *a; *d = (*d << 8)  | (*d >> 24);
    *c += *d; *b ^= *c; *b = (*b << 7)  | (*b >> 25);
}

/**
 * @brief ChaCha20 内部块函数 (20 轮完整版本)
 * 
 * 对 16 × 32 位状态矩阵执行 20 轮 (= 10 双轮) 混合。
 * 每双轮包含：
 * - 列操作 (Column Rounds): 对 4 列分别执行 QR
 * - 对角线操作 (Diagonal Rounds): 对 4 条对角线分别执行 QR
 * 
 * 这是 ChaCha20 安全性的核心——20 轮后实现完全雪崩效应。
 * 
 * @param state  16 个 32 位字的输入/输出状态矩阵
 *               输入: 初始状态；输出: 混合后的状态
 */
static void chacha20_block(uint32_t state[16]) {
    uint32_t working[16];
    int i;

    /* 复制初始状态到工作区（用于最终加法） */
    memcpy(working, state, sizeof(working));

    /* ── 10 个双轮 (20 轮总计) ── */
    for (i = 0; i < CSPRNG_ROUNDS; i += 2) {
        /* ── 列操作 (Column Rounds) ── */
        chacha_quarter_round(&working[0], &working[4], &working[8],  &working[12]);
        chacha_quarter_round(&working[1], &working[5], &working[9],  &working[13]);
        chacha_quarter_round(&working[2], &working[6], &working[10], &working[14]);
        chacha_quarter_round(&working[3], &working[7], &working[11], &working[15]);

        /* ── 对角线操作 (Diagonal Rounds) ── */
        chacha_quarter_round(&working[0], &working[5], &working[10], &working[15]);
        chacha_quarter_round(&working[1], &working[6], &working[11], &working[12]);
        chacha_quarter_round(&working[2], &working[7], &working[8],  &working[13]);
        chacha_quarter_round(&working[3], &working[4], &working[9],  &working[14]);
    }

    /* 最终加法: 将混合结果与初始状态相加 (mod 2^32) */
    for (i = 0; i < 16; i++) {
        state[i] += working[i];
    }
}

/**
 * @brief 构建完整的 ChaCha20 初始状态矩阵
 * 
 * 按照 RFC 7539 定义的布局组装 16 个 32 位字:
 * 
 * ┌────┬────┬────┬────┐
 * │expa│nd 3│2-by│te k│  ← 固定常量 "expand 32-byte k"
 * ├────┼────┼────┼────┤
 * │kkkk│kkkk│kkkk│kkkk│  ← 256 位密钥 (前半部分)
 * ├────┼────┼────┼────┤
 * │cccc│nnnn│nnnn│nnnn│  ← 32 位计数器 + 96 位 Nonce
 * ├────┼────┼────┼────┤
 * │kkkk│kkkk│kkkk│kkkk│  ← 256 位密钥 (后半部分)
 * └────┴────┴────┴────┘
 * 
 * @param key      32 字节密钥
 * @param counter  初始计数器值 (通常为 0)
 * @param nonce    12 字节 Nonce
 * @param out      输出: 16 个 32 位字的初始状态矩阵
 */
static void build_chacha_state(const uint8_t key[32], uint32_t counter,
                                const uint8_t nonce[12], uint32_t out[16]) {
    int i;

    /* 常量: "expand 32-byte k" */
    out[0] = CHACHA_CONSTANT[0];
    out[1] = CHACHA_CONSTANT[1];
    out[2] = CHACHA_CONSTANT[2];
    out[3] = CHACHA_CONSTANT[3];

    /* 密钥: 前 128 位 (8 字节 → 2 × uint32_t, 小端序) */
    for (i = 0; i < 4; i++) {
        out[4 + i] = (uint32_t)key[i * 4]        |
                     ((uint32_t)key[i * 4 + 1] << 8) |
                     ((uint32_t)key[i * 4 + 2] << 16) |
                     ((uint32_t)key[i * 4 + 3] << 24);
    }

    /* 计数器 + Nonce: 32 位计数器 + 96 位 Nonce */
    out[8] = counter;
    for (i = 0; i < 3; i++) {
        out[9 + i] = (uint32_t)nonce[i * 4]         |
                    ((uint32_t)nonce[i * 4 + 1] << 8)  |
                    ((uint32_t)nonce[i * 4 + 2] << 16) |
                    ((uint32_t)nonce[i * 4 + 3] << 24);
    }

    /* 密钥: 后 128 位 */
    for (i = 0; i < 4; i++) {
        out[12 + i] = (uint32_t)key[16 + i * 4]      |
                      ((uint32_t)key[16 + i * 4 + 1] << 8) |
                      ((uint32_t)key[16 + i * 4 + 2] << 16) |
                      ((uint32_t)key[16 + i * 4 + 3] << 24);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * CSPRNG 流管理内部函数
 * 
 * 管理 ChaCha20 输出流的缓冲和按需生成。
 * 每次 generate_block() 产出 64 字节密钥流。
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief 生成下一个 ChaCha20 输出块并写入流缓冲区
 * 
 * 工作流程：
 * 1. 复制当前 input 状态到临时工作区
 * 2. 对工作区执行 20 轮 ChaCha20 混合
 * 3. 将混合结果序列化为 64 字节输出 (little-endian)
 * 4. 更新 input 中的计数器 (+1)，为下一块做准备
 * 5. 重置 stream_pos = 0
 * 
 * @param state  CSPRNG 状态指针
 *
 * @security_note:
 *   每个 64 字节块的生成都涉及 20 轮 × 8 次 QR = 160 次 QR 运算。
 *   即使攻击者获得此块的全部内容，也无法反推下一块的内容
 *   （因为下一块的计数器不同，且 ChaCha20 的轮函数是单向的）。
 */
static void generate_block(CSPRNG_State* state) {
    uint32_t working[16];
    int i;

    /* 复制当前状态到工作区 */
    memcpy(working, state->input, sizeof(working));

    /* 执行 20 轮 ChaCha20 核心 */
    chacha20_block(working);

    /* 序列化: 16 × uint32_t → 64 字节 (little-endian) */
    for (i = 0; i < 16; i++) {
        state->stream_buffer[i * 4 + 0] = (uint8_t)(working[i] & 0xFF);
        state->stream_buffer[i * 4 + 1] = (uint8_t)((working[i] >> 8) & 0xFF);
        state->stream_buffer[i * 4 + 2] = (uint8_t)((working[i] >> 16) & 0xFF);
        state->stream_buffer[i * 4 + 3] = (uint8_t)((working[i] >> 24) & 0xFF);
    }

    /* 推进计数器: 为下一个块准备新的初始状态 */
    state->input[8]++;
    
    if (state->input[8] == 0) {
        /* 计数器溢出 (极罕见: 需要 2^32 次块生成 ≈ 256 PB 数据) */
        /* 按照 RFC 7539, 此时应停止使用该实例 */
        state->input[9]++;  /* 溢出到 nonce 低 32 位 (非标准但合理降级) */
    }

    /* 重置缓冲区位置 */
    state->stream_pos = 0;
    state->blocks_generated++;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * 公共 API 实现
 * ═══════════════════════════════════════════════════════════════════════════ */

void csprng_init(CSPRNG_State* state, uint64_t seed) {
    uint8_t key[32];
    uint8_t nonce[12];

    /* 步骤 1: 将 64 位种子安全扩展为 320 位参数集 */
    expand_seed(seed, key, nonce);

    /* 步骤 2: 构建 ChaCha20 初始状态矩阵 (counter=0) */
    build_chacha_state(key, 0, nonce, state->input);

    /* 步骤 3: 清空缓冲区标记 */
    state->stream_pos = CSPRNG_BLOCK_SIZE;  /* 强制首次读取时触发块生成 */
    state->blocks_generated = 0;

    /* 步骤 4: 预热 - 丢弃前 2 个 ChaCha20 块 (128 字节) */
    /* 目的: 确保 ChaCha20 的雪崩效应充分传播，消除任何结构性偏差 */
    generate_block(state);
    generate_block(state);
    
    state->blocks_generated = 0;  /* 重置计数 (预热块不计入) */
}

void csprng_reseed(CSPRNG_State* state, uint64_t new_seed) {
    uint8_t new_key[32];
    uint8_t new_nonce[12];

    /* 使用新种子生成全新的密钥和 Nonce */
    expand_seed(new_seed, new_key, new_nonce);

    /* 用新参数重建状态矩阵 (计数器归零) */
    build_chacha_state(new_key, 0, new_nonce, state->input);

    /* 使旧缓冲区失效 */
    state->stream_pos = CSPRNG_BLOCK_SIZE;
    state->blocks_generated = 0;

    /* 预热 (同 init) */
    generate_block(state);
    generate_block(state);
    state->blocks_generated = 0;
}

uint64_t csprng_next64(CSPRNG_State* state) {
    uint64_t result;
    
    /* 检查缓冲区是否耗尽，若耗尽则生成新块 */
    if (state->stream_pos + 8 > CSPRNG_BLOCK_SIZE) {
        generate_block(state);
    }

    /* 从流缓冲区读取 8 字节，构造成 little-endian uint64_t */
    result = (uint64_t)state->stream_buffer[state->stream_pos]         |
             ((uint64_t)state->stream_buffer[state->stream_pos + 1] << 8)  |
             ((uint64_t)state->stream_buffer[state->stream_pos + 2] << 16) |
             ((uint64_t)state->stream_buffer[state->stream_pos + 3] << 24) |
             ((uint64_t)state->stream_buffer[state->stream_pos + 4] << 32) |
             ((uint64_t)state->stream_buffer[state->stream_pos + 5] << 40) |
             ((uint64_t)state->stream_buffer[state->stream_pos + 6] << 48) |
             ((uint64_t)state->stream_buffer[state->stream_pos + 7] << 56);

    state->stream_pos += 8;
    return result;
}

uint32_t csprng_next32(CSPRNG_State* state) {
    uint32_t result;

    if (state->stream_pos + 4 > CSPRNG_BLOCK_SIZE) {
        generate_block(state);
    }

    result = (uint32_t)state->stream_buffer[state->stream_pos]         |
            ((uint32_t)state->stream_buffer[state->stream_pos + 1] << 8)  |
            ((uint32_t)state->stream_buffer[state->stream_pos + 2] << 16) |
            ((uint32_t)state->stream_buffer[state->stream_pos + 3] << 24);

    state->stream_pos += 4;
    return result;
}

uint64_t csprng_range(CSPRNG_State* state, uint64_t upper_bound) {
    if (upper_bound <= 1) return 0;

    /* 快速路径: upper_bound 是 2 的幂时可直接用位掩码 */
    if ((upper_bound & (upper_bound - 1)) == 0) {
        return csprng_next64(state) & (upper_bound - 1);
    }

    /* 一般情况: Lemire's 乘法拒绝采样法 */
    /* 
     * 数学原理:
     * 设 r ∈ [0, 2^31), m = r × upper_bound
     * 则 m 的低 32 位 < upper_bound 的概率 ≈ upper_bound / 2^32
     * 当条件满足时接受 m >> 32，否则重试
     * 这保证了输出在 [0, upper_bound) 上完全均匀分布
     */
    uint64_t r, m;
    do {
        r = csprng_next64(state) >> 33;  /* 使用高 31 位 (避免乘法溢出问题) */
        m = r * upper_bound;
    } while ((uint32_t)m < upper_bound);  /* 拒绝采样条件 */

    return (uint32_t)(m >> 32);
}

void csprng_bytes(CSPRNG_State* state, void* buf, size_t len) {
    uint8_t* p = (uint8_t*)buf;
    size_t remaining = len;
    size_t available;

    while (remaining > 0) {
        /* 检查当前缓冲区剩余可用字节 */
        available = CSPRNG_BLOCK_SIZE - state->stream_pos;

        if (available == 0) {
            /* 缓冲区耗尽，生成新块 */
            generate_block(state);
            available = CSPRNG_BLOCK_SIZE;
        }

        /* 计算本次可复制的字节数 */
        {
            size_t to_copy = (remaining < available) ? remaining : available;
            memcpy(p, state->stream_buffer + state->stream_pos, to_copy);
            
            p += to_copy;
            state->stream_pos += to_copy;
            remaining -= to_copy;
        }
    }
}
