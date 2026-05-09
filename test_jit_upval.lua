jit.on()

local a = 10
function test_upval()
  local b = a + 5
  a = b
  return a
end

print(test_upval())
print(test_upval())

local t = {x = 100}
function test_tabup()
  t.x = t.x + 50
  return t.x
end

print(test_tabup())
print(test_tabup())
