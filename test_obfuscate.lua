-- test_obfuscate.lua - 完整测试混淆器
-- 用法: lxclua test_obfuscate.lua <lua文件> [期望结果]
-- 示例: lxclua test_obfuscate.lua tests/test_cond.lua 20

local function test_file(filename, expected)
    print("=== 测试文件: " .. filename .. " ===")
    
    -- 读取源码
    local f = io.open(filename, "r")
    if not f then
        print("无法打开文件: " .. filename)
        return false
    end
    local code = f:read("*a")
    f:close()
    
    -- 加载为函数
    local fn, err = load(code, "@" .. filename)
    if not fn then
        print("加载失败: " .. tostring(err))
        return false
    end
    
    -- 获取Proto: 通过debug.getproto
    local info = debug.getinfo(fn, "S")
    print("函数类型: " .. (info and info.what or "unknown"))
    
    -- 使用 debug.getproto 获取 Proto
    local proto = debug.getproto(fn, 0)
    if not proto then
        print("✗ 无法获取Proto")
        return false
    end
    
    print("Proto 获取成功")
    
    -- 应用混淆
    local obf = require "lobfuscate"
    local flags = obf.OBFUSCATE_CFF | obf.OBFUSCATE_BLOCK_SHUFFLE | 
                  obf.OBFUSCATE_BOGUS_BLOCKS | obf.OBFUSCATE_RANDOM_NOP
    
    print("混淆标志: 0x" .. string.format("%x", flags))
    print("开始混淆...")
    
    local seed = 12345
    local ok, err = pcall(function()
        local result = obf.flatten(fn, flags, seed, "cff_test.log")
        if result ~= 0 then
            error("混淆失败! 返回码=" .. result)
        end
    end)
    
    if not ok then
        print("✗ 混淆出错: " .. tostring(err))
        return false
    end
    
    print("混淆完成")
    
    -- 执行混淆后的函数
    print("执行混淆后的函数...")
    local ok, result = pcall(fn)
    if not ok then
        print("✗ 执行失败: " .. tostring(result))
        return false
    end
    
    print("执行结果: " .. tostring(result))
    
    if expected ~= nil then
        if result == expected then
            print("✓ 测试通过!")
            return true
        else
            print("✗ 测试失败! 期望=" .. tostring(expected) .. ", 实际=" .. tostring(result))
            return false
        end
    else
        print("✓ 执行成功 (无期望值验证)")
        return true
    end
end

-- 获取命令行参数
local filename = arg[1]
local expected = arg[2] and tonumber(arg[2]) or nil

if not filename then
    print("用法: lxclua test_obfuscate.lua <lua文件> [期望结果]")
    return
end

test_file(filename, expected)
