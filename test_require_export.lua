-- 测试顶层 export 能否被 require 加载
local mod = require("test_export_toplevel")
print("version:", mod.version)
print("add(1,2):", mod.add(1, 2))