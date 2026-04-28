-- 全面测试混淆器的复杂用例
-- Flag值: CFF=1, BLOCK_SHUFFLE=2, BOGUS_BLOCKS=4, RANDOM_NOP=512, STATE_ENCODE=8
local flags = 1 | 2 | 4 | 512 | 8  -- 全部启用

print("=== 混淆器全面测试 ===")
print("混淆标志: " .. flags)
print()

-- 辅助: 比较两个值
local function values_match(actual, expected)
    if type(expected) == "table" then
        -- 多返回值: 需要逐个比较
        -- 这里我们用一个技巧: 实际执行时捕获返回值到全局
        return "table_check"
    end
    return actual == expected
end

-- 简单测试函数: 直接比较单返回值
local function test(name, code, expected)
    print("--- " .. name .. " ---")
    
    local fn = load(code)
    if not fn then
        print("  ✗ 加载失败")
        return false
    end
    
    -- 原始执行
    local ok, result = pcall(fn)
    if not ok then
        print("  ✗ 原始执行失败: " .. tostring(result))
        return false
    end
    print("  原始结果: " .. tostring(result))
    
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

-- 多返回值测试函数
local function test_multi(name, code, expected_count, expected_values)
    print("--- " .. name .. " ---")
    
    local fn = load(code)
    if not fn then
        print("  ✗ 加载失败")
        return false
    end
    
    -- 原始执行
    local results = {}
    for i = 1, expected_count do
        local wrapper = "local r = (function() " .. code .. " end)(); return r"
        results[i] = load(wrapper)()
    end
    
    local orig_values = {}
    local f = function() return fn() end
    local tmp = {}
    do
        -- 使用 select 技巧来捕获多返回值
        local n = 0
        local function check(...)
            n = select('#', ...)
            for i = 1, n do tmp[i] = select(i, ...) end
        end
        pcall(check, fn())
        for i = 1, n do orig_values[i] = tmp[i] end
    end
    
    print("  原始结果: " .. expected_count .. " 个返回值")
    
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
    
    -- 执行并捕获多返回值
    local obf_values = {}
    do
        local n = 0
        local tmp2 = {}
        local function check(...)
            n = select('#', ...)
            for i = 1, n do tmp2[i] = select(i, ...) end
        end
        local call_ok = pcall(check, obf_fn())
        if not call_ok then
            print("  ✗ 执行失败")
            return false
        end
        for i = 1, n do obf_values[i] = tmp2[i] end
        
        print("  混淆后结果: " .. n .. " 个返回值")
        for i = 1, n do
            print("    [" .. i .. "] = " .. tostring(obf_values[i]))
        end
        
        -- 比较
        if n ~= expected_count then
            print("  ✗ 失败! 期望 " .. expected_count .. " 个返回值, 实际 " .. n .. " 个")
            return false
        end
        for i = 1, expected_count do
            if obf_values[i] ~= expected_values[i] then
                print("  ✗ 失败! [" .. i .. "] 期望=" .. tostring(expected_values[i]) .. ", 实际=" .. tostring(obf_values[i]))
                return false
            end
        end
        print("  ✓ 通过!")
        return true
    end
end

local passed = 0
local failed = 0

-- 单返回值测试
local function run_single(name, code, expected)
    if test(name, code, expected) then passed = passed + 1 else failed = failed + 1 end
end

run_single("简单加法", "return 3 + 5", 8)
run_single("条件分支", "local a, b = 10, 20; if a > b then return a else return b end", 20)
run_single("for循环", "local sum = 0; for i = 1, 10 do sum = sum + i; end; return sum", 55)
run_single("while循环", "local i, sum = 1, 0; while i <= 5 do sum = sum + i*i; i = i+1; end; return sum", 55)
run_single("repeat-until", "local x = 10; repeat x = x-1 until x <= 5; return x", 5)
run_single("嵌套条件", "local n = 50; if n < 0 then return -1 elseif n == 0 then return 0 elseif n < 100 then return 1 else return 2 end", 1)
run_single("for循环break", [[local found = nil
for i = 1, 20 do
    if i * i >= 50 then
        found = i
        break
    end
end
return found]], 8)
run_single("while循环break", [[local i = 1
local sum = 0
while true do
    if i > 10 then break end
    sum = sum + i
    i = i + 1
end
return sum]], 55)
run_single("嵌套函数调用", [[function add(a, b) return a + b end
function mul(a, b) return a * b end
return mul(add(2, 3), add(4, 5))]], 45)
run_single("多层循环", [[local sum = 0
for i = 1, 3 do
    for j = 1, 3 do
        sum = sum + i * j
    end
end
return sum]], 36)
run_single("斐波那契", [[function fib(n)
    if n <= 1 then return n end
    local a, b = 0, 1
    for i = 2, n do
        a, b = b, a + b
    end
    return b
end
return fib(10)]], 55)
run_single("字符串操作", 'local s = "hello" .. " " .. "world"; return string.len(s)', 11)
run_single("表操作", [[local t = {1, 2, 3, 4, 5}
local sum = 0
for i = 1, #t do
    sum = sum + t[i]
end
return sum]], 15)
run_single("复杂表达式", [[local x = ((2 + 3) * 4 - 5) / 3
local y = x * x
return y]], 25.0)
run_single("goto语句", [[local x = 0
::start::
x = x + 1
if x < 5 then goto start end
return x]], 5)
run_single("三元模拟", [[local x = 15
local result = (x > 10) and "big" or "small"
return result]], "big")

-- 多返回值测试
if test_multi("逻辑运算符(3返回值)", 
    [[local a = false or "default"
local b = true and "yes"
local c = false and "no" or "fallback"
return a, b, c]], 
    3, {"default", "yes", "fallback"}) then passed = passed + 1 else failed = failed + 1 end

if test_multi("多返回值(divmod)", 
    [[function divmod(a, b)
    return a // b, a % b
end
local q, r = divmod(17, 5)
return q, r]], 
    2, {3, 2}) then passed = passed + 1 else failed = failed + 1 end

print("\n=== 结果: 通过=" .. passed .. ", 失败=" .. failed .. " ===")

if failed == 0 then
    print("\n✓✓✓ 所有测试通过! 混淆器工作正常 ✓✓✓")
else
    print("\n✗✗✗ 有 " .. failed .. " 个测试失败! 需要修复 ✗✗✗")
end
