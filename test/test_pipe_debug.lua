-- 测试同一行上的管道
local obj = {
    multiply = function(self, value)
        return value * 2
    end
}
local result = 21 |> obj:multiply
print("result:", result)