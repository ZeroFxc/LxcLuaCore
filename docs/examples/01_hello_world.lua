-- 01_hello_world.lua
-- 基础 Hello World 示例
-- 运行: lxclua.exe examples/01_hello_world.lua

io.write("Hello, LXCLUA-NCore!\n")
io.write("Version: 505.8\n")

-- 基础操作
local a, b = 10, 20
io.write("a + b = ", tostring(a + b), "\n")
io.write("type of a: ", type(a), "\n")
io.write("type of \"hello\": ", type("hello"), "\n")
io.write("type of true: ", type(true), "\n")
io.write("type of nil: ", type(nil), "\n")
io.write("type of {}: ", type({}), "\n")