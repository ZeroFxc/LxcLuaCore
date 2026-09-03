local a = 10
local b = 20
local msg = $"Sum: {a + b}"
print("msg =", msg)
print("type =", type(msg))
assert(msg == "Sum: 30", "expected 'Sum: 30', got '" .. tostring(msg) .. "'")
print("PASS")