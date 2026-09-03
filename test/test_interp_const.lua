print("=== 测试: 没有变量的表达式 ===")
local t3 = $"Test: {5 + 10}"
io.write("t3 = '", t3, "'\n")
io.flush()
assert(t3 == "Test: 15", "expected 'Test: 15', got '" .. tostring(t3) .. "'")
print("PASS")