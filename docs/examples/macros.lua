-- docs/examples/macros.lua
-- This file tests macros and metaprogramming

-- Initialize global tables needed for macros
_G._KEYWORDS = {}
_G._CMDS = {}
_G._OPERATORS = {}

-- Custom Keyword
keyword unless(cond, body)
    if not cond then
        body()
    end
end

unless(5 > 10, function()
    print("5 is not greater than 10")
end)

-- Custom Command
command echo(...)
    local args = {...}
    local str = ""
    for i, v in ipairs(args) do
        str = str .. tostring(v) .. (i < #args and " " or "")
    end
    print(str)
end

echo("Hello", "World")

-- Custom Operator
operator ++(x)
    return x + 1
end

local val = 10
local val2 = $$++(val)
print(val2) -- 11
