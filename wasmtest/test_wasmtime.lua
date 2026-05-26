-- test_wasmtime.lua — 修复版
local wasmtime = require("wasmtime")

local passed = 0
local failed = 0

-- 辅助函数: 读取文件内容为字符串
local function read_file(path)
    local f = io.open(path, "rb")
    if not f then
        return nil, "cannot open file: " .. path
    end
    local data = f:read("*all")
    f:close()
    return data
end

-- 通用获取导出函数（自动兼容 _xxx / xxx 两种命名）
local function get_export_func(instance, name)
    local func = instance:getExport("_"..name)
    if func then return func end
    return instance:getExport(name)
end

-- 加载 WASM 模块
local wasm_path = "test_wasm_module.wasm"
print("=== 加载 WASM 模块: " .. wasm_path .. " ===")
local wasm_bytes, err = read_file(wasm_path)
if not wasm_bytes then
    print("FAIL: " .. err)
    os.exit(1)
end
print("WASM 文件大小: " .. #wasm_bytes .. " 字节")

-- 创建引擎实例
local engine   = wasmtime.newEngine()
local store    = wasmtime.newStore(engine)
local module   = wasmtime.newModule(engine, wasm_bytes)
local instance = wasmtime.newInstance(store, module)

print("Engine/Store/Module/Instance 创建成功")
print("")

-- ==========================================================
-- 测试 1: add(a, b)
-- ==========================================================
print("=== 测试 1: add(10, 20) ===")
local func_add = get_export_func(instance, "add")
if func_add then
    local result = func_add:call(10, 20)
    print("  add(10, 20) = " .. tostring(result))
    if result == 30 then
        print("  TEST 1 PASSED")
        passed = passed + 1
    else
        print("  TEST 1 FAILED: expected 30, got " .. tostring(result))
        failed = failed + 1
    end
else
    print("  TEST 1 FAILED: add function not found")
    failed = failed + 1
end
print("")

-- ==========================================================
-- 测试 2: fib(n)
-- ==========================================================
print("=== 测试 2: fib(10) ===")
local func_fib = get_export_func(instance, "fib")
if func_fib then
    local result = func_fib:call(10)
    print("  fib(10) = " .. tostring(result))
    if result == 55 then
        print("  TEST 2 PASSED")
        passed = passed + 1
    else
        print("  TEST 2 FAILED: expected 55, got " .. tostring(result))
        failed = failed + 1
    end

    -- 额外 fib20，判空再调用避免崩溃
    local r20 = func_fib:call(20)
    print("  fib(20) = " .. tostring(r20) .. " (expected 6765)")
else
    print("  TEST 2 FAILED: fib function not found")
    failed = failed + 1
end
print("")

-- ==========================================================
-- 测试 3: mul(a, b)
-- ==========================================================
print("=== 测试 3: mul(7, 8) ===")
local func_mul = get_export_func(instance, "mul")
if func_mul then
    local result = func_mul:call(7, 8)
    print("  mul(7, 8) = " .. tostring(result))
    if result == 56 then
        print("  TEST 3 PASSED")
        passed = passed + 1
    else
        print("  TEST 3 FAILED: expected 56, got " .. tostring(result))
        failed = failed + 1
    end
else
    print("  TEST 3 FAILED: mul function not found")
    failed = failed + 1
end
print("")

-- ==========================================================
-- 测试 4: factorial(n)
-- ==========================================================
print("=== 测试 4: factorial(6) ===")
local func_fact = get_export_func(instance, "factorial")
if func_fact then
    local result = func_fact:call(6)
    print("  factorial(6) = " .. tostring(result))
    if result == 720 then
        print("  TEST 4 PASSED")
        passed = passed + 1
    else
        print("  TEST 4 FAILED: expected 720, got " .. tostring(result))
        failed = failed + 1
    end
else
    print("  TEST 4 FAILED: factorial function not found")
    failed = failed + 1
end
print("")

-- ==========================================================
-- 测试 5: add_double(a, b) — 浮点数测试
-- ==========================================================
print("=== 测试 5: add_double(3.14, 2.86) ===")
local func_dadd = get_export_func(instance, "add_double")
if func_dadd then
    local result = func_dadd:call(3.14, 2.86)
    print("  add_double(3.14, 2.86) = " .. tostring(result))
    local diff = math.abs(result - 6.0)
    if diff < 0.0001 then
        print("  TEST 5 PASSED")
        passed = passed + 1
    else
        print("  TEST 5 FAILED: expected ~6.0, got " .. tostring(result))
        failed = failed + 1
    end
else
    print("  TEST 5 FAILED: add_double function not found")
    failed = failed + 1
end
print("")

-- ==========================================================
-- 测试 6: is_prime(n)
-- ==========================================================
print("=== 测试 6: is_prime ===")
local func_prime = get_export_func(instance, "is_prime")
if func_prime then
    local tests = {
        {2, 1}, {3, 1}, {4, 0}, {17, 1}, {97, 1}, {100, 0}
    }
    local all_ok = true
    for _, t in ipairs(tests) do
        local result = func_prime:call(t[1])
        local expected = t[2]
        local status = result == expected and "OK" or "FAIL"
        if result ~= expected then all_ok = false end
        print(string.format("  is_prime(%d) = %d (expected %d) [%s]", t[1], result, expected, status))
    end
    if all_ok then
        print("  TEST 6 PASSED")
        passed = passed + 1
    else
        print("  TEST 6 FAILED")
        failed = failed + 1
    end
else
    print("  TEST 6 FAILED: is_prime function not found")
    failed = failed + 1
end
print("")

-- ==========================================================
-- 测试 7: gcd(a, b)
-- ==========================================================
print("=== 测试 7: gcd ===")
local func_gcd = get_export_func(instance, "gcd")
if func_gcd then
    local tests = {
        {48, 18, 6},
        {56, 98, 14},
        {17, 13, 1},
        {100, 10, 10},
    }
    local all_ok = true
    for _, t in ipairs(tests) do
        local result = func_gcd:call(t[1], t[2])
        local expected = t[3]
        local status = result == expected and "OK" or "FAIL"
        if result ~= expected then all_ok = false end
        print(string.format("  gcd(%d, %d) = %d (expected %d) [%s]", t[1], t[2], result, expected, status))
    end
    if all_ok then
        print("  TEST 7 PASSED")
        passed = passed + 1
    else
        print("  TEST 7 FAILED")
        failed = failed + 1
    end
else
    print("  TEST 7 FAILED: gcd function not found")
    failed = failed + 1
end

print("")
print("========================")
print(string.format("结果: %d passed, %d failed, %d total", passed, failed, passed + failed))
