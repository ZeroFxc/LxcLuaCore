jit.on()
local t = { x = 10 }
t.x = 20
local z = t.x
print(z)

local t2 = { 10 }
t2[1] = 30
local z2 = t2[1]
print(z2)
