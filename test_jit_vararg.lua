jit.on()
local function sum(...)
  local s = 0
  local args = {...}
  for i=1, #args do
    s = s + args[i]
  end
  return s
end
print(sum(1, 2, 3, 4, 5))
