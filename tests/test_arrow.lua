-- 单独测试箭头函数
local asyncio = require("asyncio")

local double = asyncio.wrap(function(x) return x * 2 end)
print("type="..type(double))
local r = double(7):await_sync(1000)
print("r="..tostring(r).." type="..type(r))
assert(r == 14, "got "..tostring(r))
print("PASS")
