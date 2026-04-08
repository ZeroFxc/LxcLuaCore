#!/usr/bin/env lua
-- 简化的调试测试脚本

local test_code = [[
print("Test executed successfully")
return 123
]]

local test_file = "debug_test.lua"
local f = io.open(test_file, "w")
f:write(test_code)
f:close()

os.execute("luac.exe -o debug_test.luac debug_test.lua")

-- 读取并显示文件信息
local fin = io.open("debug_test.luac", "rb")
local data = fin:read("*a")
fin:close()

print(string.format("文件大小: %d 字节", #data))

-- 尝试加载
print("\n尝试正常加载...")
local ok, func = pcall(loadfile, "debug_test.luac")
if ok then
    print("✓ 加载成功")
    local result = func()
    print(string.format("✓ 执行结果: %s", tostring(result)))
else
    print(string.format("✗ 加载失败: %s", tostring(func)))
end

-- 篡改最后32字节（假设是HMAC签名）
if #data > 32 then
    print("\n" .. string.rep("=", 60))
    print("篡改最后16字节（签名区域）...")
    local tamper_start = #data - 16
    local original = data:sub(tamper_start, tamper_start)
    print(string.format("原始字节[%d]: 0x%02X", tamper_start, string.byte(original)))

    local tampered = data:sub(1, tamper_start-1) ..
                    string.char((string.byte(original) + 1) % 256) ..
                    data:sub(tamper_start+1)

    local fout = io.open("debug_test_tampered.luac", "wb")
    fout:write(tampered)
    fout:close()

    print("尝试加载篡改后的文件...")
    local ok2, result2 = pcall(loadfile, "debug_test_tampered.luac")
    if ok2 then
        print("✗ 篡改后仍能加载！（HMAC验证未生效）")
        local status, err = pcall(result2)
        if not status then
            print(string.format("  执行时报错: %s", tostring(err)))
        else
            print("  甚至执行成功了！")
        end
    else
        print(string.format("✓ 篡改被检测到: %s", tostring(result2)))
    end
end

-- 清理临时文件
os.remove("debug_test.lua")
os.remove("debug_test.luac")
os.remove("debug_test_tampered.luac")

print("\n调试完成")
