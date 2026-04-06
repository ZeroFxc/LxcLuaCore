-- 精确对照：直接赋值 vs 间接赋值
local asyncio = require("asyncio")

-- ===== 版本 A：直接赋值 =====
print("=== Version A: direct assignment ===")
do
    local resultA = nil
    asyncio.sleep(0.001)
        :done(function(v)
            print("  A-fn1: v="..tostring(v))
            return 10
        end)
        :done(function(v)
            print("  A-fn2: v="..tostring(v))
            return v * 2
        end)
        :done(function(v)
            print("  A-fn3: v="..tostring(v))
            resultA = v
            print("  A-fn3 set: resultA="..tostring(resultA))
        end)
    asyncio.sleep(0.05):await_sync(200)
    print("  A final: resultA="..tostring(resultA).." (expect 20)")
end

-- ===== 版本 B：通过 table 间接赋值 =====
print("\n=== Version B: table indirect ===")
do
    local ctx = {result = nil}
    asyncio.sleep(0.001)
        :done(function(v)
            print("  B-fn1: v="..tostring(v))
            return 10
        end)
        :done(function(v)
            print("  B-fn2: v="..tostring(v))
            return v * 2
        end)
        :done(function(v)
            print("  B-fn3: v="..tostring(v))
            ctx.result = v
            print("  B-fn3 set: ctx.result="..tostring(ctx.result))
        end)
    asyncio.sleep(0.05):await_sync(200)
    print("  B final: ctx.result="..tostring(ctx.result).." (expect 20)")
end

-- ===== 版本 C：通过 upvalue function 间接 =====
print("\n=== Version C: setter function ===")
do
    local resultC = nil
    local function setR(val)
        resultC = val
    end
    asyncio.sleep(0.001)
        :done(function(v)
            print("  C-fn1: v="..tostring(v))
            return 10
        end)
        :done(function(v)
            print("  C-fn2: v="..tostring(v))
            return v * 2
        end)
        :done(function(v)
            print("  C-fn3: v="..tostring(v))
            setR(v)
            print("  C-fn3 set: resultC="..tostring(resultC))
        end)
    asyncio.sleep(0.05):await_sync(200)
    print("  C final: resultC="..tostring(resultC).." (expect 20)")
end
