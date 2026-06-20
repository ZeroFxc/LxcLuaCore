-- 快速测试 map [expr] key 语法
local t = {x = 10}
local m = [ [t["x"]] = "from_table" ]
print("m[10] =", m[10])
print("OK")