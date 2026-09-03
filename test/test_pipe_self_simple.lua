-- 简单测试：单层管道 + 方法调用
local producer = || -> 21
local obj = {
    multiply = function(self, value)
        return value * self.factor
    end
}
obj.factor = 2
local result = producer() |> obj:multiply
print("result:", result)
assert(result == 42, "expected 42, got " .. tostring(result))
print("test passed!")