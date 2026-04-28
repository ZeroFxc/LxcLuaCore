-- 测试混淆器 - 验证混淆后代码的正确性
local tcc = require "tcc"

print("=== 混淆器正确性测试 ===\n")

local function test_obfuscate(name, code, expected)
    print("--- " .. name .. " ---")
    
    local c_code = tcc.compile(code, {obfuscate = true}, "test_" .. name)
    if not c_code then
        print("✗ 编译失败: 返回nil")
        return false
    end
    
    -- 保存C代码到文件
    local c_file = "tests/obf_" .. name .. ".c"
    local so_file = "tests/obf_" .. name .. ".so"
    local f = io.open(c_file, "w")
    if not f then
        print("✗ 无法创建C文件")
        return false
    end
    f:write(c_code)
    f:close()
    
    -- 编译C代码
    local cmd = string.format("gcc -O2 -shared -fPIC -o %s %s -I. 2>&1", so_file, c_file)
    local ret = os.execute(cmd)
    if ret ~= true and ret ~= 0 then
        print("✗ C代码编译失败")
        print("命令: " .. cmd)
        -- 输出部分C代码用于调试
        print("C代码前500字符:")
        print(c_code:sub(1, 500))
        return false
    end
    
    -- 加载并执行
    package.cpath = "./tests/?.so;" .. package.cpath
    local ok, mod = pcall(require, "obf_" .. name)
    if not ok then
        print("✗ 加载模块失败: " .. tostring(mod))
        return false
    end
    
    -- 比较结果
    if expected ~= nil then
        if mod == expected then
            print("✓ 通过 (结果=" .. tostring(mod) .. ")")
            return true
        else
            print("✗ 失败! 期望=" .. tostring(expected) .. ", 实际=" .. tostring(mod))
            return false
        else
        end
    else
        print("✓ 执行成功 (结果=" .. tostring(mod) .. ")")
        return true
    end
end

local passed = 0
local failed = 0

-- 测试1: 简单函数
if test_obfuscate("t1_simple", [[
function add(a, b)
    return a + b
end
return add(3, 5)
]], 8) then passed = passed + 1 else failed = failed + 1 end

-- 测试2: 条件分支
if test_obfuscate("t2_cond", [[
function max(a, b)
    if a > b then
        return a
    else
        return b
    end
end
return max(10, 20)
]], 20) then passed = passed + 1 else failed = failed + 1 end

-- 测试3: 循环
if test_obfuscate("t3_loop", [[
local sum = 0
for i = 1, 10 do
    sum = sum + i
end
return sum
]], 55) then passed = passed + 1 else failed = failed + 1 end

print("\n=== 测试汇总 ===")
print("通过: " .. passed .. ", 失败: " .. failed)
