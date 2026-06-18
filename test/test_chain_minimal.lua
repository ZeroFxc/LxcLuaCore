-- 最简单的链式管道测试
local producer = || -> 21
local obj = {
    multiply = function(self, value)
        return value * self.factor
    end
}
obj.factor = 2
-- 不用 local result =，直接链式调用
producer() |> obj:multiply |> print