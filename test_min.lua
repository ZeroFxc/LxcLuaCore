-- 最小化测试: 仅CFF+RANDOM_NOP
local code = "local sum = 0; for i = 1, 10 do sum = sum + i; end; return sum"
local fn = load(code)

-- 测试1: 仅CFF+RANDOM_NOP (无STATE_ENCODE)
local flags1 = 1 | 512
local obf_dump1 = string.dump(fn, {obfuscate = flags1, seed = 12345, strip = false})
local obf_fn1 = load(obf_dump1)
local ok1, res1 = pcall(obf_fn1)
print("CFF+RANDOM_NOP: " .. (ok1 and tostring(res1) or "ERROR: " .. tostring(res1)))

-- 测试2: 仅CFF+STATE_ENCODE (无RANDOM_NOP)
fn = load(code)
local flags2 = 1 | 8
local obf_dump2 = string.dump(fn, {obfuscate = flags2, seed = 12345, strip = false})
local obf_fn2 = load(obf_dump2)
local ok2, res2 = pcall(obf_fn2)
print("CFF+STATE_ENCODE: " .. (ok2 and tostring(res2) or "ERROR: " .. tostring(res2)))

-- 测试3: 三者同时
fn = load(code)
local flags3 = 1 | 512 | 8
local obf_dump3 = string.dump(fn, {obfuscate = flags3, seed = 12345, strip = false})
local obf_fn3 = load(obf_dump3)
local ok3, res3 = pcall(obf_fn3)
print("CFF+RANDOM_NOP+STATE_ENCODE: " .. (ok3 and tostring(res3) or "ERROR: " .. tostring(res3)))
