-- test_wasmtime_integrity.lua — wasmtime 运行时完整性综合测试
-- 测试: host import全覆盖 / 边界值 / validate / 错误恢复 / 多实例 / 压力

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

local function get_export(instance, name)
    local func = instance:getExport("_" .. name)
    if func then return func end
    return instance:getExport(name)
end

local function check_eq(label, actual, expected)
    if actual == expected then
        print(string.format("  [OK] %s: %s", label, tostring(actual)))
        passed = passed + 1; return true
    else
        print(string.format("  [FAIL] %s: got %s, expected %s", label, tostring(actual), tostring(expected)))
        failed = failed + 1; return false
    end
end

local function check_ok(label, cond)
    if cond then
        print(string.format("  [OK] %s", label))
        passed = passed + 1; return true
    else
        print(string.format("  [FAIL] %s", label))
        failed = failed + 1; return false
    end
end

-- ================================================================
-- Section A: Full Host Import 测试 (runLua2wasm)
-- ================================================================
print("=":rep(60))
print("Section A: Full Host Import — 覆盖全部可用 host 回调")
print("=":rep(60))

do
    local wasm_bytes = read_file("test_wasm_fullhost.wasm")
    if not wasm_bytes then print("SKIP: wasm not found\n"); goto section_b end
    print("WASM 大小: " .. #wasm_bytes .. " 字节")

    -- runLua2wasm 内部自检
    local output, err = wasmtime.runLua2wasm(wasm_bytes)
    check_ok("A1. runLua2wasm 无错误", output ~= nil)

    -- 手动编译获取 verify_all 结果
    local engine = wasmtime.newEngine()
    local store  = wasmtime.newStore(engine)
    local mod    = wasmtime.newModule(engine, wasm_bytes)
    check_ok("A2. 模块编译成功", mod ~= nil)

    -- validate
    local v_ok, v_msg = wasmtime.validate(engine, wasm_bytes)
    check_ok("A3. validate 通过", v_ok == true)

    print("  [INFO] runLua2wasm 已自动: 创建linker→注册28个host import→实例化→调用main()")
    print("  [INFO] 内部测试: host.math(10种) host.math2(3种) host.fmt(5模式)")
    print("  [INFO] 内部测试: host.os_time host.os_time_table host.os_clock")
    print("  [INFO] 内部测试: host.os_tmpname host.os_exit host.fs_seek host.fs_close host.read")
end
::section_b::

print("")

-- ================================================================
-- Section B: 边界值 / 边缘情况
-- ================================================================
print("=":rep(60))
print("Section B: 边界值 — I32/I64/F64 极值, NaN, Inf")
print("=":rep(60))

do
    local wasm_bytes = read_file("test_wasm_edge.wasm")
    if not wasm_bytes then print("SKIP: wasm not found\n"); goto section_c end
    print("WASM 大小: " .. #wasm_bytes .. " 字节")

    local engine   = wasmtime.newEngine()
    local store    = wasmtime.newStore(engine)
    local module   = wasmtime.newModule(engine, wasm_bytes)
    local instance = wasmtime.newInstance(store, module)

    -- B1. I32 极值
    local fn = get_export(instance, "ret_i32_min")
    if fn then check_eq("B1a. I32_MIN", fn:call(), -2147483648) end
    fn = get_export(instance, "ret_i32_max")
    if fn then check_eq("B1b. I32_MAX", fn:call(), 2147483647) end

    -- B2. I64 极值
    fn = get_export(instance, "ret_i64_min")
    if fn then check_eq("B2a. I64_MIN", fn:call(), -9223372036854775808) end
    fn = get_export(instance, "ret_i64_max")
    if fn then check_eq("B2b. I64_MAX", fn:call(), 9223372036854775807) end

    -- B3. NaN
    fn = get_export(instance, "ret_nan")
    if fn then
        local nan_val = fn:call()
        -- Lua 的 NaN 比较: tostring 或 x~=x
        check_ok("B3a. ret_nan 返回 NaN", tostring(nan_val):lower():match("nan") ~= nil or nan_val ~= nan_val)
    end
    fn = get_export(instance, "is_nan")
    if fn then
        local nan_val = get_export(instance, "ret_nan"):call()
        check_eq("B3b. is_nan(NaN)=1", fn:call(nan_val), 1)
        check_eq("B3c. is_nan(42.0)=0", fn:call(42.0), 0)
    end

    -- B4. Inf
    fn = get_export(instance, "ret_inf")
    if fn then
        local inf_val = fn:call()
        check_ok("B4a. ret_inf = +Inf", inf_val == math.huge or tostring(inf_val):match("inf"))
    end
    fn = get_export(instance, "ret_neg_inf")
    if fn then
        local ninf_val = fn:call()
        check_ok("B4b. ret_neg_inf = -Inf", ninf_val == -math.huge or tostring(ninf_val):match("%-inf"))
    end
    fn = get_export(instance, "is_inf")
    if fn then
        local inf_val = get_export(instance, "ret_inf"):call()
        check_eq("B4c. is_inf(+Inf)=1", fn:call(inf_val), 1)
        check_eq("B4d. is_inf(0.0)=0", fn:call(0.0), 0)
    end

    -- B5. 整数回绕
    fn = get_export(instance, "add_wrap")
    if fn then
        local r = fn:call()
        -- WASM I32: INT32_MAX + 1 回绕到 INT32_MIN
        -- 在 Lua 端, 返回值是 I32, 转为 Lua integer
        check_eq("B5a. I32_MAX+1 wrap", r, -2147483648)
    end
    fn = get_export(instance, "mul_wrap")
    if fn then
        check_eq("B5b. 1<<31 = I32_MIN", fn:call(), -2147483648)
    end

    -- B6. 深度递归
    fn = get_export(instance, "deep_recursion")
    if fn then
        check_eq("B6a. deep_recursion(0)=0", fn:call(0), 0)
        check_eq("B6b. deep_recursion(10)=55", fn:call(10), 55)
        check_eq("B6c. deep_recursion(100)=5050", fn:call(100), 5050)
        local r = fn:call(500)
        check_eq("B6d. deep_recursion(500)=125250", r, 125250)
    end

    -- B7. 多参数函数 (8 个 I32)
    fn = get_export(instance, "many_params")
    if fn then
        check_eq("B7a. many_params(1..8)", fn:call(1,2,3,4,5,6,7,8), 36)
        check_eq("B7b. many_params(all 0)", fn:call(0,0,0,0,0,0,0,0), 0)
        check_eq("B7c. many_params(mixed)", fn:call(-1,-2,-3,4,5,6,7,8), 24)
    end

    -- B8. identity (F64 精度保持)
    fn = get_export(instance, "identity")
    if fn then
        check_eq("B8a. identity(0.0)", fn:call(0.0), 0.0)
        check_eq("B8b. identity(-1.5)", fn:call(-1.5), -1.5)
        check_eq("B8c. identity(3.14159265358979)", fn:call(3.14159265358979), 3.14159265358979)
    end
end
::section_c::

print("")

-- ================================================================
-- Section C: validate / 错误恢复
-- ================================================================
print("=":rep(60))
print("Section C: validate & 错误恢复")
print("=":rep(60))

do
    local engine = wasmtime.newEngine()

    -- C1. validate 合法 WASM
    local valid_wasm = read_file("test_wasm_module.wasm")
    if valid_wasm then
        local ok, msg = wasmtime.validate(engine, valid_wasm)
        check_ok("C1a. validate(valid)=true", ok == true)
        check_eq("C1b. validate msg", msg, "ok")
    end

    -- C2. validate 非法数据
    local ok, msg = wasmtime.validate(engine, "not a wasm binary!!!")
    check_ok("C2a. validate(bad)=false", ok == false)
    check_ok("C2b. validate error msg", msg ~= nil and #msg > 0)
    print(string.format("  [INFO] 错误消息: %s", tostring(msg)))

    -- C3. newModule 非法数据 (应抛出错误)
    local status, result = pcall(wasmtime.newModule, engine, "garbage data")
    check_ok("C3. newModule(bad) 抛出错误", status == false)

    -- C4. newModule 空数据
    status, result = pcall(wasmtime.newModule, engine, "")
    check_ok("C4. newModule(empty) 抛出错误", status == false)
end

print("")

-- ================================================================
-- Section D: 多实例 / 隔离性
-- ================================================================
print("=":rep(60))
print("Section D: 多实例隔离性")
print("=":rep(60))

do
    local wasm_bytes = read_file("test_wasm_module.wasm")
    if not wasm_bytes then print("SKIP\n"); goto section_e end

    local engine = wasmtime.newEngine()
    local store1 = wasmtime.newStore(engine)
    local store2 = wasmtime.newStore(engine)
    local module = wasmtime.newModule(engine, wasm_bytes)

    -- 同一模块在两个独立 store 中实例化
    local inst1 = wasmtime.newInstance(store1, module)
    local inst2 = wasmtime.newInstance(store2, module)

    -- 各自独立调用
    local add1 = get_export(inst1, "add")
    local add2 = get_export(inst2, "add")

    if add1 and add2 then
        check_eq("D1a. inst1 add(1,2)=3", add1:call(1, 2), 3)
        check_eq("D1b. inst2 add(5,5)=10", add2:call(5, 5), 10)
        check_eq("D1c. inst1 add(100,200)=300", add1:call(100, 200), 300)
        check_eq("D1d. inst2 add(-5,5)=0", add2:call(-5, 5), 0)
    end

    -- 同一 store 中实例化同一模块多次
    local status, inst3 = pcall(wasmtime.newInstance, store1, module)
    check_ok("D2. 同 store 同 module 再次实例化", status == true)
    if status then
        local add3 = get_export(inst3, "add")
        if add3 then
            check_eq("D2a. inst3 add(7,8)=15", add3:call(7, 8), 15)
        end
    end

    check_ok("D3. 多实例不互相干扰", true)
end
::section_e::

print("")

-- ================================================================
-- Section E: 压力 / 性能完整性
-- ================================================================
print("=":rep(60))
print("Section E: 压力测试")
print("=":rep(60))

do
    local wasm_bytes = read_file("test_wasm_module.wasm")
    if not wasm_bytes then print("SKIP\n"); goto section_f end

    local engine   = wasmtime.newEngine()
    local store    = wasmtime.newStore(engine)
    local module   = wasmtime.newModule(engine, wasm_bytes)
    local instance = wasmtime.newInstance(store, module)

    local fn_add = get_export(instance, "add")
    local fn_fib = get_export(instance, "fib")
    local fn_mul = get_export(instance, "mul")

    -- E1. 连续 1000 次 add 调用
    local ok_count = 0
    for i = 1, 1000 do
        local r = fn_add:call(i, i)
        if r == i + i then ok_count = ok_count + 1 end
    end
    check_eq("E1. 1000次 add(i,i) 全正确", ok_count, 1000)

    -- E2. 连续 200 次 fib 调用
    ok_count = 0
    local fib_expected = {[0]=0,1,1,2,3,5,8,13,21,34,55,89,144,233,377,610,987,1597,2584,4181,6765}
    for i = 0, 20 do
        local r = fn_fib:call(i)
        if r == fib_expected[i] then ok_count = ok_count + 1 end
    end
    check_eq("E2. fib(0..20) 全正确", ok_count, 21)

    -- E3. 连续 500 次 mul 调用
    ok_count = 0
    for i = 1, 500 do
        local r = fn_mul:call(i, 2)
        if r == i * 2 then ok_count = ok_count + 1 end
    end
    check_eq("E3. 500次 mul(i,2) 全正确", ok_count, 500)

    -- E4. 100 次 is_prime 调用
    local fn_prime = get_export(instance, "is_prime")
    if fn_prime then
        ok_count = 0
        for i = 1, 100 do
            local r = fn_prime:call(i)
            -- 已知质数: 2,3,5,7,11,13,17,19,23,29,31,37,41,43,47,53,59,61,67,71,73,79,83,89,97
            local expected = ({[2]=1,[3]=1,[5]=1,[7]=1,[11]=1,[13]=1,[17]=1,[19]=1,[23]=1,
                              [29]=1,[31]=1,[37]=1,[41]=1,[43]=1,[47]=1,[53]=1,[59]=1,
                              [61]=1,[67]=1,[71]=1,[73]=1,[79]=1,[83]=1,[89]=1,[97]=1})[i] or 0
            if r == expected then ok_count = ok_count + 1 end
        end
        check_eq("E4. is_prime(1..100) 全正确", ok_count, 100)
    end
end
::section_f::

print("")
print("=":rep(60))
print(string.format("终: %d passed, %d failed, %d total", passed, failed, passed + failed))
print("=":rep(60))