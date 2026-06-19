local function bench(name, times, func)
    local start = os.clock()
    for i = 1, times do
        func()
    end
    local cost = os.clock() - start
    print(string.format("[%s] 循环%d次 耗时:%.4fs 单次平均:%.6fs", name, times, cost, cost/times))
end

-- 测试1：简单匹配
local test1_str = "a1b2c3d4e5f6g7h8i9j0"


-- 测试2：PCRE2 语法版本
local reg2 = "[a-z]+\\d+"
print("")
print("测试2: [a-z]+\\d+ 匹配 " .. test1_str)
print("  结果:", string.match(test1_str, reg2))

bench("PCRE2语法正则", 10000000, function()
    string.match(test1_str, reg2)
end)