-- 测试仅CFF标志
local flags = 1  -- 仅CFF

local code = "return 3 + 5"
local fn = load(code)
print("原始结果: " .. fn())

local obf_dump = string.dump(fn, {
    obfuscate = flags,
    seed = 12345,
    strip = false
})
print("混淆字节码长度: " .. #obf_dump)

local obf_fn = load(obf_dump)
print("混淆后结果: " .. obf_fn())
print("仅CFF测试完成")
