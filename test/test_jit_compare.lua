-- JIT ON/OFF 对比测试
local function sum(n)
    local s = 0
    for i = 1, n do
        s = s + i
    end
    return s
end

-- 预热，触发 JIT 编译
sum(1)

-- ===== JIT OFF =====
print("========== JIT OFF ==========")
jit.off()
local t1 = os.clock()
local r1 = sum(100000000)
local t2 = os.clock()
print("sum(100000000) = " .. r1)
print("expected:      5000000050000000")
print("correct:      " .. tostring(r1 == 5000000050000000))
print("time: " .. (t2 - t1) .. "s")

-- ===== JIT ON =====
print("")
print("========== JIT ON ==========")
jit.on()
sum(1)  -- 重新触发 JIT 编译
local t3 = os.clock()
local r2 = sum(100000000)
local t4 = os.clock()
print("sum(100000000) = " .. r2)
print("expected:      5000000050000000")
print("correct:      " .. tostring(r2 == 5000000050000000))
print("time: " .. (t4 - t3) .. "s")

print("")
print("speedup: " .. ((t2 - t1) / (t4 - t3)) .. "x")