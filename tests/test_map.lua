-- 最小化测试 map
local asyncio = require("asyncio")

print("step1: call map")
local results = asyncio.map(
    function(x) return x * 10 end,
    {1, 2, 3}
)
print("step2: map returned, type="..type(results))

print("step3: await")
local r = results:await_sync(2000)
print("step4: result="..tostring(r))
if type(r) == "table" then
    for i,v in ipairs(r) do print("  ["..i.."]="..tostring(v)) end
end
