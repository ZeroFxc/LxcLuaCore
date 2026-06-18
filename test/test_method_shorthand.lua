-- 测试 function NAME() 简写语法（隐式 self）
local producer = || -> 21
local obj = { 
    function multiply(value) 
        return value * self.factor 
    end 
} 
obj.factor = 2 
producer() |> obj:multiply |> print  --> 42