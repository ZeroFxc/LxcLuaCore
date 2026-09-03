-- 更简单的测试：直接调用 |> obj:multiply
local obj = {
    multiply = function(self, value)
        return value * 2
    end
}
local result = 21
|> obj:multiply
print("result:", result)
assert(result == 42, "expected 42, got " .. tostring(result))
print("test passed!")