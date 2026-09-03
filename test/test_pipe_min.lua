local obj = { multiply = function(v) return v * 2 end }
local x = 21
local result = x |> obj.multiply
print("result:", result)
assert(result == 42, "expected 42, got " .. tostring(result))
print("test passed!")