local g = 16; g >>= 2; print("16 >>= 2 =", g, "(exp 4)")
local h = 4; h <<= 3; print("4 <<= 3 =", h, "(exp 32)")

-- also test naked shift with variable+literal
local x = 16
print("16 >> 2 =", x >> 2)
local y = 4
print("4 << 2 =", y << 2)

-- verify shift with two variables
local a = 16; local s = 2
print("16 >> s =", a >> s)
local b = 4
print("4 << s =", b << s)