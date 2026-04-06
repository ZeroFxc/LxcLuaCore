-- 调试 wrap 返回值
local asyncio = require("asyncio")

local double = asyncio.wrap(function(x) return x * 2 end)
print("double type="..type(double))
print("double metatype="..type(getmetatable(double)))

local result = double(7)
print("double(7) type="..type(result))
print("double(7) value="..tostring(result))

if type(result) == "userdata" then
    print("is promise? "..tostring(getmetatable(result) ~= nil))
    local r = result:await_sync(1000)
    print("await_sync result="..tostring(r).." type="..type(r))
elseif type(result) == "number" then
    print("Got number directly: "..result)
end
