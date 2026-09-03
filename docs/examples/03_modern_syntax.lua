-- 03_modern_syntax.lua
-- 现代语法特性示例 (已通过实际运行验证)
-- 运行: lxclua.exe examples/03_modern_syntax.lua

-- 箭头函数
local square = (x) => x * x
io.write("square(5): ", tostring(square(5)), "\n")

-- Lambda 表达式
local double = |x| -> x * 2
io.write("double(21): ", tostring(double(21)), "\n")

-- 管道运算符
local result = 5 |> double
io.write("pipe |> : ", tostring(result), "\n")

-- 空值合并
local x = nil ?? 42
io.write("nil ?? 42: ", tostring(x), "\n")

-- 可选链
local t = {a = {b = 42}}
io.write("t?.a?.b: ", tostring(t?.a?.b), "\n")

-- 字符串插值
local name = "LXCLUA"
local msg = $"Hello, {name}!"
io.write("interp: ", msg, "\n")

-- 复合赋值
local n = 10
n += 5
io.write("n += 5: ", tostring(n), "\n")
n -= 3
io.write("n -= 3: ", tostring(n), "\n")
n *= 2
io.write("n *= 2: ", tostring(n), "\n")

-- 三路比较 (宇宙飞船)
io.write("3 <=> 5: ", tostring(3 <=> 5), "\n")
io.write("5 <=> 5: ", tostring(5 <=> 5), "\n")
io.write("7 <=> 5: ", tostring(7 <=> 5), "\n")

-- 切片
local arr = {10, 20, 30, 40, 50}
local sliced = arr[2:4]
io.write("arr[2:4]: ", tostring(#sliced), " items\n")
for i = 1, #sliced do
  io.write("  ", tostring(sliced[i]), "\n")
end

-- switch 表达式
local val = 3
switch val do
  case 1 -> io.write("case 1\n")
  case 2 -> io.write("case 2\n")
  case 3 -> io.write("case 3\n")
  default -> io.write("default\n")
end