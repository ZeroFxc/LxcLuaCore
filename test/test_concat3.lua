-- 逐步测试
local s = "x"
local r1 = s .. s
print("r1:", r1)          -- expect xx
local r2 = s .. s .. s     
print("r2:", r2)          -- expect xxx
local r3 = s .. s .. s .. s
print("r3:", r3)          -- expect xxxx