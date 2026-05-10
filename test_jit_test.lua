jit.on()
local x = false
if x then
  print("x is true")
else
  print("x is false")
end
local y = 10
if y then
  print("y is true")
end
local z = x or y
print(z)
