-- test_wasmtime_imports.lua — 测试 wasmtime runLua2wasm 外部导入机制
-- test_wasm_imports.wasm 从 "host" 模块导入 7 个函数:
--   host.math / host.math2 / host.fmt / host.os_time / host.os_clock / host.fs_seek / host.fs_close
-- runLua2wasm 会自动注册全部 28 个 l2w host import 并链接

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

local function assert_eq(actual, expected, label)
    if actual == expected then
        print(string.format("  [OK] %s: %s == %s", label, tostring(actual), tostring(expected)))
        passed = passed + 1
    else
        print(string.format("  [FAIL] %s: got %s, expected %s", label, tostring(actual), tostring(expected)))
        failed = failed + 1
    end
end

local function assert_near(actual, expected, tolerance, label)
    local diff = math.abs(actual - expected)
    if diff <= tolerance then
        print(string.format("  [OK] %s: %.6f ~= %.6f (diff=%.6f)", label, actual, expected, diff))
        passed = passed + 1
    else
        print(string.format("  [FAIL] %s: got %.6f, expected %.6f (diff=%.6f)", label, actual, expected, diff))
        failed = failed + 1
    end
end

-- ==========================================================
-- Part A: runLua2wasm — 自动链接 host import 并运行 main()
-- ==========================================================
print("=== Part A: runLua2wasm 导入链接测试 ===")

local wasm_path_a = "test_wasm_imports.wasm"
local wasm_bytes_a, err_a = read_file(wasm_path_a)
if not wasm_bytes_a then
    print("FAIL: " .. err_a)
    os.exit(1)
end
print("WASM 大小: " .. #wasm_bytes_a .. " 字节")

-- runLua2wasm 内部: 创建 engine→store→linker→编译→注册 28 个 host import→实例化→调用 main()
-- 成功返回捕获的输出字符串, 失败返回 nil + error
local result_a, err_msg = wasmtime.runLua2wasm(wasm_bytes_a)
if result_a then
    print("[OK] runLua2wasm 成功 (28 个 host import 全部链接)")
    print("     捕获输出: '" .. result_a .. "'")
    passed = passed + 1
else
    print("[FAIL] runLua2wasm 失败: " .. tostring(err_msg))
    failed = failed + 1
end

print("")

-- ==========================================================
-- Part B: 手动创建引擎, 编译模块, 调用导出函数验证结果
-- 由于 newInstance 暂不支持 Lua 侧导入表, 这里仅验证模块可编译
-- ==========================================================
print("=== Part B: 模块编译与导出验证 ===")

local engine  = wasmtime.newEngine()
local store   = wasmtime.newStore(engine)
local module  = wasmtime.newModule(engine, wasm_bytes_a)
print("[OK] 模块编译成功")

-- 验证导出函数存在 (由于模块有 import, newInstance 会失败, 这里只验证编译)
-- 如果 newInstance 支持了导入, 取消下面注释即可:
-- local instance = wasmtime.newInstance(store, module)  -- 会因缺失 import 而报错(预期)
-- print("实例化结果: " .. (instance and "成功" or "失败"))

-- 演示: 用 validate 检查模块合法性 (必须传 engine 避免栈 bug)
local ok, vmsg = wasmtime.validate(engine, wasm_bytes_a)
assert_eq(ok, true, "validate(imports_module)")

print("")

-- ==========================================================
-- Part C: 手动模拟 runLua2wasm 的部分行为 (newLinker)
-- 验证 linker 可创建, 但 defineFunc 暂未暴露到 Lua
-- ==========================================================
print("=== Part C: Linker 创建测试 ===")

local linker = wasmtime.newLinker(engine)
if linker then
    print("[OK] newLinker(engine) 成功")
    passed = passed + 1
else
    print("[FAIL] newLinker(engine) 失败")
    failed = failed + 1
end

print("")
print("========================")
print(string.format("结果: %d passed, %d failed, %d total", passed, failed, passed + failed))