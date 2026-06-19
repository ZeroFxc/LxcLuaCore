-- 测试 JIT 基本功能
print("Step 1: enable JIT")
jit.on()
print("JIT status:", jit.status())

print("Step 2: define add function")
function add(a, b)
    return a + b
end

print("Step 3: stats before call")
print("JIT stats:", jit.stats())

print("Step 4: calling add(1, 2)...")
local r = add(1, 2)
print("Step 5: add(1, 2) =", r)

print("JIT stats:", jit.stats())
print("Done!")