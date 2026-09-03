-- Test: function calls, recursion, multiple return values
local function add(a, b) return a + b end
local function multi() return 10, 20, 30 end

local function fib(n)
    if n < 2 then return n end
    return fib(n - 1) + fib(n - 2)
end

local x, y, z = multi()
print(add(3, 4))
print(x, y, z)
print(fib(10))

-- nested calls
print(add(add(1, 2), add(3, 4)))

-- varargs
local function sum(...)
    local s = 0
    for _, v in ipairs({...}) do s = s + v end
    return s
end
print(sum(1, 2, 3, 4, 5))
print(sum(10, 20))
