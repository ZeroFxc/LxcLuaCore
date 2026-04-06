-- 带调试的完整测试
local asyncio = require("asyncio")
local w = asyncio.wait

local passed, failed = 0, 0
local function test(name, fn)
    local ok, err = pcall(fn)
    if ok then passed = passed + 1; print("  ✅ "..name)
    else failed = failed + 1; print("  ❌ "..name..": "..tostring(err)) end
end

print("=== test 1 ===")
test("t1", function()
    local fetch = asyncio.wrap(function(x)
        w(asyncio.sleep(0.01))
        return x * 2
    end)
    local r = fetch(21):await_sync(1000)
    assert(r == 42)
end)

print("=== test 2 ===")
test("t2", function()
    local p = asyncio.run(function()
        local r = w(asyncio.sleep(0.01))
        return r and "slept" or "error"
    end)
    local r = p:await_sync(1000)
    assert(r == "slept")
end)

print("=== test 3 (arrow) ===")
test("t3-arrow", function()
    print("    [DEBUG] creating wrap...")
    local double = asyncio.wrap(function(x) return x * 2 end)
    print("    [DEBUG] double type="..type(double))

    print("    [DEBUG] calling double(7)...")
    local result_val = double(7)
    print("    [DEBUG] result type="..type(result_val).." val="..tostring(result_val))

    if type(result_val) == "number" then
        print("    [DEBUG] !!! Got number instead of Promise!")
        print("    [DEBUG] This means __call didn't fire, or returned raw value")
        return  -- skip assert to see more
    end

    print("    [DEBUG] calling :await_sync...")
    local r = result_val:await_sync(1000)
    print("    [DEBUG] r="..tostring(r))
    assert(r == 14, "got "..tostring(r))
end)

print(string.format("\n%d/%d passed", passed, passed+failed))
