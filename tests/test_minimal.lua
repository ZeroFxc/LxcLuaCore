-- 极简定位：sleep 是否破坏了 Lua VM 状态
local asyncio = require("asyncio")
local w = asyncio.wait

print("1: before anything")

print("2: creating sleep promise...")
local sp = asyncio.sleep(0.01)
print("3: sleep promise type="..type(sp))

print("4: awaiting sync...")
local r = sp:await_sync(1000)
print("5: after await_sync, r="..tostring(r))

print("6: pure lua table test...")
local t = {a=1,b=2,c=3}
print("7: table ok, count="..#t)

print("8: function call test...")
local function add(a,b) return a+b end
print("9: add(2,3)="..add(2,3))

print("10: string concat test...")
local s = "hello" .. " " .. "world"
print("11: s="..s)

print("12: coroutine test...")
local co = coroutine.create(function()
    return 42
end)
local ok, val = coroutine.resume(co)
print("13: coroutine ok="..tostring(ok).." val="..tostring(val))

print("14: userdata test...")
local ud = newproxy(true)
print("15: ud type="..type(ud))

print("\nALL DONE!")
