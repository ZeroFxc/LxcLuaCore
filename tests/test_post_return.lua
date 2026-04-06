-- 测试 laio_async 返回后的 userdata 状态
local asyncio = require("asyncio")
local w = asyncio.wait

print("T1...")
local f = asyncio.wrap(function() w(asyncio.sleep(0.01)); return 42 end)
f():await_sync(1000)
print("T1 done\n")

print("Creating wrap (D)...")
local ok, d, err = pcall(asyncio.wrap, function(x) return x*2 end)
print("pcall: ok="..tostring(ok))
if ok then
    if d == nil then
        print("d is NIL!")
    else
        local t = type(d)
        print("d type="..t)
        if t == "userdata" then
            local mt = getmetatable(d)
            print("metatable: "..tostring(mt))
            if mt then
                print("mt type="..type(mt))
                -- 尝试读取 __call
                local call_v = rawget(mt, "__call")
                print("__call: "..tostring(call_v).." type="..type(call_v))
            else
                print("NO METATABLE!!!")
            end
        end
    end
else
    print("error: "..tostring(d))
end

-- 不调用 d，只检查它
print("\nDone without calling d")
