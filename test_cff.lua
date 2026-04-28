local function test_add(a, b)
    local result = a + b
    if result > 10 then
        return result * 2
    else
        return result
    end
end

local function test_loop(n)
    local sum = 0
    for i = 1, n do
        sum = sum + i
    end
    return sum
end

print(test_add(5, 8))
print(test_loop(10))
