jit.on()
local x = 10
local function f()
  x = 20
  return x
end
print(f())
