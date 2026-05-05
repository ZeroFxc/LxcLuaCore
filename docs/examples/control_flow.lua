-- docs/examples/control_flow.lua
-- This file tests control flow extensions

-- Switch statement
local val = "s"
local type_name
switch (val) do
    case 1:
        type_name = "Integer"
    case "s":
        type_name = "String"
    default:
        type_name = "Unknown"
end
print(type_name) -- String

-- When statement
local x = -5
when x > 0
    print("Positive")
case x < 0
    print("Negative")
else
    print("Zero")
