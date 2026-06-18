-- 测试管道 + 方法调用（单管道）
local obj = {
    multiply = function(self, value)
        return value * self.factor
    end
}
obj.factor = 2
local result = 21 |> obj:multiply
print("result:", result)
assert(result == 42, "expected 42, got " .. tostring(result))
print("test passed!")