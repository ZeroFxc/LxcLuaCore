-- 快速测试 map [expr] key 语法 - 函数内
local function test()
  local t = {x = 10}
  local m = [ [t["x"]] = "from_table" ]
  return m[10]
end
print(test())
print("OK")