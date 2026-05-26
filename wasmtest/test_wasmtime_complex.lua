-- test_wasmtime_complex.lua — 测试 wasmtime 复杂计算 WASM 模块
-- test_wasm_complex.wasm: 10 个导出函数, 测试大数递归/浮点精度/迭代算法/边缘情况

local wasmtime = require("wasmtime")

local passed = 0
local failed = 0

local function read_file(path)
    local f = io.open(path, "rb")
    if not f then return nil, "cannot open: " .. path end
    local data = f:read("*all")
    f:close()
    return data
end

local function get_export_func(instance, name)
    local func = instance:getExport("_" .. name)
    if func then return func end
    return instance:getExport(name)
end

local function check(name, actual, expected)
    if actual == expected then
        print(string.format("  [OK] %s(%s) = %s", name, tostring(actual), tostring(expected)))
        passed = passed + 1
        return true
    else
        print(string.format("  [FAIL] %s: got %s, expected %s", name, tostring(actual), tostring(expected)))
        failed = failed + 1
        return false
    end
end

local function check_near(name, actual, expected, tol)
    local diff = math.abs(actual - expected)
    if diff <= tol then
        print(string.format("  [OK] %s: %.8f ~= %.8f (diff=%.2e)", name, actual, expected, diff))
        passed = passed + 1
        return true
    else
        print(string.format("  [FAIL] %s: got %.8f, expected %.8f (diff=%.2e)", name, actual, expected, diff))
        failed = failed + 1
        return false
    end
end

-- ==========================================================
-- 加载模块
-- ==========================================================
local wasm_path = "test_wasm_complex.wasm"
print("=== 加载 WASM 模块: " .. wasm_path .. " ===")
local wasm_bytes, err = read_file(wasm_path)
if not wasm_bytes then print("FAIL: " .. err); os.exit(1) end
print("WASM 大小: " .. #wasm_bytes .. " 字节")

local engine   = wasmtime.newEngine()
local store    = wasmtime.newStore(engine)
local module   = wasmtime.newModule(engine, wasm_bytes)
local instance = wasmtime.newInstance(store, module)
print("引擎/模块/实例 创建成功\n")

-- ==========================================================
-- 测试 1: Ackermann 函数
-- ==========================================================
print("=== 测试 1: Ackermann 函数 ===")
local fn_ack = get_export_func(instance, "ackermann")
if fn_ack then
    -- ack(0,0)=1, ack(0,5)=6, ack(1,3)=5, ack(2,2)=7, ack(3,3)=61, ack(3,5)=253
    local cases = {
        {0, 0, 1,  "ack(0,0)"},
        {0, 5, 6,  "ack(0,5)"},
        {1, 3, 5,  "ack(1,3)"},
        {2, 2, 7,  "ack(2,2)"},
        {3, 3, 61, "ack(3,3)"},
        {3, 5, 253,"ack(3,5)"},
    }
    for _, c in ipairs(cases) do
        local r = fn_ack:call(c[1], c[2])
        check(c[4], r, c[3])
    end
else
    print("  [FAIL] ackermann not found")
    failed = failed + 1
end
print("")

-- ==========================================================
-- 测试 2: Mandelbrot 迭代
-- ==========================================================
print("=== 测试 2: Mandelbrot 迭代 ===")
local fn_mandel = get_export_func(instance, "mandelbrot")
if fn_mandel then
    -- 原点 (0,0) 在集合内, 应达到 max_iter
    local r1 = fn_mandel:call(0, 0, 100)
    check("mandelbrot(0,0,100)=100", r1, 100)

    -- 点 (2,0) 即 cx=2.0 缩放后=2000, 应从集合逃逸
    local r2 = fn_mandel:call(2000, 0, 100)
    print(string.format("  [INFO] mandelbrot(2.0, 0) max100 = %d (expect escape < 100)", r2))
    check("mandelbrot(2000,0,100)<100", r2 < 100, true)

    -- 边界点 (-0.75, 0.1) ≈ (-750, 100) 缩放
    local r3 = fn_mandel:call(-750, 100, 200)
    print(string.format("  [INFO] mandelbrot(-0.75, 0.1) max200 = %d (expect >30)", r3))
    check("mandelbrot(-750,100,200)>30", r3 > 30, true)

    -- 主心形内 (-0.5, 0) ≈ (-500, 0)
    local r4 = fn_mandel:call(-500, 0, 200)
    check("mandelbrot(-500,0,200)=200", r4, 200)
else
    print("  [FAIL] mandelbrot not found")
    failed = failed + 1
end
print("")

-- ==========================================================
-- 测试 3: Collatz 猜想步数
-- ==========================================================
print("=== 测试 3: Collatz 猜想 ===")
local fn_col = get_export_func(instance, "collatz_steps")
if fn_col then
    -- collatz(1)=0, collatz(6)=8, collatz(27)=111
    local cases = {
        {1, 0,    "collatz(1)"},
        {6, 8,    "collatz(6)"},
        {27, 111, "collatz(27)"},
        {837799, 524, "collatz(837799)"},
        {1000000, 152, "collatz(1000000)"},
    }
    for _, c in ipairs(cases) do
        local r = fn_col:call(c[1])
        check(c[3], r, c[2])
    end
else
    print("  [FAIL] collatz_steps not found")
    failed = failed + 1
end
print("")

-- ==========================================================
-- 测试 4: 牛顿法求平方根
-- ==========================================================
print("=== 测试 4: 牛顿法 sqrt ===")
local fn_sqrt = get_export_func(instance, "sqrt_newton")
if fn_sqrt then
    -- sqrt(2)*10^6 ≈ 1414213, sqrt(100)*10^6 = 10000000, sqrt(0)=0
    local r1 = fn_sqrt:call(2, 10)
    check_near("sqrt(2)", r1, 1414213, 100)

    local r2 = fn_sqrt:call(100, 10)
    check_near("sqrt(100)", r2, 10000000, 100)

    local r3 = fn_sqrt:call(0, 5)
    check("sqrt(0)", r3, 0)

    local r4 = fn_sqrt:call(50, 15)
    -- sqrt(50)*10^6 ≈ 7071067
    check_near("sqrt(50)", r4, 7071067, 200)

    -- 大数: sqrt(999999)
    local r5 = fn_sqrt:call(999999, 15)
    -- sqrt(999999) ≈ 999.9995, *10^6 ≈ 999999500
    check_near("sqrt(999999)", r5, 999999500, 1000)
else
    print("  [FAIL] sqrt_newton not found")
    failed = failed + 1
end
print("")

-- ==========================================================
-- 测试 5: 2x2 矩阵行列式
-- ==========================================================
print("=== 测试 5: 2x2 行列式 ===")
local fn_det = get_export_func(instance, "det2x2")
if fn_det then
    -- 注意: wasmtime 会根据 Lua 值类型选择 WASM 类型
    -- Lua integer → I32/I64, Lua float → F64
    -- det2x2 期望 F64, 所以参数必须用浮点数 (加 .0)
    local cases = {
        {1.0, 0.0, 0.0, 1.0, 1,   "det(I)"},
        {2.0, 3.0, 4.0, 5.0, -2,  "det([2,3;4,5])"},
        {0.0, 0.0, 0.0, 0.0, 0,   "det(zero)"},
        {7.0, 2.0, 1.0, 8.0, 54,  "det([7,2;1,8])"},
        {-1.0, 2.0, -3.0, 4.0, 2, "det([-1,2;-3,4])"},
    }
    for _, c in ipairs(cases) do
        local r = fn_det:call(c[1], c[2], c[3], c[4])
        check(c[6], r, c[5])
    end
else
    print("  [FAIL] det2x2 not found")
    failed = failed + 1
end
print("")

-- ==========================================================
-- 测试 6: 多项式求值 (Horner)
-- ==========================================================
print("=== 测试 6: 多项式求值 ===")
local fn_poly = get_export_func(instance, "poly_eval")
if fn_poly then
    -- f(x) = 1 + 2x + 3x^2 + 4x^3 + 5x^4
    -- f(0)=1, f(1)=1+2+3+4+5=15, f(2)=1+4+12+32+80=129
    local r1 = fn_poly:call(1.0, 2.0, 3.0, 4.0, 5.0, 0.0)
    check("poly(0)", r1, 1)

    local r2 = fn_poly:call(1.0, 2.0, 3.0, 4.0, 5.0, 1.0)
    check("poly(1)", r2, 15)

    local r3 = fn_poly:call(1.0, 2.0, 3.0, 4.0, 5.0, 2.0)
    check("poly(2)", r3, 129)

    -- f(x) = 10 - 3x + 0x^2 + 0x^3 + 0x^4 = 10 - 3x
    local r4 = fn_poly:call(10.0, -3.0, 0.0, 0.0, 0.0, 7.0)
    check("poly(10-3*7)", r4, -11)

    -- 浮点精度: f(x)=x^4, f(2.5)=39.0625
    local r5 = fn_poly:call(0.0, 0.0, 0.0, 0.0, 1.0, 2.5)
    check_near("x^4 at 2.5", r5, 39.0625, 0.001)
else
    print("  [FAIL] poly_eval not found")
    failed = failed + 1
end
print("")

-- ==========================================================
-- 测试 7: 区间求和 (Gauss)
-- ==========================================================
print("=== 测试 7: 区间求和 ===")
local fn_sum = get_export_func(instance, "sum_range")
if fn_sum then
    local cases = {
        {1, 10, 55,        "sum(1,10)"},
        {1, 100, 5050,     "sum(1,100)"},
        {5, 5, 5,          "sum(5,5)"},
        {10, 1, 0,         "sum(10,1) reversed"},
        {0, 0, 0,          "sum(0,0)"},
    }
    for _, c in ipairs(cases) do
        local r = fn_sum:call(c[1], c[2])
        check(c[4], r, c[3])
    end
else
    print("  [FAIL] sum_range not found")
    failed = failed + 1
end
print("")

-- ==========================================================
-- 测试 8: Hamming weight / popcount
-- ==========================================================
print("=== 测试 8: Hamming weight (32-bit) ===")
local fn_hw = get_export_func(instance, "hamming_weight")
if fn_hw then
    -- 0x00000000=0, 0xFFFFFFFF=32, 0xAAAAAAAA=16, 0x0000000F=4
    local cases = {
        {0,           0,  "hw(0)"},
        {0xFFFFFFFF, 32,  "hw(0xFFFFFFFF)"},
        {0xAAAAAAAA, 16,  "hw(0xAAAAAAAA)"},
        {0x0000000F, 4,   "hw(0xF)"},
        {0x12345678, 13,  "hw(0x12345678)"},
        {0x80000000, 1,   "hw(0x80000000)"},
    }
    for _, c in ipairs(cases) do
        -- wasmtime 的 i32 参数通过 Lua integer 传入, 需要处理符号
        local arg = c[1]
        if arg > 0x7FFFFFFF then
            -- 负数补码处理: Lua integer 不能表示 >2^31-1 的 unsigned
            -- wasmtime 使用 lua_to_wasmtime_val, 对 LUA_TNUMBER 判断是否在 I32 范围
            -- 如果超出范围会用 I64 传递, 但 WASM 函数签名是 I32...
            -- 让 wasmtime 决定类型 —— 在 2^31-1 内的作为 I32 传
            -- 对于 0xFFFFFFFF, 用 I64 传, 然后在 WASM 内截断
            print(string.format("  [SKIP] %s: 0x%X (>INT32_MAX, I64→I32 截断测试跳过)", c[3], c[1]))
            -- 跳过, wasmtime 这边 I32 参数无法准确传入 >INT32_MAX 的值
        else
            local r = fn_hw:call(arg)
            check(c[3], r, c[2])
        end
    end

    -- 补充: 在 I32 范围内的高位测试
    local r_high = fn_hw:call(0x40000000)  -- bit30, hw=1
    check("hw(0x40000000)", r_high, 1)
else
    print("  [FAIL] hamming_weight not found")
    failed = failed + 1
end
print("")

-- ==========================================================
-- 测试 9: Popcount 64-bit (参数必须是 I64, 即值 > INT32_MAX 或 < INT32_MIN)
--   wasmtime 的 lua_to_wasmtime_val 对 INT32 范围内的整数传 I32,
--   只有超出 INT32 范围的整数才传 I64, 所以这里只测大值
-- ==========================================================
print("=== 测试 9: Popcount 64-bit ===")
local fn_pop64 = get_export_func(instance, "popcount")
if fn_pop64 then
    -- 这些值 > INT32_MAX 或 < INT32_MIN, 会被传为 I64
    local cases = {
        {0x0101010101010101,      8,  "pop64(0x0101...)"},
        {0x7FFF000000000000,     15,  "pop64(0x7FFF...)"},
        {0x0001000100010001,      4,  "pop64(稀疏4bit)"},
        {0x8000000000000000,      1,  "pop64(hi_bit)"},   -- ≡ -9223372036854775808
    }
    for _, c in ipairs(cases) do
        local r = fn_pop64:call(c[1])
        check(c[3], r, c[2])
    end
else
    print("  [FAIL] popcount not found")
    failed = failed + 1
end
print("")

-- ==========================================================
-- 测试 10: 位反转 (reverse_bits)
-- ==========================================================
print("=== 测试 10: 位反转 32-bit ===")
local fn_rev = get_export_func(instance, "reverse_bits")
if fn_rev then
    local cases = {
        {0,          0,          "rev(0)"},
        {1,          0x80000000, "rev(1)"},
        {0x80000000, 1,          "rev(0x80000000)"},
        {0x55555555, 0xAAAAAAAA,"rev(0x55555555)"},
        {0x0000FFFF, 0xFFFF0000, "rev(0x0000FFFF)"},
    }
    for _, c in ipairs(cases) do
        local arg = c[1]
        if arg > 0x7FFFFFFF then
            print(string.format("  [SKIP] %s: 0x%X (I32 范围限制)", c[3], arg))
        else
            local r = fn_rev:call(arg)
            -- reverse_bits 返回 unsigned, wasmtime 可能返回负数补码(Lua integer)
            -- 做位掩码确保比较正确
            local r_masked = r & 0xFFFFFFFF
            local expected = c[2]
            if r_masked == expected then
                check(c[3], r_masked, expected)
            else
                print(string.format("  [FAIL] %s: got 0x%X, expected 0x%X", c[3], r, expected))
                failed = failed + 1
            end
        end
    end
else
    print("  [FAIL] reverse_bits not found")
    failed = failed + 1
end

print("")
print("========================")
print(string.format("结果: %d passed, %d failed, %d total", passed, failed, passed + failed))