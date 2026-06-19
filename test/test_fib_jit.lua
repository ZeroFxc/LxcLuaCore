-- fib(32) JIT 性能测试 - 检查 self_calls 计数
function fib(n)
    if n < 2 then
        return n
    end
    return fib(n - 1) + fib(n - 2)
end

-- ========== JIT OFF ==========
if jit then jit.off() end
local start = os.clock()
local result = fib(32)
local elapsed_off = os.clock() - start
print("========== JIT OFF ==========")
print("fib(32) = " .. result)
print(string.format("time: %.6fs", elapsed_off))

-- ========== JIT ON ==========
if jit then jit.on() end
-- 第一次调用，触发编译
result = fib(32)
local s1 = jit.stats()
print("第一次: compiled=" .. s1.compiled .. ", fallback=" .. s1.fallback .. ", self_calls=" .. s1.self_calls)

-- 第二次调用
start = os.clock()
result = fib(32)
elapsed_on = os.clock() - start
local s2 = jit.stats()
print("========== JIT ON (第二次) ==========")
print("fib(32) = " .. result)
print(string.format("time: %.6fs", elapsed_on))
print("self_calls delta: " .. (s2.self_calls - s1.self_calls))
print(string.format("speedup: %.2fx", elapsed_off / elapsed_on))