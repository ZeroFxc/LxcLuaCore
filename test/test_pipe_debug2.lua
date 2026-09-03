-- 测试管道 + 普通函数
local f = function(x) return x * 2 end
local result = 21 |> f
print("result:", result)