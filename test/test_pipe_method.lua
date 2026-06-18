-- 测试管道 + 方法调用语法（链式管道）
local producer = || -> 21
local obj = {
    multiply = function(self, value)
        return value * self.factor
    end
}
obj.factor = 2

-- 测试链式管道：producer() |> obj:multiply |> print
-- 预期输出 42
print("=== 链式管道测试 ===")
producer() |> obj:multiply |> print

-- 测试不包含 print 的链式管道，验证返回值
local result = producer() |> obj:multiply
assert(result == 42, "expected 42, got " .. tostring(result))
print("test passed!")