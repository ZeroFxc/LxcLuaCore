-- 逐个测试混淆器，找出哪个用例导致超时
-- Flag值: CFF=1, BLOCK_SHUFFLE=2, BOGUS_BLOCKS=4, RANDOM_NOP=512, STATE_ENCODE=8
local flags = 1 | 2 | 4 | 512 | 8

local test_cases = {
    {name = "简单加法", code = "return 3 + 5", expected = 8},
    {name = "条件分支", code = "local a, b = 10, 20; if a > b then return a else return b end", expected = 20},
    {name = "for循环", code = "local sum = 0; for i = 1, 10 do sum = sum + i; end; return sum", expected = 55},
    {name = "while循环", code = "local i, sum = 1, 0; while i <= 5 do sum = sum + i*i; i = i+1; end; return sum", expected = 55},
    {name = "repeat-until", code = "local x = 10; repeat x = x-1 until x <= 5; return x", expected = 5},
    {name = "嵌套条件", code = "local n = 50; if n < 0 then return -1 elseif n == 0 then return 0 elseif n < 100 then return 1 else return 2 end", expected = 1},
}

print("=== 逐个测试混淆器 ===\n")

for i, tc in ipairs(test_cases) do
    io.write("[" .. i .. "/" .. #test_cases .. "] " .. tc.name .. "... ")
    io.flush()
    
    local fn = load(tc.code)
    if not fn then
        print("加载失败")
        goto continue
    end
    
    local dump_ok, obf_dump = pcall(string.dump, fn, {
        obfuscate = flags,
        seed = 12345,
        strip = false
    })
    if not dump_ok then
        print("dump失败: " .. tostring(obf_dump))
        goto continue
    end
    
    local load_ok, obf_fn = pcall(load, obf_dump)
    if not load_ok then
        print("加载失败: " .. tostring(obf_fn))
        goto continue
    end
    
    local call_ok, result = pcall(obf_fn)
    if not call_ok then
        print("执行失败: " .. tostring(result))
        goto continue
    end
    
    if result == tc.expected then
        print("✓ 通过 (结果=" .. result .. ")")
    else
        print("✗ 失败! 期望=" .. tc.expected .. ", 实际=" .. result)
    end
    
    ::continue::
end
