-- 04_control_flow.lua
-- 控制流示例 (已通过实际运行验证)
-- 运行: lxclua.exe examples/04_control_flow.lua

-- switch 语句
local value = 42
switch value do
  case 1 -> io.write("small\n")
  case 2 -> io.write("small\n")
  case 42 -> io.write("the answer\n")
  default -> io.write("other\n")
end

-- guard 守卫
local function safeDivide(a, b)
  guard b ~= 0 else {
    io.write("Division by zero!\n")
    return nil
  }
  io.write("result: ", tostring(a / b), "\n")
  return a / b
end
safeDivide(10, 2)
safeDivide(10, 0)

-- guard let
local function processUser(user)
  guard let name = user.name else {
    return "anonymous"
  }
  return $"user: {name}"
end
io.write(processUser({name = "Alice"}), "\n")
io.write(processUser({}), "\n")

-- for...else
local arr = {1, 2, 3, 4, 5}
for i = 1, #arr do
  if arr[i] == 99 then break end
else
  io.write("99 not found in array\n")
end

-- 枚举
enum Color {
  Red = 1,
  Green = 2,
  Blue = 4
}
io.write("Color.Red: ", tostring(Color.Red), "\n")
io.write("Color.Green: ", tostring(Color.Green), "\n")

-- 命名空间
namespace Math {
  PI = 3.14159
  function add(a, b) return a + b end
}
io.write("Math.add(3, 4): ", tostring(Math.add(3, 4)), "\n")
io.write("Math.PI: ", tostring(Math.PI), "\n")