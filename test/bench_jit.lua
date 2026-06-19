-- 简单 JIT 性能对比
local function measure(name, func, n)
    jit.on()
    func(1)  -- 预热
    local t1 = os.clock()
    local r1 = func(n)
    t1 = os.clock() - t1

    jit.off()
    func(1)
    local t2 = os.clock()
    local r2 = func(n)
    t2 = os.clock() - t2

    print(string.format("%s  JIT:%.3fs  NOJIT:%.3fs  加速:%.2fx  %s",
        name, t1, t2, t2/t1, r1==r2 and "OK" or "ERR"))
end

-- 1. 纯循环
print("1. 纯循环累加 (1亿次)")
local function sum_loop(n)
    local s = 0
    for i = 1, n do
        s = s + i
    end
    return s
end
measure("sum_loop", sum_loop, 100000000)

-- 2. 斐波那契
print("\n2. 斐波那契递归")
local function fib(n)
    if n < 2 then return n end
    return fib(n-1) + fib(n-2)
end
measure("fib", fib, 35)

-- 3. 函数调用
print("\n3. 函数调用密集 (1000万次)")
local function add(a, b)
    return a + b
end
local function func_call(n)
    local s = 0
    for i = 1, n do
        s = add(s, i)
    end
    return s
end
measure("func_call", func_call, 10000000)