jit.on()
local function sum(n)
    local s = 0
    for i = 1, n do
        s = s + i
    end
    return s
end
sum(1)  -- 触发JIT编译
print("sum(100) = " .. sum(100))
print("sum(1000) = " .. sum(1000))