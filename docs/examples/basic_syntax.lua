-- docs/examples/basic_syntax.lua
-- This file tests basic syntax extensions

-- Compound assignments
local x = 10
x += 5      -- x = 15
x -= 2      -- x = 13
x *= 3      -- x = 39
x /= 2      -- x = 19.5
x //= 2     -- x = 9.0
x %= 3      -- x = 0.0

-- String Interpolation
local name = "World"
local age = 25
print("Hello, ${name}!")
print("Next year: ${[age + 1]}")
print("Price: $$100")

local path = _raw"C:\\Windows\\System32"
print(path)

-- If-expression
local a = 10
local b = 20
local max = if a > b then a else b end
print(max) -- 20
