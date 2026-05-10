-- ============================================================
-- JIT 性能基准测试
-- 使用 jit.on() / jit.off() + os.clock() 对比开关JIT的性能差异
-- ============================================================

local function run_bench(name, func, iterations)
    func()
    collectgarbage("collect")

    local start = os.clock()
    for _ = 1, iterations do
        func()
    end
    local elapsed = os.clock() - start
    print(string.format("  %-35s: %.6f 秒  (%.2f 微秒/次)",
        name, elapsed, elapsed / iterations * 1e6))
    return elapsed
end

local function compare(name, func, iterations)
    print("\n[" .. name .. "]  (迭代次数: " .. iterations .. ")")
    print("  --- JIT 关闭 ---")
    jit.off()
    local ok, t_off = pcall(run_bench, "JIT OFF", func, iterations)
    if not ok then
        print("  JIT OFF 出错: " .. tostring(t_off))
    end

    print("  --- JIT 开启 ---")
    jit.on()
    local ok2, t_on = pcall(run_bench, "JIT ON ", func, iterations)
    if not ok2 then
        print("  JIT ON  出错: " .. tostring(t_on))
    end
    jit.off()

    if ok and ok2 then
        if t_on < t_off then
            local speedup = t_off / t_on
            print(string.format("  >>> 加速比: %.2fx  (%.1f%% 更快)", speedup, (speedup - 1) * 100))
        elseif t_on > t_off then
            print(string.format("  >>> 减速: %.2fx  (JIT反而慢)", t_off / t_on))
        else
            print("  >>> 无差异")
        end
    end
    collectgarbage("collect")
end

-- ============================================================
-- 测试用例
-- ============================================================

print(string.rep("=", 60))
print("  LXCLUA JIT 性能基准测试")
print("  jit.status() = " .. tostring(jit.status()))
print(string.rep("=", 60))

-- 测试1: 整数四则运算 + 取模 (用模运算限制值域, 防止溢出)
local function bench_arith_int()
    local a, b, c, d = 100, 200, 300, 400
    for i = 1, 500 do
        a = (a + b) * (c - d)
        b = a % 0xFFFF + b * 2
        c = c * 3 % 0xFFFFFFF
        d = d + b - c
        -- 用模运算把值限制在整数范围内
        a = a % 0xFFFFFFFF
        b = b % 0xFFFFFFFF
        c = c % 0xFFFFFFFF
        d = d % 0xFFFFFFFF
    end
end
compare("整数算术 (ADD/SUB/MUL/MOD)", bench_arith_int, 500)

-- 测试2: 循环累加
local function bench_loop_sum()
    local s = 0
    for i = 1, 2000 do
        s = s + i
    end
    return s
end
compare("循环累加 (1~2000)", bench_loop_sum, 2000)

-- 测试3: 浮点数运算
local function bench_arith_float()
    local a, b, c = 1.5, 3.14, 2.71
    for i = 1, 500 do
        a = a * b + c
        b = a / (b + 0.001)
        c = (a - b) * (a + b)
    end
end
compare("浮点算术密集循环", bench_arith_float, 500)

-- 测试4: 常量折叠
local function bench_const_fold()
    local a = 0
    for i = 1, 1000 do
        a = a + (100 + 200 * 3 + 10 * 4 + 5 * 6)
    end
end
compare("常量表达式 (编译期折叠)", bench_const_fold, 2000)

-- 测试5: 值移动/复制
local function bench_moves()
    local a, b, c, d, e = 1, 2, 3, 4, 5
    for i = 1, 1000 do
        a = b
        b = c
        c = d
        d = e
        e = a
    end
end
compare("简单值移动 (MOV密集)", bench_moves, 2000)

-- 测试6: 位运算密集 (用掩码限制值域防止符号位问题)
local function bench_bitwise()
    local mask = 0x7FFFFFFF
    local a, b = 0x12345678, 0x0ABCDEF0
    for i = 1, 500 do
        a = ((a & b) | (a << 4)) & mask
        b = ((b ~ a) >> 2) & mask
        a = (a & 0x3FFFFFFF)
        b = (b | (a << 8)) & mask
    end
end
compare("位运算密集", bench_bitwise, 1000)

-- 测试7: 函数调用 + 算术
local function helper_add(a, b)
    return a + b * 2 + 3
end
local function bench_func_calls()
    local s = 0
    for i = 1, 500 do
        s = s + helper_add(i, i + 1)
    end
end
compare("带函数调用的循环", bench_func_calls, 1000)

-- 测试8: 表操作
local function bench_table_ops()
    local t = {10, 20, 30, 40, 50}
    for i = 1, 300 do
        t[2] = t[1] + t[3]
        t[4] = t[2] * t[5]
    end
end
compare("简单表操作", bench_table_ops, 1000)

-- 测试9: 极短函数
local function bench_short()
    return 1 + 1
end
compare("极短函数 (单次加法)", bench_short, 10000)

-- 测试10: 比较/跳转
local function bench_compare_branch()
    local a, b = 0, 0
    for i = 1, 1000 do
        if i < 500 then
            if i > 250 then
                a = a + 1
            else
                b = b + 1
            end
        else
            if i > 750 then
                a = a + 2
            else
                b = b + 2
            end
        end
    end
end
compare("比较跳转密集", bench_compare_branch, 500)

-- 测试11: 多项式计算 (值受控, 不会溢出)
local function bench_poly()
    local result = 0
    for i = 1, 1000 do
        result = (result + i * i + i * 3 + 7) % 0xFFFFFFFF
    end
    return result
end
compare("整数多项式计算", bench_poly, 1000)

-- 测试12: 斐波那契 (每项取模保持整数范围)
local function bench_fib()
    local a, b = 0, 1
    for i = 1, 200 do
        a, b = b, (a + b) % 0xFFFFFFFF
    end
    return a
end
compare("斐波那契迭代 (200项)", bench_fib, 500)

-- 测试13: 地板除 + 取模
local function bench_divmod()
    local a, b = 987654321, 12345
    for i = 1, 500 do
        a = a // b + a % b
        b = b * 3 + (a // 7)
        a = a % 0xFFFFFFFF
        b = b % 0xFFFFFFFF
    end
end
compare("整数地板除+取模", bench_divmod, 500)

-- 测试14: 幂运算 (^) 受控, 小指数
local function bench_pow_small()
    local a = 0
    for i = 1, 300 do
        local n = i % 10 + 2   -- 指数 2~11, 不会溢出
        a = (a + (n ^ 3)) % 0xFFFFFFFF
    end
    return a
end
compare("幂运算 (小指数)", bench_pow_small, 500)

-- ============================================================
print("\n" .. string.rep("=", 60))
print("  测试完成!")
print("  注意: 当前 JIT MVP 版本中, JIT执行后解释器仍会重跑一遍,")
print("  因此用 os.clock() 测量的JIT ON时间包含了 JIT执行+解释器执行")
print("  双重开销, 加速比会被严重低估甚至显示为减速。")
print("  建议关注 JIT ON 相比 JIT OFF 的耗时差异变化趋势。")
print(string.rep("=", 60))