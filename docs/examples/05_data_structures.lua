-- 05_data_structures.lua
-- 数据结构示例 (已通过实际运行验证)
-- 运行: lxclua.exe examples/05_data_structures.lua

-- 表 (Table)
local t = {a = 1, b = 2, c = 3}
io.write("table a: ", tostring(t.a), "\n")
io.write("table #: ", tostring(#t), "\n")

-- 数组表
local arr = {10, 20, 30, 40, 50}
io.write("arr[1]: ", tostring(arr[1]), "\n")
io.write("arr length: ", tostring(#arr), "\n")

-- 切片
local sliced = arr[2:4]
for i = 1, #sliced do
  io.write("  sliced[", i, "]: ", tostring(sliced[i]), "\n")
end

-- 表合并
local a = {1, 2, 3}
local b = {4, 5, 6}
local c = a <> b
io.write("merged: ", tostring(#c), " items\n")