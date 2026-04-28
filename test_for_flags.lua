-- 测试 for循环 在不同标志组合下
local code = "local sum = 0; for i = 1, 10 do sum = sum + i; end; return sum"

local test_flags = {
    {name = "仅CFF", flags = 1},
    {name = "CFF+BLOCK_SHUFFLE", flags = 1|2},
    {name = "CFF+BOGUS_BLOCKS", flags = 1|4},
    {name = "CFF+RANDOM_NOP", flags = 1|512},
    {name = "CFF+STATE_ENCODE", flags = 1|8},
    {name = "CFF+BLOCK_SHUFFLE+STATE_ENCODE", flags = 1|2|8},
    {name = "CFF+BOGUS_BLOCKS+STATE_ENCODE", flags = 1|4|8},
    {name = "CFF+RANDOM_NOP+STATE_ENCODE", flags = 1|512|8},
    {name = "全部无NESTED", flags = 1|2|4|8|512},
}

for _, tf in ipairs(test_flags) do
    io.write("[" .. tf.name .. "]... ")
    io.flush()
    
    local fn = load(code)
    local obf_dump = string.dump(fn, {
        obfuscate = tf.flags,
        seed = 12345,
        strip = false
    })
    
    local obf_fn = load(obf_dump)
    local ok, res = pcall(obf_fn)
    if ok and res == 55 then
        print("✓ (长度=" .. #obf_dump .. ")")
    else
        print("✗ 结果=" .. tostring(res))
    end
end
