-- 测试内联lambda直接调用
local result = (|x| -> x * 2)(10)
print("result:", result)