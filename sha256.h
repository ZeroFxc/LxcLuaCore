#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

#define SHA256_DIGEST_SIZE 32  // SHA-256 produces 32 raw bytes (256 bits)

/**
 * Computes the SHA-256 hash of the given input message.
 *
 * @param msg        Pointer to the input message (raw bytes).
 * @param msgLen     Length of the input message in bytes.
 * @param digest     Pointer to a buffer of at least SHA256_DIGEST_SIZE bytes.
 *                   The resulting 32-byte hash will be written here.
 *
 * @return           0 on success, non-zero on failure.
 */
int SHA256(const uint8_t* msg, size_t msgLen, uint8_t* digest);

/**
 * Computes HMAC-SHA256 (Hash-based Message Authentication Code).
 * 用于字节码文件的全局完整性验证，防止篡改攻击。
 *
 * @param key        HMAC密钥指针（至少32字节）。
 * @param keyLen     密钥长度（字节）。
 * @param data       待签名的数据指针。
 * @param dataLen    数据长度（字节）。
 * @param digest     输出缓冲区（必须至少32字节）。
 * @return           0表示成功，非0表示失败。
 */
int HMAC_SHA256(const uint8_t* key, size_t keyLen,
                const uint8_t* data, size_t dataLen,
                uint8_t* digest);

#ifdef __cplusplus
}
#endif
