-- then chain 追踪：result何时被覆盖
local asyncio = require("asyncio")

print("=== then chain trace ===")

local result = nil
local call_count = 0

local p = asyncio.sleep(0.001)
p = p:done(function(v)
    call_count = call_count + 1
    print("fn1 [call#"..call_count.."]: v="..tostring(v).." type="..type(v))
    return 10
end)
p = p:done(function(v)
    call_count = call_count + 1
    print("fn2 [call#"..call_count.."]: v="..tostring(v).." type="..type(v))
    return v * 2
end)
p = p:done(function(v)
    call_count = call_count + 1
    print("fn3 [call#"..call_count.."]: v="..tostring(v).." type="..type(v).." (BEFORE SET) result="..tostring(result))
    result = v
    print("fn3 [call#"..call_count.."]: AFTER SET result="..tostring(result))
end)

print("Before await_sync: result="..tostring(result).." calls="..call_count)

local r = asyncio.sleep(0.05):await_sync(200)
print("After await_sync: r="..tostring(r).." result="..tostring(result).." calls="..call_count)

-- 再等一下看看有没有延迟触发
local r2 = asyncio.sleep(0.05):await_sync(200)
print("After await_sync2: r2="..tostring(r2).." result="..tostring(result).." calls="..call_count)
