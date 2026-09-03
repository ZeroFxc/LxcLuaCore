-- 斐波那契 JIT ON/OFF 对比测试
local function fib(n)
    if n <= 1 then
        return n
    end
    return fib(n - 1) + fib(n - 2)
end

-- 预热
fib(1)

-- ===== JIT OFF =====
print("========== JIT OFF ==========")
jit.off()
local t1 = os.clock()
local r1 = fib(32)
local t2 = os.clock()
print("fib(32) = " .. r1)
print("expected: 2178309")
print("correct: " .. tostring(r1 == 2178309))
print("time: " .. (t2 - t1) .. "s")

-- ===== JIT ON =====
print("")
print("========== JIT ON ==========")
jit.on()
fib(1)  -- 重新触发 JIT 编译
local t3 = os.clock()
local r2 = fib(32)
local t4 = os.clock()
print("fib(32) = " .. r2)
print("expected: 2178309")
print("correct: " .. tostring(r2 == 2178309))
print("time: " .. (t4 - t3) .. "s")

print("")
print("speedup: " .. ((t2 - t1) / (t4 - t3)) .. "x")