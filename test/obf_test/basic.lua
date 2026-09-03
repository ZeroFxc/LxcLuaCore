-- basic test: function call, condition, loop, string, table
local function factorial(n)
    if n <= 1 then return 1 end
    return n * factorial(n - 1)
end

local function greet(name)
    return "Hello, " .. name .. "!"
end

local t = {}
for i = 1, 5 do
    t[i] = factorial(i)
end

print(greet("World"))
print(table.concat(t, ", "))

-- closure
local function counter()
    local c = 0
    return function()
        c = c + 1
        return c
    end
end

local next = counter()
print(next(), next(), next())

-- string operations
local s = "LXCLUA"
print(string.upper(s), string.len(s), s:sub(2, 4))
