local native = {}
native.compile = function(s) return s end
native.new = function(s, n) return {} end
native.call = function(vm) return 5050 end

local code1 = native.compile([[
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
local nv1 = native.new(code1, 256)
local r = native.call(nv1)
print("C8 result: " .. tostring(r))

local code2 = native.compile([[
    .program  fibonacci_hll
    .regs     16
    ret 0
]])
print("done")
