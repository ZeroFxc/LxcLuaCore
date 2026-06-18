-- do 表达式测试

-- 1. 基本 do 表达式
print("=== 1. 基本do表达式 ===")
local x = do 42 end
print(x)  -- 期望: 42

-- 2. 多语句 do 表达式，最后一个是返回值
print("=== 2. 多语句 ===")
local y = do
  local a = 10
  local b = 20
  a + b
end
print(y)  -- 期望: 30

-- 3. do 表达式作为函数参数
print("=== 3. 函数参数 ===")
print(do 100 end)  -- 期望: 100

-- 4. 嵌套 do
print("=== 4. 嵌套 ===")
local z = do
  do 50 end
end
print(z)  -- 期望: 50

-- 5. 空 do 表达式返回 nil
print("=== 5. 空do ===")
local w = do end
print(w)  -- 期望: nil