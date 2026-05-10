jit.on()
local sum = 0
for i = 1, 10 do
    for j = 1, 10 do
        sum = sum + i + j
    end
end
print(sum)
