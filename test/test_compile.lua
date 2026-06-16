-- 简单测试编译
local async function test_sleep()
    await(asyncio.sleep(0.01))
    return 99
end