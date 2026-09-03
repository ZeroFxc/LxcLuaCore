-- Test: control flow - if/elseif/else, while, repeat, for, break, continue-like
local function classify(n)
    if n < 0 then
        return "negative"
    elseif n == 0 then
        return "zero"
    elseif n < 10 then
        return "small"
    else
        return "large"
    end
end

print(classify(-5))
print(classify(0))
print(classify(7))
print(classify(100))

-- while loop
local i = 1
local sum = 0
while i <= 100 do
    sum = sum + i
    i = i + 1
end
print("sum 1-100:", sum)

-- repeat-until
local n = 1
local fact = 1
repeat
    fact = fact * n
    n = n + 1
until n > 5
print("5!:", fact)

-- for with break
for i = 1, 20 do
    if i * i > 50 then
        print("first square > 50:", i * i)
        break
    end
end

-- nested loops
local count = 0
for i = 1, 3 do
    for j = 1, 3 do
        count = count + 1
    end
end
print("nested count:", count)

-- generic for with pairs
local t = {a = 1, b = 2, c = 3}
local keys = {}
for k, v in pairs(t) do
    keys[#keys + 1] = k
end
table.sort(keys)
print(table.concat(keys, ","))
