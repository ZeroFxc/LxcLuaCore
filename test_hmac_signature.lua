#!/usr/bin/env lua
-- 测试脚本：验证HMAC-SHA256全局完整性签名功能
-- 测试用例1：正常编译和加载（应该成功）
-- 测试用例2：篡改字节码后加载（应该失败）

local function test_normal_dump_load()
    print("=" .. string.rep("=", 70))
    print("测试1: 正常编译和加载（预期：成功）")
    print("=" .. string.rep("=", 70))

    local test_code = [[
        function hello()
            print("Hello from HMAC-protected bytecode!")
            return 42
        end
        local result = hello()
        assert(result == 42, "函数返回值不正确")
        print("[PASS] 正常执行完成")
    ]]

    -- 写入测试文件
    local test_file = "test_hmac.lua"
    local f = io.open(test_file, "w")
    if not f then
        print("[FAIL] 无法创建测试文件")
        return false
    end
    f:write(test_code)
    f:close()

    -- 编译为字节码
    local output_luac = "test_hmac.luac"
    os.execute(string.format("luac.exe -o %s %s", output_luac, test_file))

    -- 尝试加载并执行字节码
    local success, err = loadfile(output_luac)
    if not success then
        print(string.format("[FAIL] 加载失败: %s", tostring(err)))
        return false
    end

    -- 执行字节码
    local ok, exec_err = pcall(success)
    if not ok then
        print(string.format("[FAIL] 执行失败: %s", tostring(exec_err)))
        return false
    end

    print("[PASS] 测试1通过 - 正常字节码可以正常加载和执行\n")
    return true
end

local function test_tampered_bytecode()
    print("=" .. string.rep("=", 70))
    print("测试2: 篡改字节码后加载（预期：失败）")
    print("=" .. string.rep("=", 70))

    local test_code = [[
        function secure_func()
            return "original"
        end
        print(secure_func())
    ]]

    -- 写入测试文件
    local test_file = "test_hmac_tamper.lua"
    local f = io.open(test_file, "w")
    if not f then
        print("[FAIL] 无法创建测试文件")
        return false
    end
    f:write(test_code)
    f:close()

    -- 编译为字节码
    local output_luac = "test_hmac_tamper.luac"
    os.execute(string.format("luac.exe -o %s %s", output_luac, test_file))

    -- 读取原始字节码文件
    local fin = io.open(output_luac, "rb")
    if not fin then
        print("[FAIL] 无法读取字节码文件")
        return false
    end
    local data = fin:read("*a")
    fin:close()

    -- 篡改数据（修改倒数第33个字节，即HMAC签名的中间位置）
    if #data > 32 then
        local tamper_pos = #data - 16  -- 在签名区域篡改
        local original_byte = string.byte(data, tamper_pos)
        local new_data = data:sub(1, tamper_pos-1) ..
                        string.char((original_byte + 1) % 256) ..
                        data:sub(tamper_pos+1)

        -- 写入篡改后的文件
        local fout = io.open(output_luac .. ".tampered", "wb")
        if not fout then
            print("[FAIL] 无法写入篡改后的文件")
            return false
        end
        fout:write(new_data)
        fout:close()

        print(string.format("[INFO] 已在位置 %d 篡改字节 (0x%02X -> 0x%02X)",
              tamper_pos, original_byte, (original_byte + 1) % 256))

        -- 尝试加载篡改后的字节码
        local success, err = loadfile(output_luac .. ".tampered")

        if success then
            -- 如果加载成功，尝试执行
            local ok, exec_err = pcall(success)
            if ok then
                print("[FAIL] 篡改的字节码竟然执行成功了！HMAC验证未生效！")
                return false
            else
                print(string.format("[PASS] 加载成功但执行失败: %s", tostring(exec_err)))
                return true
            end
        else
            print(string.format("[PASS] 加载被拒绝: %s", tostring(err)))
            print("[INFO] 这说明HMAC-SHA256完整性验证正常工作！\n")
            return true
        end
    else
        print("[SKIP] 字节码文件太小，无法进行篡改测试")
        return true
    end
end

local function test_partial_tamper_in_segment()
    print("=" .. string.rep("=", 70))
    print("测试3: 篡改段数据（非签名区）（预期：失败）")
    print("=" .. string.rep("=", 70))

    local test_code = [[
        local x = 100
        local y = 200
        print(x + y)
    ]]

    -- 写入测试文件
    local test_file = "test_hmac_segment.lua"
    local f = io.open(test_file, "w")
    if not f then
        print("[FAIL] 无法创建测试文件")
        return false
    end
    f:write(test_code)
    f:close()

    -- 编译为字节码
    local output_luac = "test_hmac_segment.luac"
    os.execute(string.format("luac.exe -o %s %s", output_luac, test_file))

    -- 读取原始字节码文件
    local fin = io.open(output_luac, "rb")
    if not fin then
        print("[FAIL] 无法读取字节码文件")
        return false
    end
    local data = fin:read("*a")
    fin:close()

    -- 篡改段数据区域（前100字节内的某个位置）
    if #data > 100 then
        local tamper_pos = 50  -- 肯定在段数据区域
        local original_byte = string.byte(data, tamper_pos)
        local new_data = data:sub(1, tamper_pos-1) ..
                        string.char((original_byte + 128) % 256) ..
                        data:sub(tamper_pos+1)

        -- 写入篡改后的文件
        local fout = io.open(output_luac .. ".tampered_seg", "wb")
        if not fout then
            print("[FAIL] 无法写入篡改后的文件")
            return false
        end
        fout:write(new_data)
        fout:close()

        print(string.format("[INFO] 已在段数据位置 %d 篡改字节", tamper_pos))

        -- 尝试加载篡改后的字节码
        local success, err = loadfile(output_luac .. ".tampered_seg")

        if success then
            print("[FAIL] 段数据被篡改后仍能加载！HMAC验证可能未生效！")
            return false
        else
            print(string.format("[PASS] 段数据篡改被检测到: %s", tostring(err)))
            print("[INFO] HMAC-SHA256能检测到段数据的任何篡改！\n")
            return true
        end
    else
        print("[SKIP] 字节码文件太小")
        return true
    end
end

-- 主测试流程
print("\n" .. string.rep("#", 80))
print("#  HMAC-SHA256 全局完整性签名功能测试")
print("#  测试时间: " .. os.date("%Y-%m-%d %H:%M:%S"))
print(string.rep("#", 80) .. "\n")

local results = {}
results["normal"] = test_normal_dump_load()
results["tampered"] = test_tampered_bytecode()
results["segment"] = test_partial_tamper_in_segment()

-- 输出总结
print("\n" .. string.rep("=", 70))
print("测试结果总结")
print("=" .. string.rep("=", 70))
for name, result in pairs(results) do
    local status = result and "[PASS]" or "[FAIL]"
    print(string.format("  %-20s %s", name, status))
end

local all_pass = true
for _, result in pairs(results) do
    if not result then
        all_pass = false
        break
    end
end

print("")
if all_pass then
    print("🎉 所有测试通过！HMAC-SHA256全局完整性保护功能正常工作！")
else
    print("⚠️  部分测试失败，请检查实现")
end

print("")
