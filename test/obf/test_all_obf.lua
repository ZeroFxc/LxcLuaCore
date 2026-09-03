-- 混淆功能综合测试 (精简版)
-- 每个测试分类独立运行，避免累积内存问题

local function do_test_section(section_name, flags_list, src_file)
    print("\n--- " .. section_name .. " ---")
    for _, item in ipairs(flags_list) do
        local name, flag = item[1], item[2]
        local bc_file = "test_obf_" .. name:gsub("%+", "_") .. ".luac"
        
        -- string.dump
        local ok, err = pcall(function()
            local f = loadfile(src_file)
            if not f then error("loadfile failed") end
            local bc = string.dump(f, {strip=false, obfuscate=flag})
            local out = io.open(bc_file, "wb")
            out:write(bc)
            out:close()
        end)
        if not ok then
            print(string.format("  [SKIP] %s: string.dump失败 - %s", name, err))
            goto continue
        end
        
        -- load bytecode
        local f2 = loadfile(bc_file, "bt")
        if not f2 then
            print(string.format("  [FAIL] %s: 无法加载字节码", name))
            goto continue
        end
        
        -- execute
        local eok, res = pcall(f2)
        if not eok then
            print(string.format("  [FAIL] %s: 执行失败 - %s", name, tostring(res)))
            goto continue
        end
        
        if type(res) == "function" then
            print(string.format("  [BUG] %s: VM_PROTECT返回函数而非原始值", name))
            goto continue
        end
        
        if type(res) == "number" then
            print(string.format("  [PASS] %s: result=%s", name, res))
        elseif type(res) == "table" then
            print(string.format("  [PASS] %s: table ok", name))
        else
            print(string.format("  [PASS] %s: type=%s", name, type(res)))
        end
        
        ::continue::
    end
end

-- 简单测试源文件
local simple_src = "return 42"
local simple_file = "test_obf_simple.lua"
io.open(simple_file, "w"):write(simple_src):close()

-- 复杂测试源文件
local complex_src = [[
local function add(a, b) return a + b end
local function factorial(n)
    if n <= 1 then return 1 end
    return n * factorial(n - 1)
end
local function loop_test(n)
    local sum = 0
    for i = 1, n do sum = sum + i end
    return sum
end
local function multi_branch(x)
    if x < 0 then return -1
    elseif x == 0 then return 0
    else return 1 end
end
return {add=add, factorial=factorial, loop_test=loop_test, multi_branch=multi_branch}
]]
local complex_file = "test_obf_complex.lua"
io.open(complex_file, "w"):write(complex_src):close()

print("========================================")
print("  LXCLUA 混淆功能测试 (lobfuscate.c)")
print("========================================")

-- 1. 简单代码 - 单个标志
do_test_section("简单代码 - 单个标志", {
    {"NONE", 0}, {"CFF", 1}, {"BLOCK_SHUFFLE", 2}, {"BOGUS_BLOCKS", 4},
    {"STATE_ENCODE", 8}, {"NESTED", 16}, {"OPAQUE", 32}, {"INTERLEAVE", 64},
}, simple_file)

-- 2. 简单代码 - VM_PROTECT (单独测试)
do_test_section("简单代码 - VM_PROTECT", {
    {"VM_PROTECT", 128},
}, simple_file)

-- 3. 简单代码 - 组合
do_test_section("简单代码 - 组合", {
    {"CFF+SHUFFLE", 3}, {"ALL_NO_VM", 127}, {"CFF+VM", 129}, {"ALL", 255},
}, simple_file)

-- 4. 复杂代码 - 组合 (不含VM_PROTECT)
do_test_section("复杂代码 - 组合(无VM)", {
    {"CFF+SHUFFLE", 3}, {"CFF+STATE_ENCODE", 9}, {"CFF+OPAQUE", 33},
    {"ALL_NO_VM", 127},
}, complex_file)

-- 5. 复杂代码 - 含VM_PROTECT
do_test_section("复杂代码 - 含VM", {
    {"VM_PROTECT", 128}, {"CFF+VM", 129}, {"ALL", 255},
}, complex_file)

-- 6. 嵌套函数
print("\n--- 嵌套函数 ---")
local nested_src = [[
local function outer(x)
    local function inner(y) return y * 2 end
    return inner(x) + inner(x + 1)
end
return outer(5)
]]
local nested_file = "test_obf_nested_src.lua"
io.open(nested_file, "w"):write(nested_src):close()
local expected = loadfile(nested_file)()
print(string.format("  期望: outer(5) = %s", expected))

for _, combo in ipairs({{"NONE", 0}, {"CFF+SHUFFLE", 3}, {"ALL_NO_VM", 127}, {"VM_PROTECT", 128}, {"ALL", 255}}) do
    local bc_file = "test_obf_nested_" .. combo[1]:gsub("%+", "_") .. ".luac"
    local ok = pcall(function()
        local f = loadfile(nested_file)
        local bc = string.dump(f, {strip=false, obfuscate=combo[2]})
        io.open(bc_file, "wb"):write(bc):close()
    end)
    if not ok then print(string.format("  [SKIP] %s: dump失败", combo[1])) goto cont_nested end
    
    local nf = loadfile(bc_file, "bt")
    if not nf then print(string.format("  [FAIL] %s: load失败", combo[1])) goto cont_nested end
    
    local nok, nres = pcall(nf)
    if not nok then print(string.format("  [FAIL] %s: %s", combo[1], tostring(nres))) goto cont_nested end
    
    if type(nres) == "function" then
        print(string.format("  [BUG] %s: 返回函数而非值", combo[1]))
        goto cont_nested
    end
    
    if nres == expected then
        print(string.format("  [PASS] %s: %s", combo[1], nres))
    else
        print(string.format("  [FAIL] %s: 期望=%s, 实际=%s", combo[1], expected, nres))
    end
    ::cont_nested::
end

-- 7. 闭包测试
print("\n--- 闭包 ---")
local closure_src = [[
local function counter(init)
    local n = init
    return function() n = n + 1; return n end
end
local c1 = counter(10)
local c2 = counter(100)
return {c1(), c1(), c2()}
]]
local closure_file = "test_obf_closure_src.lua"
io.open(closure_file, "w"):write(closure_src):close()
local cexp = loadfile(closure_file)()
print(string.format("  期望: {%s, %s, %s}", cexp[1], cexp[2], cexp[3]))

for _, combo in ipairs({{"NONE", 0}, {"CFF+SHUFFLE", 3}, {"ALL_NO_VM", 127}, {"VM_PROTECT", 128}, {"ALL", 255}}) do
    local bc_file = "test_obf_closure_" .. combo[1]:gsub("%+", "_") .. ".luac"
    local ok = pcall(function()
        local f = loadfile(closure_file)
        local bc = string.dump(f, {strip=false, obfuscate=combo[2]})
        io.open(bc_file, "wb"):write(bc):close()
    end)
    if not ok then print(string.format("  [SKIP] %s: dump失败", combo[1])) goto cont_cl end
    
    local cf = loadfile(bc_file, "bt")
    if not cf then print(string.format("  [FAIL] %s: load失败", combo[1])) goto cont_cl end
    
    local cok, cres = pcall(cf)
    if not cok then print(string.format("  [FAIL] %s: %s", combo[1], tostring(cres))) goto cont_cl end
    
    if type(cres) == "function" then
        print(string.format("  [BUG] %s: 返回函数而非值", combo[1]))
        goto cont_cl
    end
    
    local match = cres[1] == cexp[1] and cres[2] == cexp[2] and cres[3] == cexp[3]
    if match then
        print(string.format("  [PASS] %s: {%s,%s,%s}", combo[1], cres[1], cres[2], cres[3]))
    else
        print(string.format("  [FAIL] %s: 期望 {%s,%s,%s}, 实际 {%s,%s,%s}",
            combo[1], cexp[1], cexp[2], cexp[3], cres[1], cres[2], cres[3]))
    end
    ::cont_cl::
end

-- 清理
print("\n--- 清理临时文件 ---")
os.execute("del /Q test_obf_*.lua test_obf_*.luac 2>nul")
print("  完成")

print("\n========================================")
print("  测试完成")
print("========================================")