-- 导出字节码并检查
local asyncio = require("asyncio")

-- 方法A: await 关键字语法
local async function test_a()
    await(asyncio.sleep(0.01))
    return 99
end

-- 方法B: 手动 coroutine.yield  
local function test_b()
    coroutine.yield(asyncio.sleep(0.01))
    return 99
end

print("=== test_a (async function) 的字节码 ===")
local dump_a = string.dump(test_a, true)
print("字节码长度:", #dump_a)

print("\n=== test_b (普通函数) 的字节码 ===")
local dump_b = string.dump(test_b, true)
print("字节码长度:", #dump_b)

-- 尝试查看字节码内容
local function hexdump(data, maxlen)
    local maxlen = maxlen or 200
    local result = {}
    for i = 1, math.min(#data, maxlen) do
        local b = string.byte(data, i)
        table.insert(result, string.format("%02X", b))
        if i % 16 == 0 then table.insert(result, "\n") end
    end
    return table.concat(result, " ")
end

print("test_a 前200字节:")
print(hexdump(dump_a, 200))
print("\ntest_b 前200字节:")
print(hexdump(dump_b))