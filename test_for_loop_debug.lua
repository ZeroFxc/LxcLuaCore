-- 测试 for循环 在 flags=527 下的行为
local flags = 527

local code = "local sum = 0; for i = 1, 10 do sum = sum + i; end; return sum"
local fn = load(code)

local obf_dump = string.dump(fn, {
    obfuscate = flags,
    seed = 12345,
    strip = false
})
print("混淆字节码长度: " .. #obf_dump)

-- 保存到文件
local f = io.open("test_for_loop.luac", "wb")
f:write(obf_dump)
f:close()
print("已保存到 test_for_loop.luac")

-- 用 luac -l 查看
print("\n执行混淆后的代码:")
local obf_fn = load(obf_dump)
local ok, res = pcall(obf_fn)
if ok then
    print("结果: " .. tostring(res))
else
    print("执行错误: " .. tostring(res))
end
