-- 关键测试：yield vs no-yield
local asyncio = require("asyncio")
local w = asyncio.wait

-- Test A: wrap WITHOUT yield (no sleep)
print("=== TEST A: no-yield wrap ===")
local fa = asyncio.wrap(function(x) return x + 1 end)
print("fa created")
local pa = fa(10)
print("pa type="..type(pa))
local ra = pa:await_sync(1000)
print("ra="..tostring(ra).." (expect 11)")

-- Test B: wrap WITH yield (has sleep)  
print("\n=== TEST B: yield wrap ===")
local fb = asyncio.wrap(function() w(asyncio.sleep(0.01)); return 42 end)
print("fb created")
local pb = fb()
print("pb type="..type(pb))
local rb = pb:await_sync(1000)
print("rb="..tostring(rb).." (expect 42)")

-- Test C: wrap AFTER yield wrap (the crash point)
print("\n=== TEST C: wrap after yield ===")
local fc = asyncio.wrap(function(x) return x * 2 end)
print("fc created!!!")
local rc = fc(7):await_sync(1000)
print("rc="..tostring(rc).." (expect 14)")

print("\nALL PASSED!")
