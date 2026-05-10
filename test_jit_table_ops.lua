jit.on()
local t = { x = 10, y = 20 }
t.x = 100
t[1] = 50
print(t.x)
print(t[1])
local z = t.y
print(z)
