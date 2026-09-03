-- 测试管道 + lambda
local result = 21 |> |x| -> x * 2
print("result:", result)
assert(result == 42, "expected 42, got " .. tostring(result))
print("test passed!")