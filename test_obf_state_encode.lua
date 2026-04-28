-- 测试 STATE_ENCODE 标志
local flags = 1 | 8  -- CFF + STATE_ENCODE

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
local ok, res = pcall(obf_fn)
if ok then
    print("混淆后结果: " .. res)
else
    print("执行失败: " .. tostring(res))
end
print("CFF+STATE_ENCODE测试完成")
