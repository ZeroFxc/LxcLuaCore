-- 测试：变量 |> obj:method
local obj = {
    multiply = function(value)
        return value * 2
    end
}
local x = 21
local result = x |> obj:multiply
print("result:", result)
assert(result == 42, "expected 42, got " .. tostring(result))
print("test passed!")