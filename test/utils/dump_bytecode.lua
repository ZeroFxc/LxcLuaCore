local asyncio = require("asyncio")
local async function test()
    await(asyncio.sleep(0.01))
    return 99
end
local name, body = debug.getupvalue(test, 1)
local s = string.dump(body)
print("size:", #s)
for i = 1, #s do
    io.write(string.format("%02X ", s:byte(i)))
    if i % 16 == 0 then print() end
end
print()