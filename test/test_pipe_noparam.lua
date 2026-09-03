-- 测试内联 || 无参lambda 在管道中
local result = 10 |> || -> 42
print("result:", result)