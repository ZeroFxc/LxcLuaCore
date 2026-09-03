print("=== 测试: $\"...\" 函数调用插值 ===")
local function t(x) return x * 2 end
local t3 = $"value: {t(5)}"
io.write("t3 = '", t3, "'\n")
io.flush()
assert(t3 == "value: 10", "expected 'value: 10', got '" .. tostring(t3) .. "'")
print("  通过: $\"value: {t(5)}\" =", t3)
print("ALL PASS")