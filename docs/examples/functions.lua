-- docs/examples/functions.lua
-- This file tests function extensions

-- Arrow functions (statement form)
local log = ->(msg) { print(msg) }
log("Hello from arrow function!")

-- Arrow functions (expression form)
local f = =>(x) { x * x }
print(f(4)) -- 16

-- Generic functions
-- Note: Generic parsing exists but currently ignores type parameters at runtime.
local function identity(x)
    return x
end
print(identity(100)) -- 100

-- Pipeline Operator
local function add1(x) return x + 1 end
local function mul2(x) return x * 2 end

-- Using pipeline
local v = 5
local res = v |> add1 |> mul2
print(res) -- 12

local s2 = "hello"
local res3 = s2 |> string.upper |> string.reverse
print(res3) -- OLLEH
