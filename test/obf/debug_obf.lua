-- 混淆调试脚本：复现已知问题
print("=== LXCLUA 混淆问题调试 ===")

local function test_obf(name, flag, src, expected_func)
    local src_file = "_debug_" .. name:gsub("%+", "_") .. ".lua"
    local bc_file = "_debug_" .. name:gsub("%+", "_") .. ".luac"

    -- 写源文件
    local f = io.open(src_file, "w")
    f:write(src)
    f:close()

    -- string.dump
    local ok, err = pcall(function()
        local f2 = loadfile(src_file)
        if not f2 then error("loadfile failed: " .. (err or "?")) end
        local bc = string.dump(f2, {strip = false, obfuscate = flag})
        local out = io.open(bc_file, "wb")
        out:write(bc)
        out:close()
    end)
    if not ok then
        print(string.format("  [SKIP] %s(flag=%d): dump失败 - %s", name, flag, err))
        return
    end

    -- load字节码
    local lf = loadfile(bc_file, "bt")
    if not lf then
        print(string.format("  [FAIL] %s(flag=%d): 无法加载字节码", name, flag))
        return
    end

    -- 执行
    local eok, eres = pcall(lf)
    if not eok then
        print(string.format("  [FAIL] %s(flag=%d): 执行失败 - %s", name, flag, tostring(eres)))
        return
    end

    -- 检查结果
    if expected_func then
        local exp = expected_func()
        if type(eres) == "function" then
            print(string.format("  [BUG]  %s(flag=%d): 返回function而非%s(期望%s)", name, flag, type(exp), exp))
            -- 尝试调用这个函数
            local ok2, res2 = pcall(eres)
            if ok2 then
                print(string.format("         调用返回: type=%s, val=%s", type(res2), tostring(res2)))
            else
                print(string.format("         调用失败: %s", tostring(res2)))
            end
        elseif eres == exp then
            print(string.format("  [PASS] %s(flag=%d): %s", name, flag, eres))
        else
            print(string.format("  [FAIL] %s(flag=%d): 期望%s, 实际%s", name, flag, exp, eres))
        end
    else
        print(string.format("  [OK]   %s(flag=%d): type=%s", name, flag, type(eres)))
    end
end

-- 1. 简单代码 VM_PROTECT
print("\n--- 1. 简单代码 VM_PROTECT ---")
test_obf("simple_vm", 128, "return 42", function() return 42 end)

-- 2. 简单代码 ALL_NO_VM
print("\n--- 2. 简单代码 ALL_NO_VM ---")
test_obf("simple_allnovm", 127, "return 42", function() return 42 end)

-- 3. 嵌套函数 VM_PROTECT
print("\n--- 3. 嵌套函数 VM_PROTECT ---")
local nested_src = [[
local function outer(x)
    local function inner(y) return y * 2 end
    return inner(x) + inner(x + 1)
end
return outer(5)
]]
test_obf("nested_vm", 128, nested_src, function() return 22 end)

-- 4. 嵌套函数 ALL_NO_VM
print("\n--- 4. 嵌套函数 ALL_NO_VM ---")
test_obf("nested_allnovm", 127, nested_src, function() return 22 end)

-- 5. 闭包 VM_PROTECT
print("\n--- 5. 闭包 VM_PROTECT ---")
local closure_src = [[
local function counter(init)
    local n = init
    return function() n = n + 1; return n end
end
local c1 = counter(10)
return {c1(), c1(), c1()}
]]
test_obf("closure_vm", 128, closure_src, function() return {11, 12, 13} end)

-- 6. 手机上值 VM_PROTECT
print("\n--- 6. 手机上值 VM_PROTECT ---")
test_obf("upvalue_vm", 128, "local x = 10; return function() return x end", nil)

-- 7. 纯常量 NONE
print("\n--- 7. 纯常量 NONE ---")
test_obf("simple_none", 0, "return 42", function() return 42 end)

-- 8. 纯常量 CFF
print("\n--- 8. 纯常量 CFF ---")
test_obf("simple_cff", 1, "return 42", function() return 42 end)

-- 9. 纯常量 CFF+SHUFFLE
print("\n--- 9. 纯常量 CFF+SHUFFLE ---")
test_obf("simple_cff_shuffle", 3, "return 42", function() return 42 end)

print("\n=== 调试完成 ===")