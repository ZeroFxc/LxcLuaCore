-- 极端测试：T1 后只做纯 Lua 操作
local asyncio = require("asyncio")
local w = asyncio.wait

print("T1: wrap+sleep...")
local f = asyncio.wrap(function() w(asyncio.sleep(0.01)); return 42 end)
print("f type="..type(f))
local r = f():await_sync(1000)
print("r="..tostring(r))

print("\nPure Lua operations:")
local t = {1,2,3}
print("table ok: "..#t)
local s = "hello"..tostring(r)
print("string ok: "..s)
local fn = function(x) return x*2 end
print("function ok: "..tostring(fn(21)))

print("\nNow trying rawget...")
local raw_fn = rawget(asyncio, "wrap")
print("rawget result type="..type(raw_fn))
