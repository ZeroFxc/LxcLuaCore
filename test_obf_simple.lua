-- 测试 string.dump 的混淆功能
-- Flag值: CFF=1, BLOCK_SHUFFLE=2, BOGUS_BLOCKS=4, RANDOM_NOP=512
local flags = 1 | 2 | 4 | 512  -- OBFUSCATE_CFF | BLOCK_SHUFFLE | BOGUS_BLOCKS | RANDOM_NOP

print("=== string.dump 混淆测试 ===")
print("混淆标志: " .. flags)
print()

local function test_case(name, code, expected)
    print("--- " .. name .. " ---")
    
    local fn = load(code)
    if not fn then
        print("  ✗ 加载失败")
        return false
    end
    
    -- 原始执行
    local orig_result = fn()
    print("  原始结果: " .. tostring(orig_result))
    
    -- 混淆 dump
    local dump_ok, obf_dump = pcall(string.dump, fn, {
        obfuscate = flags,
        seed = 12345,
        strip = false
    })
    if not dump_ok then
        print("  ✗ string.dump失败: " .. tostring(obf_dump))
        return false
    end
    print("  混淆字节码长度: " .. #obf_dump)
    
    -- 加载混淆后的字节码
    local load_ok, obf_fn = pcall(load, obf_dump)
    if not load_ok then
        print("  ✗ 加载混淆字节码失败: " .. tostring(obf_fn))
        return false
    end
    if not obf_fn then
        print("  ✗ 加载混淆字节码返回nil")
        return false
    end
    
    -- 执行混淆后的函数
    local call_ok, obf_result = pcall(obf_fn)
    if not call_ok then
        print("  ✗ 执行失败: " .. tostring(obf_result))
        return false
    end
    
    print("  混淆后结果: " .. tostring(obf_result))
    
    if obf_result == expected then
        print("  ✓ 通过!")
        return true
    else
        print("  ✗ 失败! 期望=" .. tostring(expected) .. ", 实际=" .. tostring(obf_result))
        return false
    end
end

local passed = 0
local failed = 0

-- 测试1: 简单加法
if test_case("简单加法", "return 3 + 5", 8) then passed = passed + 1 else failed = failed + 1 end

-- 测试2: 条件分支
if test_case("条件分支", 
    "local a, b = 10, 20; if a > b then return a else return b end", 
    20) then passed = passed + 1 else failed = failed + 1 end

-- 测试3: for循环
if test_case("for循环", 
    "local sum = 0; for i = 1, 10 do sum = sum + i; end; return sum", 
    55) then passed = passed + 1 else failed = failed + 1 end

-- 测试4: while循环
if test_case("while循环", 
    "local i, sum = 1, 0; while i <= 5 do sum = sum + i*i; i = i+1; end; return sum", 
    55) then passed = passed + 1 else failed = failed + 1 end

-- 测试5: 嵌套条件
if test_case("嵌套条件", 
    "local n = 50; if n < 0 then return -1 elseif n == 0 then return 0 elseif n < 100 then return 1 else return 2 end", 
    1) then passed = passed + 1 else failed = failed + 1 end

-- 测试6: repeat-until
if test_case("repeat-until", 
    "local x = 10; repeat x = x-1 until x <= 5; return x", 
    5) then passed = passed + 1 else failed = failed + 1 end

print("\n=== 结果: 通过=" .. passed .. ", 失败=" .. failed .. " ===")
