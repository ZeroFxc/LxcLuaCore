-- 测试：T1 后调用各种 C 函数
local asyncio = require("asyncio")
local w = asyncio.wait

print("T1: wrap+sleep...")
local f = asyncio.wrap(function() w(asyncio.sleep(0.01)); return 42 end)
local r = f():await_sync(1000)
print("r="..tostring(r))

print("\n--- Test A: asyncio.resolve() ---")
local ok, err = pcall(function()
    local p = asyncio.resolve("hello")
    print("resolve ok! type="..type(p))
end)
print("A: ok="..tostring(ok).." err="..tostring(err))

print("\n--- Test B: asyncio.sleep(0) ---")
ok, err = pcall(function()
    local p = asyncio.sleep(0)
    print("sleep ok! type="..type(p))
end)
print("B: ok="..tostring(ok).." err="..tostring(err))

print("\n--- Test C: asyncio.run(simple) ---")
ok, err = pcall(function()
    local p = asyncio.run(function() return 99 end)
    print("run ok! type="..type(p))
end)
print("C: ok="..tostring(ok).." err="..tostring(err))

print("\n--- Test D: asyncio.wrap (THE CRASHING ONE) ---")
ok, err = pcall(function()
    local d = asyncio.wrap(function(x) return x*2 end)
    print("wrap!! type="..type(d))
end)
print("D: ok="..tostring(ok).." err="..tostring(err))
