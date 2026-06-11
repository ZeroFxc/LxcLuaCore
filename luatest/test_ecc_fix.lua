-- ECC 修复验证测试
local ecc = require("ecc")
local function hex(s) return s end

print("=== Test 1: Key Generation ===")
local key1 = ecc.key.generate()
local key2 = ecc.key.generate()
print("Key1 priv:", key1.private_key)
print("Key1 pub:", key1.public_key)
print("Key2 priv:", key2.private_key)
print("Key2 pub:", key2.public_key)
print("PASS: Key generation works")

print("\n=== Test 2: ECDSA Sign + Verify (valid) ===")
local msg = "hello"
local sig = ecc.sign(msg, key1.private_key)
print("Signature r:", sig.r)
print("Signature s:", sig.s)
local result = ecc.verify(msg, sig, key1.public_key)
print("Verify valid:", result)
assert(result == true, "FAIL: verify should return true for valid signature")
print("PASS: Sign + Verify works correctly")

print("\n=== Test 3: ECDSA Verify (invalid/tampered) ===")
local result_bad = ecc.verify("bad", sig, key1.public_key)
print("Verify tampered:", result_bad)
assert(result_bad == false, "FAIL: verify should return false for tampered message")
print("PASS: Tampered message correctly rejected")

print("\n=== Test 4: ECDH Shared Secret ===")
local shared1 = ecc.ecdh(key1.private_key, key2.public_key)
local shared2 = ecc.ecdh(key2.private_key, key1.public_key)
print("Shared1:", shared1)
print("Shared2:", shared2)
assert(shared1 == shared2, "FAIL: ECDH shared secrets should match")
print("PASS: ECDH shared secrets match")

print("\n=== Test 5: key.from_private ===")
local key3 = ecc.key.from_private(key1.private_key)
assert(key3.private_key == key1.private_key, "FAIL: private key mismatch")
assert(key3.public_key == key1.public_key, "FAIL: public key mismatch")
print("PASS: from_private derives correct public key")

print("\n=== Test 6: encode/decode roundtrip ===")
local decoded = ecc.decode.public(key1.public_key)
print("Decoded x:", decoded.x)
print("Decoded y:", decoded.y)
local re_encoded = ecc.encode.public(decoded, false)
assert(re_encoded == key1.public_key, "FAIL: encode(decode(x)) != x")
print("PASS: encode/decode roundtrip works")

print("\n=== Test 7: ECDH with self-generated keys ===")
-- Generate a fresh pair and test ECDH
local a = ecc.key.generate()
local b = ecc.key.generate()
local s1 = ecc.ecdh(a.private_key, b.public_key)
local s2 = ecc.ecdh(b.private_key, a.public_key)
assert(s1 == s2, "FAIL: ECDH between fresh keys")
print("PASS: ECDH between fresh keys works")

print("\n=== ALL TESTS PASSED ===")