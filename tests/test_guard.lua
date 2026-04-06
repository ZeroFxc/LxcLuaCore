-- 守卫测试：追踪 result 每次变化
local asyncio = require("asyncio")

local result = nil
local set_count = 0
local history = {}

local function record(src, val)
    set_count = set_count + 1
    history[set_count] = {src=src, val=val, debug=debug.getinfo(2)}
    result = val
end

print("=== Chain ===")
asyncio.sleep(0.001)
    :done(function(v)
        record("fn1", 10)
        return 10
    end)
    :done(function(v)
        record("fn2", v * 2)
        return v * 2
    end)
    :done(function(v)
        record("fn3", v)
        -- 不显式 return
    end)

print("Before await_sync: result="..tostring(result).." sets="..set_count)

for i=1,set_count do
    local h = history[i]
    print("  #"..i.." "..h.src.." => "..tostring(h.val)..
          " at line="..(h.debug and h.debug.currentline or "?"))
end

asyncio.sleep(0.05):await_sync(200)

print("\nAfter await_sync:")
print("  result="..tostring(result).." sets="..set_count)

for i=1,set_count do
    local h = history[i]
    print("  #"..i.." "..h.src.." => "..tostring(h.val)..
          " at line="..(h.debug and h.debug.currentline or "?"))
end

print("\nFinal: expected 20, got " .. tostring(result))
