-- 极简复现：两次 wrap
local asyncio = require("asyncio")
local w = asyncio.wait

print("wrap #1 (with sleep)...")
local f1 = asyncio.wrap(function() w(asyncio.sleep(0.01)); return "A" end)
print("f1 type="..type(f1))
local r1 = f1():await_sync(1000)
print("#1 result="..tostring(r1))

print("\nwrap #2 (with sleep again)...")
local f2 = asyncio.wrap(function() w(asyncio.sleep(0.01)); return "B" end)
print("f2 type="..type(f2))
local r2 = f2():await_sync(1000)
print("#2 result="..tostring(r2))
