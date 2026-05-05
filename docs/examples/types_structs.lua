-- docs/examples/types_structs.lua
-- This file tests types, structures, and destructuring

-- Strong typing hints
int i = 10
string s = "test"
print(i, s)

-- Destructuring assignment
local data = { x = 10, y = 20, z = 30 }
local take { x, y } = data
print(x, y) -- 10, 20

local list = { 1, 2, 3 }
local take [ a, , c ] = list
print(a, c) -- 1, 3

-- Const variables
const PI = 3.14159
print(PI)

-- Structs
struct Point {
    x = 0,
    y = 0
}
local p = Point{ x = 10, y = 20 }
print(p.x) -- 10

-- Defer
local function test_defer()
    defer print("File closed or cleanup done")
    print("Doing work...")
end
test_defer()
