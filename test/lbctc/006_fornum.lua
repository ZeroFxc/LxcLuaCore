-- covers: OP_FORPREP,OP_FORLOOP,OP_LOADI,OP_ADDI,OP_SETTABLE
print("=== 006 fornum START ===")
local s = 0
for i = 1, 10, 2 do s = s + i end
print("step2_sum: " .. tostring(s))
local s2 = 0
for i = 10, 1, -1 do s2 = s2 * 10 + i end
print("revnum: " .. tostring(s2))
local c = 0
for i = 5, 5 do c = c + 1 end
print("single_iter: " .. tostring(c))
local c2 = 0
for i = 10, 5 do c2 = c2 + 1 end
print("empty_for: " .. tostring(c2))
print("=== 006 fornum END ===")
