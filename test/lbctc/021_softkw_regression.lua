-- 软关键字回归测试：验证 class/new/override/init 作为变量名不冲突

print("=== 021 softkw regression START ===")

-- class 作为变量名（在非语句开头位置）
do
  local class = 1
  class = class + 1
  print("class_as_var: " .. class)
end

-- new 作为变量名
do
  local new = function(x) return x * 2 end
  local result = new(5)
  print("new_as_var: " .. result)
end

-- override 作为变量名
do
  local override = true
  print("override_as_var: " .. tostring(override))
end

-- init 作为变量名
do
  local init = 42
  print("init_as_var: " .. init)
end

-- 混合使用：变量名和关键字在不同上下文
local new_val = 10
local class_name = "Test"

-- new 作为变量名后跟表达式
do
  local new_val2 = 99
  local x = new_val2 + 1
  print("new_plus_one: " .. x)
end

print("=== 021 softkw regression END ===")