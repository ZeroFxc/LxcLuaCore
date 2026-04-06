-- 精确定位 T3 崩溃原因
local asyncio = require("asyncio")
local w = asyncio.wait

-- T1: 带 sleep 的 wrap
print("=== T1: wrap with sleep ===")
local f = asyncio.wrap(function(x) w(asyncio.sleep(0.01)); return x*2 end)
print("f type="..type(f).." metatype="..type(getmetatable(f)))
local r1 = f(21):await_sync(1000)
print("T1 result="..tostring(r1))

-- 检查 asyncio 模块状态
print("\n=== State check after T1 ===")
print("asyncio.wrap type="..type(asyncio.wrap))
print("asyncio type="..type(asyncio))

-- T3: 不带 sleep 的 wrap
print("\n=== T3: wrap without sleep ===")
local d = asyncio.wrap(function(x) return x*2 end)
print("d type="..type(d).." metatype="..type(getmetatable(d)))

print("calling d(7)...")
local raw_result = d(7)
print("d(7) type="..type(raw_result).." value="..tostring(raw_result))

if type(raw_result) == "userdata" then
    local r = raw_result:await_sync(1000)
    print("T3 result="..tostring(r))
elseif type(raw_result) == "number" then
    print("ERROR: Got number instead of Promise!")
    -- 尝试手动检查元方法
    local mt = getmetatable(d)
    if mt then
        print("d metatable exists:")
        for k,v in pairs(mt) do print("  "..tostring(k).." = "..type(v)) end
    else
        print("d has NO metatable!")
    end
end
