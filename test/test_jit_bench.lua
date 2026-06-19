-- JIT 性能对比测试 (计时在主脚本中，避免 JIT 编译 bench 函数)
local function bench(fn, iterations)
    local start = os.clock()
    for i = 1, iterations do
        fn()
    end
    return os.clock() - start
end

-- 测试函数定义
local function sum_loop()
    local s = 0
    for i = 1, 100000 do s = s + i end
    return s
end
local function nested_loop()
    local s = 0
    for i = 1, 1000 do
        for j = 1, 100 do
            s = s + 1
        end
    end
    return s
end
local function cond_loop()
    local s = 0
    for i = 1, 100000 do
        if i % 2 == 0 then s = s + i end
    end
    return s
end
local function mul_loop()
    local s = 0
    for i = 1, 100000 do s = s + i * 3 end
    return s
end
local function step_loop()
    local s = 0
    for i = 1, 100000, 3 do s = s + i end
    return s
end
local function rev_loop()
    local s = 0
    for i = 100000, 1, -1 do s = s + i end
    return s
end

local ITER = 100

-- 预热
sum_loop(); nested_loop(); cond_loop(); mul_loop(); step_loop(); rev_loop()

-- JIT OFF
print("========== JIT OFF ==========")
jit.off()
local t1 = bench(sum_loop, ITER); print("  sum_loop:    " .. t1 .. " sec")
local t2 = bench(nested_loop, ITER); print("  nested_loop: " .. t2 .. " sec")
local t3 = bench(cond_loop, ITER); print("  cond_loop:   " .. t3 .. " sec")
local t4 = bench(mul_loop, ITER); print("  mul_loop:    " .. t4 .. " sec")
local t5 = bench(step_loop, ITER); print("  step_loop:   " .. t5 .. " sec")
local t6 = bench(rev_loop, ITER); print("  rev_loop:    " .. t6 .. " sec")
local total_off = t1 + t2 + t3 + t4 + t5 + t6

-- JIT ON
print("")
print("========== JIT ON ==========")
jit.on()
local t1j = bench(sum_loop, ITER); print("  sum_loop:    " .. t1j .. " sec")
local t2j = bench(nested_loop, ITER); print("  nested_loop: " .. t2j .. " sec")
local t3j = bench(cond_loop, ITER); print("  cond_loop:   " .. t3j .. " sec")
local t4j = bench(mul_loop, ITER); print("  mul_loop:    " .. t4j .. " sec")
local t5j = bench(step_loop, ITER); print("  step_loop:   " .. t5j .. " sec")
local t6j = bench(rev_loop, ITER); print("  rev_loop:    " .. t6j .. " sec")
local total_on = t1j + t2j + t3j + t4j + t5j + t6j

-- 对比
print("")
print("========== 对比 ==========")
local function ratio(a, b)
    if b == 0 then return "N/A" end
    local r = a / b
    return r
end
print("sum_loop:     " .. t1 .. " -> " .. t1j .. "  (" .. ratio(t1, t1j) .. "x)")
print("nested_loop:  " .. t2 .. " -> " .. t2j .. "  (" .. ratio(t2, t2j) .. "x)")
print("cond_loop:    " .. t3 .. " -> " .. t3j .. "  (" .. ratio(t3, t3j) .. "x)")
print("mul_loop:     " .. t4 .. " -> " .. t4j .. "  (" .. ratio(t4, t4j) .. "x)")
print("step_loop:    " .. t5 .. " -> " .. t5j .. "  (" .. ratio(t5, t5j) .. "x)")
print("rev_loop:     " .. t6 .. " -> " .. t6j .. "  (" .. ratio(t6, t6j) .. "x)")
print("total:        " .. total_off .. " -> " .. total_on .. "  (" .. ratio(total_off, total_on) .. "x)")