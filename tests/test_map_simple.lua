-- 最小化 map 测试
local asyncio = require("asyncio")

print("=== map test ===")

local results = asyncio.map(
    function(x) return x * 10 end,
    {1, 2, 3}
):await_sync(2000)

print("type="..type(results).." #="..#results)
if type(results) == "table" then
    for i=1,#results do
        print("  ["..i.."]="..tostring(results[i]).." type="..type(results[i]))
    end
else
    print("  value="..tostring(results))
end
