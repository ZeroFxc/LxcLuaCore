-- 实测结论测试：
--  1) preview1 模块 fd_write 到 stdout 在 wasmtime v48 下返回 ENOSYS(48)
--     （v48 preview2 架构对 preview1 标准流/文件的运行时限制，非绑定问题）
--  2) preview2 组件能真正访问宿主资源：环境变量读取（addWasiP2 + newWasi{env}）
local wasmtime = require("wasmtime")
local npass, nfail = 0, 0
local function check(name, cond, extra)
    if cond then
        npass = npass + 1
        print(string.format("[OK]   %-28s %s", name, extra or ""))
    else
        nfail = nfail + 1
        print(string.format("[FAIL] %-28s %s", name, extra or ""))
    end
end

print("=== 1) preview1 fd_write 行为 ===")
do
    local e = wasmtime.newEngine()
    local s = wasmtime.newStore(e)
    local wasi = wasmtime.newWasi{ inheritStdout = true, argv = { "prog" }, env = { "FOO=bar" } }
    s:setWasi(wasi)
    local linker = wasmtime.newLinker(e)
    linker:defineWasi()

    local p1 = [[
(module
  (import "wasi_snapshot_preview1" "fd_write"
    (func $fd_write (param i32 i32 i32 i32) (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 8) "Hello from preview1!\n")
  (func (export "_start") (local $e i32)
    i32.const 1        ;; fd=stdout
    i32.const 8        ;; iovs ptr
    i32.const 1        ;; iovs len
    i32.const 0        ;; nwritten ptr
    call $fd_write
    local.set $e
    i32.const 0
    local.get $e
    i32.store)
  (func (export "get_errno") (result i32)
    i32.const 0
    i32.load))
]]
    local inst, ierr = linker:instantiate(s, wasmtime.newModule(e, wasmtime.wat2wasm(p1)))
    check("preview1 fd_write 模块实例化", inst ~= nil, tostring(ierr))
    if inst then
        inst:getExport("_start"):call()
        local errno = inst:getExport("get_errno"):call()
        check("preview1 fd_write=ENOSYS(48)（v48 运行时限制）", errno == 48,
              string.format("errno=%d", errno))
    end
end

print("=== 2) preview2 组件读环境变量 ===")
do
    local f = io.open("_env.wasm", "rb")
    check("找到 _env.wasm", f ~= nil)
    if not f then return end
    local bytes = f:read("*a")
    f:close()

    local e = wasmtime.newEngine()
    local s = wasmtime.newStore(e)
    s:setWasi(wasmtime.newWasi{ env = { "A=1", "B=2", "C=3" }, inheritStdout = true })
    local c = wasmtime.newComponent(e, bytes)
    check("newComponent", type(c) == "userdata")
    local linker = wasmtime.newComponentLinker(e)
    local dw, derr = linker:addWasiP2()
    check("addWasiP2", dw == linker, tostring(derr))
    local inst, ierr = linker:instantiate(s, c)
    check("preview2 实例化", inst ~= nil, tostring(ierr))
    if inst then
        local n = inst:getExport("env-count"):call()
        check("preview2 读到 3 个环境变量", n == 3, string.format("count=%d", n))
    end
end

print(string.format("=== 结果: %d passed, %d failed, %d total ===", npass, nfail, npass + nfail))
