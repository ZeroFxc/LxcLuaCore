-- 测试：T1 后立即释放 wrap 引用
local asyncio = require("asyncio")
local w = asyncio.wait

print("T1 (wrap in local scope)...")
do
    local f = asyncio.wrap(function() w(asyncio.sleep(0.01)); return 42 end)
    local r = f():await_sync(1000)
    print("r="..tostring(r))
end
-- f 现在超出作用域，应该可被 GC
print("T1 done, f out of scope\n")

-- 强制 GC
collectgarbage()
print("after gc\n")

-- 现在创建新的 wrap
print("T2: creating wrap...")
local d = asyncio.wrap(function(x) return x*2 end)
print("wrap type="..type(d))

local r2 = d(7):await_sync(1000)
print("r2="..tostring(r2))
