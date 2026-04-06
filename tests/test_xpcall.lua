-- 更安全的状态检查
local asyncio = require("asyncio")
local w = asyncio.wait

print("=== T1 ===")
local f = asyncio.wrap(function() w(asyncio.sleep(0.01)); return 42 end)
print("f type="..type(f))
local r = f():await_sync(1000)
print("r="..tostring(r))

print("\n=== Safe wrap call ===")
-- 用 xpcall 加错误处理器
local ok, err = xpcall(function()
    local d = asyncio.wrap(function(x) return x*2 end)
    -- 立刻存到全局防止被GC
    _saved_wrap = d
    print("wrap returned! type="..type(d))
    if type(d) == "userdata" then
        local mt = getmetatable(d)
        print("metatable exists? "..tostring(mt ~= nil))
        if mt then
            for k,v in pairs(mt) do
                print("  mt["..tostring(k).."] = "..type(v))
            end
        end
    end
end, function(msg)
    return "XPCALL ERROR: "..tostring(msg)
end)

print("\nxpcall: ok="..tostring(ok))
if not ok then print("err="..tostring(err)) end

if _saved_wrap then
    print("_saved_wrap still alive: type="..type(_saved_wrap))
end
