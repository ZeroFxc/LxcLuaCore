-- 扩展函数测试

-- 1. 扩展 string 类型
function string:double()
  return self .. self
end
print("=== 1. string:double ===")
print(("hello"):double())  -- 期望: hellohello

-- 2. 扩展 table 类型
function table:keys()
  local result = {}
  for k, _ in pairs(self) do
    result[#result + 1] = k
  end
  return result
end
print("=== 2. table:keys ===")
local t = {a = 1, b = 2, c = 3}
local k = t:keys()
print(#k)  -- 期望: 3

-- 3. 扩展函数带参数
function string:repeat_n(n)
  local result = ""
  for i = 1, n do
    result = result .. self
  end
  return result
end
print("=== 3. string:repeat_n ===")
print(("a"):repeat_n(3))  -- 期望: aaa

-- 4. 扩展函数，使用局部变量接收
function string:wrap(tag)
  local s = self
  local left = "<" .. tag .. ">"
  local right = "</" .. tag .. ">"
  return left .. s .. right
end
print("=== 4. string:wrap ===")
print(("hello"):wrap("b"))  -- 期望: <b>hello</b>