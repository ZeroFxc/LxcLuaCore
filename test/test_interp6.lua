print("=== 测试6: 表达式插值 ===")
local a = 10
local b = 20
local msg2 = $"Sum: {a + b}"
io.write("msg2 = '", msg2, "'\n")
io.flush()
assert(msg2 == "Sum: 30", "expected 'Sum: 30', got '" .. tostring(msg2) .. "'")
print("  通过: $\"Sum: {a + b}\" =", msg2)
print("ALL PASS")