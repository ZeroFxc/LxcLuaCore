-- 用 pcall 包裹定位精确错误
local asyncio = require("asyncio")
local w = asyncio.wait

print("T1: wrap+call+await...")
local fn = asyncio.wrap(function() w(asyncio.sleep(0.01)); return 99 end)
local p = fn()
local r = p:await_sync(1000)
print("T1 OK: r="..tostring(r))

print("\nT2: pcall(wrap)...")
local ok, err = pcall(function()
    local fn2 = asyncio.wrap(function(x) return x*2 end)
    print("fn2 type="..type(fn2))
    return fn2
end)
print("pcall: ok="..tostring(ok).." err="..tostring(err))

if ok and err then
    print("\nT3: calling fn2...")
    local p2 = err(7)
    local r2 = p2:await_sync(1000)
    print("r2="..tostring(r2))
end

print("\nALL DONE!")
