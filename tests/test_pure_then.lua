-- 最小化：无任何前置操作的 .then 链
local asyncio = require("asyncio")

print("=== PURE then chain, zero prefix ===")
local result = nil

print("Creating sleep+chain...")
asyncio.sleep(0.001)
    :done(function(v)
        print("  fn1: v="..tostring(v))
        return 10
    end)
    :done(function(v)
        print("  fn2: v="..tostring(v))
        return v * 2
    end)
    :done(function(v)
        print("  fn3: v="..tostring(v))
        result = v
    end)

print("Before await_sync: result="..tostring(result))

print("Awaiting...")
local r = asyncio.sleep(0.05):await_sync(200)

print("After await_sync:")
print("  r="..tostring(r))
print("  result="..tostring(result))

if result == 20 then
    print("\n✅ PASS!")
else
    print("\n❌ FAIL: expected 20, got " .. tostring(result))
end
