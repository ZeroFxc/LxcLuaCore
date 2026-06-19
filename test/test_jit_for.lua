-- 测试 for 循环 JIT 加速效果
local function test_for(n)
    local sum = 0
    for i = 1, n do
        sum = sum + i
    end
    return sum
end

local n = 100000000

-- 先预热一次，触发 JIT 编译
print("--- 预热 ---")
jit.on()
local r = test_for(100)
print("预热结果:", r)
print("JIT stats:", jit.stats())

-- JIT 测试
print("\n--- JIT ON ---")
local start = os.clock()
local r1 = test_for(n)
local elapsed_jit = os.clock() - start
print("结果:", r1)
print("耗时:", string.format("%.3f", elapsed_jit), "秒")
print("JIT stats:", jit.stats())

-- 关闭 JIT 测试
print("\n--- JIT OFF ---")
jit.off()
local start = os.clock()
local r2 = test_for(n)
local elapsed_nojit = os.clock() - start
print("结果:", r2)
print("耗时:", string.format("%.3f", elapsed_nojit), "秒")

print("\n--- 加速比 ---")
print("JIT 耗时:", string.format("%.3f", elapsed_jit), "秒")
print("解释器耗时:", string.format("%.3f", elapsed_nojit), "秒")
print("加速比:", string.format("%.2fx", elapsed_nojit / elapsed_jit))