-- ================================================================
-- 密码学库边界测试: sha256, aes, crc, csprng
-- ================================================================
local passed, failed = 0, 0
local function check(name, ok, msg)
    if ok then passed = passed + 1
    else failed = failed + 1; print("  FAIL: " .. (msg or name)) end
end

-- ================================================================
-- 1. SHA256
-- ================================================================
do
    local s = require("sha256")
    check("sha256 module loaded", type(s) == "table")
    check("sha256.SHA256 exists", type(s.SHA256) == "function")
    check("sha256.HMAC_SHA256 exists", type(s.HMAC_SHA256) == "function")

    -- 空字符串
    local r = s.SHA256("")
    check("sha256 empty", type(r) == "string" and #r == 64, "empty→hex len=64")
    check("sha256 empty known", r == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")

    -- 已知值 "abc"
    check("sha256 abc", s.SHA256("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")

    -- 超长字符串
    local long = string.rep("x", 10000)
    check("sha256 long string", type(s.SHA256(long)) == "string" and #s.SHA256(long) == 64)

    -- nil 输入
    local ok, err = pcall(function() s.SHA256(nil) end)
    check("sha256 nil", not ok)

    -- HMAC 空消息空密钥
    local h = s.HMAC_SHA256("", "")
    check("hmac empty", type(h) == "string" and #h == 64)
    check("hmac empty known", h == "b613679a0814d9ec772f95d778c35fc5ff1697c493715653c6c712144292c5ad")

    -- HMAC known test
    check("hmac key+msg", s.HMAC_SHA256("key", "The quick brown fox") == "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8")

    -- 超长 key
    local long_key = string.rep("k", 2000)
    local r2 = s.HMAC_SHA256(long_key, "data")
    check("hmac long key", type(r2) == "string" and #r2 == 64)

    -- 二进制数据
    local r3 = s.SHA256("\0\1\2\3")
    check("sha256 binary", type(r3) == "string")
end

-- ================================================================
-- 2. AES
-- ================================================================
do
    local aes = require("aes")
    check("aes module loaded", type(aes) == "table")

    -- 16-byte key for AES-128
    local key = "0123456789abcdef"
    local plain = "Hello World!1234"  -- 16 bytes

    local enc = aes.AES_ECB_encrypt(plain, key)
    check("aes ecb encrypt", type(enc) == "string")
    check("aes ecb encrypt len", #enc == 16)

    local dec = aes.AES_ECB_decrypt(enc, key)
    check("aes ecb decrypt", dec == plain)

    -- 空输入
    local ok = pcall(function() aes.AES_ECB_encrypt("", key) end)
    check("aes empty input", ok)

    -- 错误密钥长度
    local ok2, err2 = pcall(function() aes.AES_ECB_encrypt(plain, "short") end)
    check("aes bad key", not ok2)

    -- CBC 模式
    local iv = "0123456789abcdef"
    local cbc_enc = aes.AES_CBC_encrypt_buffer(plain, key, iv)
    check("aes cbc encrypt", type(cbc_enc) == "string" and #cbc_enc == 16)
    local cbc_dec = aes.AES_CBC_decrypt_buffer(cbc_enc, key, iv)
    check("aes cbc decrypt", cbc_dec == plain)

    -- CTR 模式
    local nonce = "\0\0\0\0\0\0\0\0"
    local ctr = aes.AES_CTR_xcrypt_buffer(plain, key, nonce)
    check("aes ctr", type(ctr) == "string" and #ctr == 16)
    local ctr2 = aes.AES_CTR_xcrypt_buffer(ctr, key, nonce)
    check("aes ctr roundtrip", ctr2 == plain)
end

-- ================================================================
-- 3. CRC32
-- ================================================================
do
    local crc = require("crc")
    check("crc module loaded", type(crc) == "table")
    check("crc.naga_crc32 exists", type(crc.naga_crc32) == "function")

    -- 空字符串
    local r = crc.naga_crc32("")
    check("crc32 empty", type(r) == "number")

    -- 已知值
    local r2 = crc.naga_crc32("123456789")
    check("crc32 known", type(r2) == "number")

    -- nil
    local ok = pcall(function() crc.naga_crc32(nil) end)
    check("crc32 nil", not ok)

    -- 大尺寸数据
    local r3 = crc.naga_crc32(string.rep("A", 100000))
    check("crc32 large", type(r3) == "number")

    -- crc32int
    if crc.naga_crc32int then
        local r4 = crc.naga_crc32int({1, 2, 3})
        check("crc32int table", type(r4) == "number")
    end
end

-- ================================================================
-- 4. CSPRNG (加密安全随机数)
-- ================================================================
do
    local cs = require("csprng")
    check("csprng module loaded", type(cs) == "table")

    check("csprng_init", type(cs.csprng_init) == "function")
    check("csprng_next64", type(cs.csprng_next64) == "function")
    check("csprng_next32", type(cs.csprng_next32) == "function")
    check("csprng_range", type(cs.csprng_range) == "function")
    check("csprng_bytes", type(cs.csprng_bytes) == "function")
    check("csprng_reseed", type(cs.csprng_reseed) == "function")

    -- 初始化
    local init = cs.csprng_init(256)
    check("csprng_init return", type(init) == "boolean" or type(init) == "number")

    -- 获取随机数
    local r64 = cs.csprng_next64()
    check("csprng_next64", type(r64) == "number")

    local r32 = cs.csprng_next32()
    check("csprng_next32", type(r32) == "number")

    -- 范围随机
    local r_range = cs.csprng_range(1, 100)
    check("csprng_range", type(r_range) == "number" and r_range >= 1 and r_range <= 100)

    -- 随机字节
    local bytes = cs.csprng_bytes(32)
    check("csprng_bytes", type(bytes) == "string" and #bytes == 32)

    -- 边界: 0字节
    local empty_bytes = cs.csprng_bytes(0)
    check("csprng_bytes 0", type(empty_bytes) == "string" and #empty_bytes == 0)

    -- 重播种
    local reseed = cs.csprng_reseed()
    check("csprng_reseed", type(reseed) == "boolean" or type(reseed) == "number")

    -- 负数或超大的 bytes 参数
    local ok, err = pcall(function() cs.csprng_bytes(-1) end)
    check("csprng_bytes negative", not ok)

    -- 多次获取确保不重复 (概率验证)
    local vals = {}
    for i = 1, 20 do
        vals[i] = cs.csprng_next64()
    end
    local unique = true
    for i = 1, 19 do
        for j = i+1, 20 do
            if vals[i] == vals[j] then unique = false end
        end
    end
    check("csprng uniqueness", unique, "20 samples should be unique")
end

print(string.format("\n=== 密码学库完成: %d 通过, %d 失败 ===", passed, failed))
if failed > 0 then os.exit(1) end