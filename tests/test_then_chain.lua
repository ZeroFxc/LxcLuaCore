-- 最小化 .then 链测试
local asyncio = require("asyncio")

print("=== then chain test ===")

local result = nil
asyncio.sleep(0.001)
    :done(function(v)
        print("fn1: v="..tostring(v).." type="..type(v))
        return 10
    end)
    :done(function(v)
        print("fn2: v="..tostring(v).." type="..type(v))
        return v * 2
    end)
    :done(function(v)
        print("fn3: v="..tostring(v).." type="..type(v))
        result = v
    end)

-- 用 await_sync 让 event loop 跑起来处理 timer
asyncio.sleep(0.05):await_sync(200)

print("result="..tostring(result).." (expect 20)")
assert(result == 20, "got "..tostring(result))

print("PASS!")
