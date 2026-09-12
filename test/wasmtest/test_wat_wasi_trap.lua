-- ============================================================
-- 测试: WAT→WASM / Trap 诊断 / WASI（P0+P1 扩展）
-- ============================================================
local wasmtime = require("wasmtime")
local passed, failed = 0, 0
local function check(name, cond, extra)
    if cond then passed = passed + 1
        print(string.format("  [OK] %s", name))
    else
        failed = failed + 1
        print(string.format("  [FAIL] %s  %s", name, extra or ""))
    end
end

print("=== 1. WAT→WASM ===")
local add_wat = [[
(module
  (func (export "add") (param i32 i32) (result i32)
    local.get 0
    local.get 1
    i32.add)
  (func (export "mul") (param i32 i32) (result i32)
    local.get 0
    local.get 1
    i32.mul))
]]

local wasm = wasmtime.wat2wasm(add_wat)
check("wat2wasm 返回二进制", type(wasm) == "string" and #wasm > 0, tostring(#wasm))
if type(wasm) == "string" then
    local bad = wasmtime.wat2wasm("(module (func")  -- 非法 WAT
    check("wat2wasm 非法输入返回 nil,err", bad == nil, tostring(bad))
end

-- 用编译出的 wasm 执行
do
    local e = wasmtime.newEngine()
    local s = wasmtime.newStore(e)
    local m = wasmtime.newModule(e, wasm)
    local inst = wasmtime.newInstance(s, m)
    local f = inst:getExport("add")
    local r = f:call(20, 22)
    check("WAT→执行 add(20,22)=42", r == 42, tostring(r))
    local m2 = inst:getExport("mul")
    check("WAT→执行 mul(7,8)=56", m2:call(7, 8) == 56)
end

print("=== 2. Trap 诊断 ===")
-- 除零 trap
local div_wat = [[
(module
  (func (export "div") (param i32 i32) (result i32)
    local.get 0
    local.get 1
    i32.div_s))
]]
local divwasm = wasmtime.wat2wasm(div_wat)
do
    local e = wasmtime.newEngine()
    local s = wasmtime.newStore(e)
    local m = wasmtime.newModule(e, divwasm)
    local inst = wasmtime.newInstance(s, m)
    local f = inst:getExport("div")
    -- func:call 除零时返回 nil, trap, message（trap 对象）
    local res, trap = f:call(10, 0)
    check("除零返回 trap 对象", res == nil and type(trap) == "userdata", tostring(trap))
    if type(trap) == "userdata" then
        local code, num = trap:code()
        check("trap:code() = integer_division_by_zero", code == "integer_division_by_zero", tostring(code))
        check("trap:code() 数值", num == 7, tostring(num))
        local mmsg = trap:message()
        check("trap:message() 非空", type(mmsg) == "string" and #mmsg > 0, tostring(mmsg))
        print(string.format("  [INFO] trap msg: %s", mmsg:sub(1, 60)))
    end
end

-- 手动创建 trap
do
    local t = wasmtime.newTrap("custom error")
    check("newTrap 创建", type(t) == "userdata")
    if type(t) == "userdata" then
        check("newTrap:message()", t:message() == "custom error", t:message())
        local code, num = t:code()
        check("宿主 trap 无 code", code == nil and num == nil, tostring(code))
    end
    local tc = wasmtime.newTrapCode("out_of_fuel")
    check("newTrapCode(out_of_fuel)", type(tc) == "userdata")
    if type(tc) == "userdata" then
        local code, num = tc:code()
        check("newTrapCode:code()", code == "out_of_fuel" and num == 11, tostring(code)..tostring(num))
    end
end

print("=== 3. WASI ===")
-- preview1 风格的 fd_write 模块（仅用于验证 import 解析）
local wasi_wat = [[
(module
  (import "wasi_snapshot_preview1" "fd_write"
    (func $fd_write (param i32 i32 i32 i32) (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 8) "Hello from WASI!\n")
  (func (export "_start")
    i32.const 1
    i32.const 8
    i32.const 1
    i32.const 0
    call $fd_write
    drop))
]]
-- WASI 绑定 API：newWasi / store:setWasi / linker:defineWasi
-- 注：wasmtime v48 为 preview2 架构，preview1 模块的标准流/文件操作
--     （fd_write 等）返回 ENOSYS(48)；纯计算接口（random_get 等）正常。
do
    local e = wasmtime.newEngine()
    local s = wasmtime.newStore(e)
    local wasi = wasmtime.newWasi{
        argv = { "prog", "arg1" },
        env = { "FOO=bar" },
        inheritStdout = true,
        preopenDirs = { { host = ".", guest = "." } }
    }
    check("newWasi 创建", type(wasi) == "userdata")
    if type(wasi) == "userdata" then
        local s2 = s:setWasi(wasi)
        check("store:setWasi 返回 store", s2 == s)
        local linker = wasmtime.newLinker(e)
        local dw, derr = linker:defineWasi()
        check("linker:defineWasi", dw == linker, tostring(derr))

        -- preview1 模块实例化（import 被 defineWasi 满足）
        local okmod = wasmtime.wat2wasm(wasi_wat)
        local inst, ierr = linker:instantiate(s, wasmtime.newModule(e, okmod))
        check("preview1 模块实例化", inst ~= nil, tostring(ierr))

        -- preview1 random_get：验证适配层真实可用（非 stub）
        local rng_wat = [[
(module
  (import "wasi_snapshot_preview1" "random_get"
    (func $random_get (param i32 i32) (result i32)))
  (memory (export "memory") 1)
  (func (export "fill")
    i32.const 300
    i32.const 0
    i32.const 8
    call $random_get
    i32.store)
  (func (export "get_errno") (result i32)
    i32.const 300
    i32.load))
]]
        local rmod = wasmtime.newModule(e, wasmtime.wat2wasm(rng_wat))
        local rinst = linker:instantiate(s, rmod)
        check("random_get 模块实例化", rinst ~= nil, tostring(rinst))
        if rinst then
            rinst:getExport("fill"):call()
            local rerr = rinst:getExport("get_errno"):call()
            check("preview1 random_get 成功(errno=0)", rerr == 0, tostring(rerr))
        end
    end
end

print("")
print(string.format("========================\n结果: %d passed, %d failed", passed, failed))
os.exit(failed == 0 and 0 or 1)
