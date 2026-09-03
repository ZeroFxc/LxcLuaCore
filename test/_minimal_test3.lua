local code_compile7 = native.compile([[
    .program  factorial
    .regs     16
    @result = R0
    @i      = R1
    @limit  = R2
    @one    = R3

    @result = 1
    @i      = 1
    @limit  = 11
    @one    = 1

    while @i < @limit
        @result = @result * @i
        @i = @i + @one
    end

    ret @result
]])
local nv_c7 = native.new(code_compile7, 256)
r = native.call(nv_c7)
check(r == 3628800, "HLL while: 10! = " .. tostring(r))

-- C8: while 循环 — 累加 1+2+...+100 = 5050
print("\nC8. HLL while: 累加 1..100 = 5050")
local code_compile8 = native.compile([[
    .program  sum_1_to_100
    .regs     16
    @sum   = R0
    @i     = R1
    @limit = R2
    @one   = R3

    @sum   = 0
    @i     = 1
    @limit = 101
    @one   = 1

    while @i < @limit
        @sum = @sum + @i
        @i = @i + @one
    end

    ret @sum
]])
local nv_c8 = native.new(code_compile8, 256)
r = native.call(nv_c8)
check(r == 5050, "HLL while: 1..100 累加 = " .. tostring(r))

-- C9: while 循环 — 斐波那契 fib(20) = 6765
print("\nC9. HLL while: 斐波那契 fib(20) = 6765")
local code_compile9 = native.compile([[
    .program  fibonacci_hll
    .regs     16
    @a     = R0
    @b     = R1
    @i     = R2
    @limit = R3
    @one   = R4
    @c     = R5
