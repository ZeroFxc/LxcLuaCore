-- 快速 JIT 测试
jit.on()

-- 纯循环
local function sum(n)
    local s = 0
    for i = 1, n do s = s + i end
    return s
end

sum(1)  -- 预热
local t1 = os.clock()
local r = sum(100000000)
t1 = os.clock() - t1
print("sum_loop:", string.format("%.3fs", t1), "r=", r)

jit.off()
sum(1)
local t2 = os.clock()
r = sum(100000000)
t2 = os.clock() - t2
print("sum_loop(NOJIT):", string.format("%.3fs", t2), "r=", r)
print("加速:", string.format("%.2fx", t2 / t1))