function fib(n)
    if n < 2 then return n end
    return fib(n-1) + fib(n-2)
end
local f = io.open("test/fib_dump.out", "wb")
f:write(string.dump(fib, true))
f:close()