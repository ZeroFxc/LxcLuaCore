-- 测试局部递归函数的 JIT 自递归检测
local function fib(n)
    if n < 2 then return n end
    return fib(n-1) + fib(n-2)
end

-- JIT OFF
jit.off()
local start = os.clock()
local result = fib(32)
local elapsed_off = os.clock() - start
print("========== JIT OFF ==========")
print("fib(32) = " .. result)
print(string.format("time: %.6fs", elapsed_off))

-- JIT ON
jit.on()
result = fib(32)
local s1 = jit.stats()
print("第一次: compiled=" .. s1.compiled .. ", fallback=" .. s1.fallback .. ", self_calls=" .. s1.self_calls)

start = os.clock()
result = fib(32)
elapsed_on = os.clock() - start
local s2 = jit.stats()
print("========== JIT ON (第二次) ==========")
print("fib(32) = " .. result)
print(string.format("time: %.6fs", elapsed_on))
print("self_calls delta: " .. (s2.self_calls - s1.self_calls))
print(string.format("speedup: %.2fx", elapsed_off / elapsed_on))