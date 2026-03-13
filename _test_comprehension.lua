local src = {1, 2, 3, 4, 5}
local evens = [for _, v in ipairs(src) do v * 2 if v % 2 == 0]
print(dump(evens))
