-- 测试 RANDOM_NOP + STATE_ENCODE 完全兼容
local code = "local sum = 0; for i = 1, 10 do sum = sum + i; end; return sum"

-- 测试所有包含 RANDOM_NOP + STATE_ENCODE 的组合
local combos = {
    {name = "CFF+RANDOM_NOP+STATE_ENCODE", flags = 1|512|8},
    {name = "ALL (含NESTED)", flags = 1|2|4|8|512},
}

for _, c in ipairs(combos) do
    io.write("[" .. c.name .. " (" .. c.flags .. ")]... ")
    io.flush()
    local fn = load(code)
    local obf_dump = string.dump(fn, {
        obfuscate = c.flags, seed = 12345, strip = false
    })
    local obf_fn = load(obf_dump)
    local ok, res = pcall(obf_fn)
    if ok and res == 55 then
        print("✓ len=" .. #obf_dump)
    else
        print("✗ res=" .. tostring(res))
    end
end
