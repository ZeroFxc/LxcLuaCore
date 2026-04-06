-- 检查 T1 后的 Lua 内部状态
local asyncio = require("asyncio")
local w = asyncio.wait

print("=== T1 ===")
local f = asyncio.wrap(function() w(asyncio.sleep(0.01)); return 42 end)
print("f type="..type(f))
local r = f():await_sync(1000)
print("r="..tostring(r))

print("\n=== Lua state check ===")
print("type(asyncio)="..type(asyncio))
print("type(asyncio.wrap)="..type(asyncio.wrap))
print("wrap is function? "..tostring(type(asyncio.wrap) == "function"))

-- 尝试直接调用 wrap
print("\n=== pcall(safe call) wrap ===")
local ok, result = pcall(asyncio.wrap, function(x) return x*2 end)
print("pcall ok="..tostring(ok))
if ok then
    print("result type="..type(result))
else
    print("error="..tostring(result))
end

-- 尝试通过 rawget
print("\n=== rawget wrap ===")
local fn_raw = rawget(asyncio, "wrap")
print("rawget type="..type(fn_raw))
