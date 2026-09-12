-- test_wasmtime_abe.lua — A: callWithFuel 超时保护 / B: trap:frames / E: component getType+callAsync
local wasmtime = require("wasmtime")

local passed, failed = 0, 0
local function check(name, cond, extra)
    if cond then
        passed = passed + 1
        print("  PASS " .. name)
    else
        failed = failed + 1
        print("  FAIL " .. name .. (extra and (" | " .. tostring(extra)) or ""))
    end
end

local function read_file(path)
    local f = io.open(path, "rb")
    if not f then return nil end
    local d = f:read("*all"); f:close()
    return d
end

local function get_export_func(instance, name)
    local f = instance:getExport("_"..name)
    if f then return f end
    return instance:getExport(name)
end

-- ==========================================================
-- A: callWithFuel 超时保护
-- ==========================================================
print("=== A: callWithFuel ===")
do
    local loop_wat = [[
(module
  (func (export "loop") (param i32) (result i32)
    (local i32)
    block
      loop
        local.get 0
        i32.const 1
        i32.add
        local.set 0
        br 0
      end
    end
    local.get 0))
]]
    local mod_bytes = wasmtime.wat2wasm(loop_wat)
    check("wat2wasm loop module", mod_bytes ~= nil)

    -- 需要 fuel=true 引擎（consume_fuel）
    local engine = wasmtime.newEngine({ fuel = true })
    local store = wasmtime.newStore(engine)
    local module = wasmtime.newModule(engine, mod_bytes)
    local instance = wasmtime.newInstance(store, module)
    local loopfn = get_export_func(instance, "loop")
    check("loop func exists", loopfn ~= nil)

    if loopfn then
        local a, b, c = loopfn:callWithFuel(5000, 0)
        check("callWithFuel trap on infinite loop", a == nil and b ~= nil, tostring(b))
        if b then
            local code = b:code()
            check("trap code OUT_OF_FUEL", code == 11 or code == "out_of_fuel", code)
            check("trap message non-empty", type(c) == "string" and #c > 0)
        end
        -- 燃料恢复后普通函数仍可用
        local add_wat = [[(module (func (export "add") (param i32 i32) (result i32) local.get 0 local.get 1 i32.add))]]
        local add_mod = wasmtime.newModule(engine, wasmtime.wat2wasm(add_wat))
        local add_inst = wasmtime.newInstance(store, add_mod)
        local addfn = get_export_func(add_inst, "add")
        check("fuel restored: add(2,3)=5", addfn and addfn:call(2, 3) == 5)
    end

    -- 未开 fuel 的引擎：set_fuel 静默降级（不限制），调用不崩溃
    local engine2 = wasmtime.newEngine()
    local store2 = wasmtime.newStore(engine2)
    local inst2 = wasmtime.newInstance(store2, wasmtime.newModule(engine2, mod_bytes))
    local loopfn2 = get_export_func(inst2, "loop")
    local ok2 = pcall(function() loopfn2:callWithFuel(1000, 0) end)
    check("callWithFuel without fuel: no crash", ok2)
end

-- ==========================================================
-- B: trap:frames()
-- ==========================================================
print("=== B: trap:frames ===")
do
    local div_wat = [[
(module
  (func (export "div") (param i32 i32) (result i32)
    local.get 0
    local.get 1
    i32.div_s))
]]
    local engine = wasmtime.newEngine()
    local store = wasmtime.newStore(engine)
    local module = wasmtime.newModule(engine, wasmtime.wat2wasm(div_wat))
    local instance = wasmtime.newInstance(store, module)
    local div = get_export_func(instance, "div")

    local a, b, c = div:call(1, 0)
    check("div-by-zero -> nil, trap", a == nil and b ~= nil)
    if b then
        local fr = b:frames()
        check("frames is table", type(fr) == "table")
        check("frames has >=1 entry", #fr >= 1, #fr)
        if #fr >= 1 then
            local f0 = fr[1]
            check("frame has funcIndex", type(f0.funcIndex) == "number", f0.funcIndex)
            check("frame has funcOffset", type(f0.funcOffset) == "number")
            check("frame has moduleOffset", type(f0.moduleOffset) == "number")
            -- wasmtime 符号名（需要 debug info 才有 func 名，offset 一定在）
            check("frame module field present or nil", f0.module == nil or type(f0.module) == "string")
        end
    end
end

-- ==========================================================
-- E: component getType + callAsync
-- ==========================================================
print("=== E: component getType / callAsync ===")
do
    local bytes = read_file("_rust_demo.wasm")
    if not bytes then
        check("need _rust_demo.wasm", false)
    else
        local engine = wasmtime.newEngine()
        local store = wasmtime.newStore(engine)
        local c = wasmtime.newComponent(engine, bytes)
        local linker = wasmtime.newComponentLinker(engine)
        local inst, ierr = linker:instantiate(store, c)
        check("component instantiate ok", inst ~= nil, ierr)

        if inst then
            -- getType
            local add = inst:getExport("add")
            check("getExport add ok", add ~= nil)
            if add then
                local params, result = add:getType()
                check("getType params = {u32,u32}", type(params) == "table" and params[1] == "u32" and params[2] == "u32", tostring(params and params[1]))
                check("getType result = u32", result == "u32", result)
            end
            local greet = inst:getExport("greet")
            if greet then
                local p2, r2 = greet:getType()
                check("greet param = string", p2 and p2[1] == "string")
                check("greet result = string", r2 == "string", r2)
            end

            -- callAsync（纯计算组件，同步完成）
            if add then
                local fu = add:callAsync(10, 5)
                check("callAsync returns component_future", fu ~= nil)
                if fu then
                    local v = fu:wait()
                    check("comp callAsync wait = 15", v == 15, v)
                end
                local fu2 = add:callAsync(100, 200)
                if fu2 then
                    check("comp callAsync done", fu2:done() == true)
                    local v2 = fu2:result()
                    check("comp callAsync result = 300", v2 == 300, v2)
                end
            end
            -- callAsync 带复杂类型（list 双向）
            local sum_list = inst:getExport("sum-list")
            if sum_list then
                local fu3 = sum_list:callAsync({1, 2, 3, 4})
                if fu3 then
                    local v3 = fu3:wait()
                    check("comp callAsync sum-list = 10", v3 == 10, v3)
                end
            end
        end
    end
end

print("")
print(string.format("结果: %d 通过, %d 失败", passed, failed))
if failed > 0 then os.exit(1) end
