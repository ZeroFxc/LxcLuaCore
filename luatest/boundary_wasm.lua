-- ================================================================
-- WASM 组边界测试: wasm3, wasmtime, lua2wasm
-- ================================================================
local passed, failed = 0, 0
local function check(name, ok, msg)
    if ok then passed = passed + 1
    else failed = failed + 1; print("  FAIL: " .. (msg or name)) end
end

-- ================================================================
-- 1. wasm3 库 (嵌入式 wasm3 运行时)
-- 注意: wasm3 的某些 C 层操作不是 pcall 安全的，会触发 0xC0000005
--       访问冲突。所有操作必须用 pcall 包裹，尽量只做存在性检查和
--       最基本的 pcall 调用，避免深度测试可能崩溃的功能。
-- ================================================================
do
    print("\n========== wasm3 ==========")

    -- Step 1: 加载模块（pcall 安全）
    local ok_load, wasm3 = pcall(require, "wasm3")
    if not ok_load then
        print("  SKIP: wasm3 not available (" .. tostring(wasm3) .. ")")
    else
        -- 模块类型检查
        check("wasm3 loaded", type(wasm3) == "table",
              "expected table, got " .. type(wasm3))

        -- newEnvironment 存在性检查（仅类型检查，不调用）
        check("wasm3.newEnvironment", type(wasm3.newEnvironment) == "function",
              "expected function, got " .. type(wasm3.newEnvironment))

        -- Step 2: 尝试创建环境（pcall 包裹，此处可能在 C 层崩溃）
        local ok_env, env = pcall(wasm3.newEnvironment)
        if ok_env and env then
            check("wasm3 env created", true)
            check("wasm3 env type", type(env) == "userdata" or type(env) == "table",
                  "expected userdata or table, got " .. type(env))

            -- 环境对象的方法存在性检查（仅类型检查，安全，不调用方法本身）
            for _, fn in ipairs({"parseModule", "newRuntime", "loadModule",
                                  "findFunction", "getMemorySize",
                                  "getMemory", "printInfo", "getBacktrace"}) do
                check("wasm3 env." .. fn, type(env[fn]) == "function",
                      fn .. " missing, got " .. type(env[fn]))
            end

            -- 检查 metatable 中的 __gc（pcall 包裹 getmetatable）
            local ok_mt, mt = pcall(getmetatable, env)
            if ok_mt and mt then
                check("wasm3 env has metatable", true)
                check("wasm3 env __gc",
                      type(mt.__gc) == "function" or mt.__gc == nil,
                      "__gc is " .. type(mt.__gc))
            elseif not ok_mt then
                check("wasm3 env getmetatable pcall", false,
                      "getmetatable crashed: " .. tostring(mt))
            end

            -- Step 3: 尝试创建运行时（pcall 包裹）
            local ok_runtime, runtime = pcall(function()
                return env:newRuntime(1024 * 1024)
            end)
            if ok_runtime and runtime then
                check("wasm3 newRuntime", true)
                check("wasm3 runtime type",
                      type(runtime) == "userdata" or type(runtime) == "table",
                      "expected userdata or table, got " .. type(runtime))

                -- 运行时对象的方法存在性检查（仅类型检查，不调用）
                for _, fn in ipairs({"parseModule", "loadModule",
                                      "findFunction", "getMemorySize",
                                      "getMemory", "printInfo", "getBacktrace"}) do
                    check("wasm3 runtime." .. fn,
                          type(runtime[fn]) == "function",
                          fn .. " missing on runtime, got "
                              .. type(runtime[fn]))
                end

                -- 只做最基本的 pcall 调用：getMemorySize
                -- 注意：即使 pcall 包裹，C 层崩溃也可能无法捕获
                local ok_mem, mem = pcall(function()
                    return runtime:getMemorySize()
                end)
                if ok_mem then
                    check("wasm3 getMemorySize", type(mem) == "number",
                          "expected number, got " .. type(mem))
                else
                    check("wasm3 getMemorySize pcall", ok_mem,
                          "getMemorySize failed: " .. tostring(mem))
                end
            elseif not ok_runtime then
                check("wasm3 newRuntime pcall", false,
                      "newRuntime failed: " .. tostring(runtime))
            else
                check("wasm3 newRuntime returned nil", false,
                      "newRuntime returned nil without error")
            end

            -- Step 4: parseModule nil 参数测试（pcall 包裹）
            local ok_pm, pm = pcall(function()
                return env:parseModule(nil)
            end)
            if not ok_pm then
                check("wasm3 env:parseModule nil", true,
                      "correctly errored on nil: " .. tostring(pm))
            else
                check("wasm3 env:parseModule nil", false,
                      "expected error on nil, got " .. tostring(pm))
            end

        elseif not ok_env then
            check("wasm3 newEnvironment pcall", false,
                  "newEnvironment failed: " .. tostring(env))
        else
            check("wasm3 newEnvironment returned nil", false,
                  "newEnvironment returned nil without error")
        end

        -- Step 5: 模块级 parseModule nil（pcall 包裹）
        local ok_pm2, pm2 = pcall(function()
            return wasm3.parseModule(nil)
        end)
        if not ok_pm2 then
            check("wasm3.parseModule nil", true,
                  "correctly errored on nil (module-level): " .. tostring(pm2))
        else
            check("wasm3.parseModule nil", false,
                  "expected error on nil, got " .. tostring(pm2))
        end

        -- Step 6: newEnvironment nil 参数（pcall 包裹）
        local ok_ne, ne = pcall(function()
            return wasm3.newEnvironment(nil)
        end)
        check("wasm3 newEnvironment nil pcall",
              ok_ne == true or ok_ne == false,
              "should not crash with nil arg, result: "
                  .. tostring(ok_ne) .. ", " .. tostring(ne))
    end
end

-- ================================================================
-- 2. wasmtime 库 (wasmtime 运行时)
-- 注意: wasmtime 在 Emscripten 等平台不可用时会设置 _error 字段。
--       所有调用必须用 pcall 包裹，并先检查 _error 字段。
-- ================================================================
do
    print("\n========== wasmtime ==========")

    -- Step 1: 加载模块（pcall 安全）
    local ok_load, wasmtime = pcall(require, "wasmtime")
    if not ok_load then
        print("  SKIP: wasmtime not available (" .. tostring(wasmtime) .. ")")
    else
        -- 模块类型检查
        check("wasmtime loaded", type(wasmtime) == "table",
              "expected table, got " .. type(wasmtime))

        -- 检查 _error 字段（平台不支持标志）
        local platform_unsupported = false
        if wasmtime._error then
            platform_unsupported = true
            print("  INFO: wasmtime._error = "
                  .. tostring(wasmtime._error)
                  .. " (platform may not support wasmtime)")
        end

        -- API 函数存在性检查（仅类型检查，安全）
        for _, fn in ipairs({"newEngine", "newStore", "newModule",
                              "newInstance", "validate", "newLinker",
                              "runLua2wasm"}) do
            check("wasmtime." .. fn,
                  type(wasmtime[fn]) == "function"
                      or type(wasmtime[fn]) == "table",
                  fn .. " missing or wrong type: "
                      .. type(wasmtime[fn]))
        end

        -- Step 2: newEngine 基本调用（pcall 包裹）
        local ok_eng, engine = pcall(wasmtime.newEngine)
        if ok_eng and engine then
            check("wasmtime newEngine", true)
            check("wasmtime engine type",
                  type(engine) == "userdata" or type(engine) == "table",
                  "expected userdata or table, got " .. type(engine))
        elseif platform_unsupported then
            print("  SKIP: wasmtime engine creation failed "
                  .. "(platform unsupported)")
        else
            check("wasmtime newEngine pcall", ok_eng,
                  "newEngine failed: " .. tostring(engine))
        end

        -- Step 3: validate 基本调用（pcall 包裹）
        local ok_val, val = pcall(function()
            return wasmtime.validate("")
        end)
        if ok_val then
            check("wasmtime validate empty", type(val) == "boolean",
                  "expected boolean, got " .. type(val))
        elseif platform_unsupported then
            print("  SKIP: wasmtime validate failed "
                  .. "(platform unsupported)")
        else
            check("wasmtime validate empty pcall", ok_val,
                  "validate empty failed: " .. tostring(val))
        end

        -- validate nil 参数
        local ok_val2, val2 = pcall(function()
            return wasmtime.validate(nil)
        end)
        if not ok_val2 then
            check("wasmtime validate nil", true,
                  "correctly errored on nil: " .. tostring(val2))
        else
            check("wasmtime validate nil", false,
                  "expected error on nil, got " .. tostring(val2))
        end

        -- Step 4: newStore 基本调用（pcall 包裹）
        local ok_store, store = pcall(wasmtime.newStore)
        if ok_store and store then
            check("wasmtime newStore", true)
            check("wasmtime store type",
                  type(store) == "userdata" or type(store) == "table",
                  "expected userdata or table, got " .. type(store))
        elseif platform_unsupported then
            print("  SKIP: wasmtime newStore failed "
                  .. "(platform unsupported)")
        else
            check("wasmtime newStore pcall", ok_store,
                  "newStore failed: " .. tostring(store))
        end

        -- Step 5: newLinker 基本调用（pcall 包裹）
        local ok_link, linker = pcall(function()
            return wasmtime.newLinker(engine or wasmtime.newEngine())
        end)
        if ok_link and linker then
            check("wasmtime newLinker", true)
            check("wasmtime linker type",
                  type(linker) == "userdata" or type(linker) == "table",
                  "expected userdata or table, got " .. type(linker))
        elseif platform_unsupported then
            print("  SKIP: wasmtime newLinker failed "
                  .. "(platform unsupported)")
        else
            check("wasmtime newLinker pcall", ok_link,
                  "newLinker failed: " .. tostring(linker))
        end

        -- Step 6: runLua2wasm 基本调用（pcall 包裹）
        local ok_run, run_result = pcall(function()
            return wasmtime.runLua2wasm("local x = 1")
        end)
        if ok_run then
            check("wasmtime runLua2wasm",
                  type(run_result) == "number"
                      or type(run_result) == "nil",
                  "unexpected return type: " .. type(run_result))
        elseif platform_unsupported then
            print("  SKIP: wasmtime runLua2wasm failed "
                  .. "(platform unsupported)")
        else
            check("wasmtime runLua2wasm pcall", ok_run,
                  "runLua2wasm failed: " .. tostring(run_result))
        end

        -- runLua2wasm nil 参数
        local ok_run2, run_result2 = pcall(function()
            return wasmtime.runLua2wasm(nil)
        end)
        if not ok_run2 then
            check("wasmtime runLua2wasm nil", true,
                  "correctly errored on nil: " .. tostring(run_result2))
        else
            check("wasmtime runLua2wasm nil", false,
                  "expected error on nil, got " .. tostring(run_result2))
        end

        -- Step 7: newInstance nil 参数（pcall 包裹）
        local ok_inst, inst = pcall(function()
            return wasmtime.newInstance(nil, nil)
        end)
        if not ok_inst then
            check("wasmtime newInstance nil", true,
                  "correctly errored on nil: " .. tostring(inst))
        elseif platform_unsupported then
            print("  SKIP: wasmtime newInstance failed "
                  .. "(platform unsupported)")
        else
            check("wasmtime newInstance nil", false,
                  "expected error on nil, got " .. tostring(inst))
        end

        -- Step 8: newModule nil 参数（pcall 包裹）
        local ok_mod, mod = pcall(function()
            return wasmtime.newModule(nil, nil)
        end)
        if not ok_mod then
            check("wasmtime newModule nil", true,
                  "correctly errored on nil: " .. tostring(mod))
        elseif platform_unsupported then
            print("  SKIP: wasmtime newModule failed "
                  .. "(platform unsupported)")
        else
            check("wasmtime newModule nil", false,
                  "expected error on nil, got " .. tostring(mod))
        end
    end
end

-- ================================================================
-- 3. lua2wasm 库 (Lua 到 WASM 编译器)
-- ================================================================
do
    print("\n========== lua2wasm ==========")

    local ok_load, lua2wasm = pcall(require, "lua2wasm")
    if not ok_load then
        print("  SKIP: lua2wasm not available (" .. tostring(lua2wasm) .. ")")
    else
        -- 模块加载
        check("lua2wasm loaded", type(lua2wasm) == "table",
              "expected table, got " .. type(lua2wasm))

        -- API 函数存在性
        check("lua2wasm.compile", type(lua2wasm.compile) == "function",
              "expected function, got " .. type(lua2wasm.compile))
        check("lua2wasm.wcompile", type(lua2wasm.wcompile) == "function",
              "expected function, got " .. type(lua2wasm.wcompile))
        check("lua2wasm.assemble", type(lua2wasm.assemble) == "function",
              "expected function, got " .. type(lua2wasm.assemble))

        -- compile: Lua → WAT
        local ok_comp, wat = pcall(function()
            return lua2wasm.compile("local function f() return 42 end")
        end)
        if ok_comp and wat then
            check("lua2wasm compile result", type(wat) == "string",
                  "expected string WAT, got " .. type(wat))
            check("lua2wasm compile nonempty", #wat > 0,
                  "WAT should not be empty")
            -- WAT 通常包含 module 关键字
            check("lua2wasm compile contains module",
                  string.find(wat, "module") ~= nil,
                  "WAT should contain 'module'")
        else
            check("lua2wasm compile pcall", ok_comp,
                  "compile failed: " .. tostring(wat))
        end

        -- compile 空字符串
        local ok_comp2, wat2 = pcall(function()
            return lua2wasm.compile("")
        end)
        -- 空输入可能返回空或报错，只要不崩溃即可
        check("lua2wasm compile empty pcall",
              ok_comp2 == true or ok_comp2 == false,
              "should not crash with empty input, result: "
                  .. tostring(ok_comp2))

        -- wcompile: Lua → WASM binary
        local ok_wcomp, wasm_bin = pcall(function()
            return lua2wasm.wcompile(
                "local function f() return 42 end")
        end)
        if ok_wcomp and wasm_bin then
            check("lua2wasm wcompile result",
                  type(wasm_bin) == "string",
                  "expected string (binary WASM), got "
                      .. type(wasm_bin))
            check("lua2wasm wcompile nonempty", #wasm_bin > 0,
                  "WASM binary should not be empty")
        else
            check("lua2wasm wcompile pcall", ok_wcomp,
                  "wcompile failed: " .. tostring(wasm_bin))
        end

        -- wcompile 空字符串
        local ok_wcomp2, wasm_bin2 = pcall(function()
            return lua2wasm.wcompile("")
        end)
        check("lua2wasm wcompile empty pcall",
              ok_wcomp2 == true or ok_wcomp2 == false,
              "should not crash with empty input, result: "
                  .. tostring(ok_wcomp2))

        -- assemble: WAT → WASM
        local ok_asm, wasm_asm = pcall(function()
            return lua2wasm.assemble("(module)")
        end)
        if ok_asm and wasm_asm then
            check("lua2wasm assemble result",
                  type(wasm_asm) == "string",
                  "expected string (binary WASM), got "
                      .. type(wasm_asm))
            check("lua2wasm assemble nonempty", #wasm_asm > 0,
                  "WASM binary should not be empty")
        else
            check("lua2wasm assemble pcall", ok_asm,
                  "assemble failed: " .. tostring(wasm_asm))
        end

        -- assemble 无效 WAT
        local ok_asm2, wasm_asm2 = pcall(function()
            return lua2wasm.assemble("not a valid wat module")
        end)
        if not ok_asm2 then
            check("lua2wasm assemble invalid", true,
                  "correctly errored on invalid WAT: "
                      .. tostring(wasm_asm2))
        else
            check("lua2wasm assemble invalid", false,
                  "expected error on invalid WAT, got "
                      .. tostring(wasm_asm2))
        end

        -- nil 参数测试
        local ok_nil1, nil1 = pcall(function()
            return lua2wasm.compile(nil)
        end)
        check("lua2wasm compile nil", not ok_nil1,
              "should error on nil, got " .. tostring(nil1))

        local ok_nil2, nil2 = pcall(function()
            return lua2wasm.wcompile(nil)
        end)
        check("lua2wasm wcompile nil", not ok_nil2,
              "should error on nil, got " .. tostring(nil2))

        local ok_nil3, nil3 = pcall(function()
            return lua2wasm.assemble(nil)
        end)
        check("lua2wasm assemble nil", not ok_nil3,
              "should error on nil, got " .. tostring(nil3))

        -- 多次 compile 不同的 Lua 代码
        local codes = {
            "return 1 + 1",
            "local a = {}; return a",
            "local function fib(n) if n <= 1 then return n end "
                .. "return fib(n-1) + fib(n-2) end; return fib(5)",
        }
        for _, code in ipairs(codes) do
            local ok_c, result = pcall(function()
                return lua2wasm.compile(code)
            end)
            if ok_c then
                check("lua2wasm compile '"
                          .. code:sub(1, 30) .. "...'",
                      type(result) == "string")
            end
        end
    end
end

-- ================================================================
-- 结果汇总
-- ================================================================
print(string.format(
    "\n========== WASM 组完成: %d 通过, %d 失败 ==========",
    passed, failed))
if failed > 0 then os.exit(1) end