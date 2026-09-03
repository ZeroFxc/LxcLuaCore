-- Test: closures, upvalues, higher-order functions
local function make_adder(n)
    return function(x) return x + n end
end

local add5 = make_adder(5)
local add10 = make_adder(10)
print(add5(3))
print(add10(3))

-- shared upvalue
local function make_counter()
    local count = 0
    local function inc() count = count + 1; return count end
    local function dec() count = count - 1; return count end
    return inc, dec
end

local inc, dec = make_counter()
print(inc(), inc(), inc())
print(dec())

-- closure capturing loop variable
local fns = {}
for i = 1, 3 do
    fns[i] = function() return i end
end
print(fns[1](), fns[2](), fns[3]())

-- recursion with closure
local function memoize(fn)
    local cache = {}
    return function(n)
        if cache[n] then return cache[n] end
        cache[n] = fn(n)
        return cache[n]
    end
end

local function slow_fib(n)
    if n < 2 then return n end
    return slow_fib(n - 1) + slow_fib(n - 2)
end

local fast_fib = memoize(slow_fib)
print(fast_fib(15))

-- function as argument
local function apply(fn, x) return fn(x) end
print(apply(add5, 100))
print(apply(function(x) return x * x end, 7))
