-- 测试1: 全局函数
local result = 10 |> print  -- 应该OK

-- 测试2: 局部函数  
local f = function(x) return x * 2 end
local result2 = 10 |> f  -- 崩溃

-- 测试3: 表方法
local obj = { m = function(x) return x end }
local result3 = 10 |> obj.m  -- ?

-- 测试4: 匿名函数
local result4 = 10 |> function(x) return x end  -- ?