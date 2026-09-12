-- test_wasmtime_async.lua — 验证 ④Linker 深化 / ⑤编译缓存 / ⑥Async
local wasmtime = require("wasmtime")

local passed = 0
local failed = 0
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
    if not f then return nil, "cannot open file: " .. path end
    local d = f:read("*all"); f:close()
    return d
end

local function get_export_func(instance, name)
    local f = instance:getExport("_"..name)
    if f then return f end
    return instance:getExport(name)
end

-- ==========================================================
-- ⑥ Async: func:callAsync / future
-- ==========================================================
print("=== ⑥ Async: func:callAsync ===")
do
    local wasm = read_file("test_wasm_module.wasm")
    assert(wasm, "need test_wasm_module.wasm")

    local engine = wasmtime.newEngine()
    local store = wasmtime.newStore(engine)
    local module = wasmtime.newModule(engine, wasm)
    local instance = wasmtime.newInstance(store, module)
    local add = get_export_func(instance, "add")
    check("async add func exists", add ~= nil)

    if add then
        local fu = add:callAsync(10, 20)
        check("callAsync returns future", fu ~= nil)
        if fu then
            check("future:done after callAsync (immediate)", fu:done() == true)
            check("future:poll first", fu:poll() == true)
            check("future:done after poll", fu:done() == true)
            local r = fu:result()
            check("future:result() = 30", r == 30, r)
        end

        -- await
        local fu2 = add:callAsync(100, 5)
        if fu2 then
            local r2 = fu2:wait()
            check("future:wait() = 105", r2 == 105, r2)
        end
    end

    -- trap 场景（除零）
    local div = get_export_func(instance, "div")
    if div then
        local fu3 = div:callAsync(1, 0)
        if fu3 then
            local a, b, c = fu3:wait()
            check("async div-by-zero -> nil,trap,msg", a == nil and b ~= nil and type(c) == "string", tostring(b))
        end
    end

    -- lpromise 集成：callAsyncP 返回 Promise + await
    local asyncio = require("asyncio")
    local async function run_await_test()
        local v = await(add:callAsyncP(30, 12))
        check("await(callAsyncP) = 42", v == 42, v)
        return v
    end
    local p = run_await_test()
    local v2 = p:await_sync()
    check("result promise await_sync = 42", v2 == 42, v2)
end

-- ==========================================================
-- ④ Linker 深化: allowShadowing
-- ==========================================================
print("=== ④ Linker: allowShadowing ===")
do
    local wasm = read_file("test_wasm_module.wasm")
    local engine = wasmtime.newEngine()
    local store = wasmtime.newStore(engine)
    local module = wasmtime.newModule(engine, wasm)
    local instance = wasmtime.newInstance(store, module)
    local add = get_export_func(instance, "add")

    -- 默认 allowShadowing=false：重复 defineFunc 应报错
    local linker = wasmtime.newLinker(engine)
    local ok1 = linker:defineFunc("m", "f", "i32,i32", "i32", function(a, b) return a + b end)
    local ok2 = pcall(function()
        linker:defineFunc("m", "f", "i32,i32", "i32", function(a, b) return a * b end)
    end)
    check("shadowing off: second define errors", ok1 ~= nil and not ok2)

    -- allowShadowing(true) 后允许覆盖
    local linker2 = wasmtime.newLinker(engine)
    local ok3 = linker2:allowShadowing(true)
    check("allowShadowing returns linker", ok3 ~= nil)
    linker2:defineFunc("m", "f", "i32,i32", "i32", function(a, b) return a + b end)
    local ok4 = pcall(function()
        linker2:defineFunc("m", "f", "i32,i32", "i32", function(a, b) return a * b end)
    end)
    check("shadowing on: second define ok", ok3 ~= nil and ok4)
end

-- ==========================================================
-- ④ Linker: define (extern: func/memory)
-- ==========================================================
print("=== ④ Linker: define(func/memory) ===")
do
    local wasm = read_file("test_wasm_module.wasm")
    local engine = wasmtime.newEngine()
    local store = wasmtime.newStore(engine)
    local module = wasmtime.newModule(engine, wasm)
    local instance = wasmtime.newInstance(store, module)
    local add = get_export_func(instance, "add")

    local linker = wasmtime.newLinker(engine)

    -- 用导出 func 作为 extern 定义（define 与 defineFunc 等价路径）
    local okf, ef = linker:define(store, "env", "add", add)
    check("linker:define func ok", okf ~= nil, ef)

    -- 定义一个 memory import
    local memory = store:newMemory(1, 10)
    local okm, em = linker:define(store, "env", "mem", memory)
    check("linker:define memory ok", okm ~= nil, em)

    -- 非法类型应报错
    local okb, eb = pcall(function() linker:define(store, "env", "x", {}) end)
    check("linker:define bad item -> error", not okb)
end

-- ==========================================================
-- ④ Linker: defineInstance（模块间依赖注入）
-- ==========================================================
print("=== ④ Linker: defineInstance ===")
do
    -- 用 wat2wasm 构造两个模块：
    --   A: 导出 double(x) = x*2
    --   B: import "a" "double" 并导出 run(x) = double(x)+1
    local watA = [[
(module
  (func (export "double") (param i32) (result i32)
    local.get 0
    i32.const 2
    i32.mul))
]]
    local watB = [[
(module
  (import "a" "double" (func $double (param i32) (result i32)))
  (func (export "run") (param i32) (result i32)
    local.get 0
    call $double
    i32.const 1
    i32.add))
]]
    local modA_bytes = wasmtime.wat2wasm(watA)
    local modB_bytes = wasmtime.wat2wasm(watB)
    check("wat2wasm A/B ok", modA_bytes ~= nil and modB_bytes ~= nil)

    local engine = wasmtime.newEngine()
    local store = wasmtime.newStore(engine)
    local modA = wasmtime.newModule(engine, modA_bytes)
    local modB = wasmtime.newModule(engine, modB_bytes)

    local instA = wasmtime.newInstance(store, modA)
    local double = get_export_func(instA, "double")
    check("A:double(5) = 10", double and double:call(5) == 10)

    local linker = wasmtime.newLinker(engine)
    local ok, err = linker:defineInstance(store, "a", instA)
    check("linker:defineInstance ok", ok ~= nil, err)

    local instB, ierr = linker:instantiate(store, modB)
    check("instantiate B with injected A ok", instB ~= nil, ierr)
    if instB then
        local run = get_export_func(instB, "run")
        check("B:run(5) = 11", run and run:call(5) == 11)
    end
end

-- ==========================================================
-- ⑤ 编译缓存
-- ==========================================================
print("=== ⑤ 编译缓存 newEngine{cache=...} ===")
do
    local abs_cache_dir = "E:/Soft/Proje/LXCLUA-NCore/lua/test/wasmtest/wmt_cache_test"
    local cache_toml = "[cache]\ndirectory = \"" .. abs_cache_dir .. "\"\n"
    local f = io.open("_cache_test.toml", "w")
    f:write(cache_toml); f:close()

    -- 创建 cache 目录
    os.execute("if not exist wmt_cache_test mkdir wmt_cache_test")

    local wasm = read_file("test_wasm_module.wasm")
    local ok, err = pcall(function()
        local engine = wasmtime.newEngine({ cache = "_cache_test.toml" })
        local store = wasmtime.newStore(engine)
        local module = wasmtime.newModule(engine, wasm)
        local instance = wasmtime.newInstance(store, module)
        local add = get_export_func(instance, "add")
        assert(add, "no add export")
        assert(add:call(3, 4) == 7, "cache engine add wrong")
        return true
    end)
    check("newEngine{cache=...} works", ok, err)

    -- 无效路径应报错
    local ok2, err2 = pcall(function()
        wasmtime.newEngine({ cache = "_no_such_cache_dir/nonexist.toml" })
    end)
    check("cache bad path -> error", not ok2, err2)

    os.execute("rd /s /q wmt_cache_test 2>nul")
    os.remove("_cache_test.toml")
end

print("")
print(string.format("结果: %d 通过, %d 失败", passed, failed))
if failed > 0 then os.exit(1) end
