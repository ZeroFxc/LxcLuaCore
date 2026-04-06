-- 直接复制 test_complete.lua 的 .then 测试并加调试
local asyncio = require("asyncio")

local function test(name, fn)
    local ok, err = pcall(fn)
    if ok then
        print("  PASS: " .. name)
    else
        print("  FAIL: " .. name .. " => " .. tostring(err))
    end
end

-- 先运行一些前置测试（模拟 test_complete.lua 的环境）
test("warmup run", function()
    local p = asyncio.run(function() return true end)
    p:await_sync(1000)
end)

test("warmup wrap", function()
    local fn = asyncio.wrap(function(x) return x * 2 end)
    local r = fn(21):await_sync(1000)
    assert(r == 42)
end)

-- 现在运行 then 链测试
test(".then chain value passing", function()
    local result = nil
    local p = asyncio.sleep(0.001)
    print("    sleep(0.001) state before done: " .. tostring(p:state()))
    
    p = p:done(function(v)
        print("    fn1 called, v=" .. tostring(v) .. " type=" .. type(v))
        return 10
    end)
    
    p = p:done(function(v)
        print("    fn2 called, v=" .. tostring(v) .. " type=" .. type(v))
        return v * 2
    end)
    
    p = p:done(function(v)
        print("    fn3 called, v=" .. tostring(v) .. " type=" .. type(v))
        result = v
        print("    fn3 set result=" .. tostring(result))
    end)
    
    print("    before await_sync, result=" .. tostring(result))
    asyncio.sleep(0.05):await_sync(200)
    print("    after await_sync, result=" .. tostring(result))
    
    assert(result == 20, "then chain got " .. tostring(result))
end)
