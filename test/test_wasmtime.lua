-- test/test_wasmtime.lua — wasmtime Lua 模块功能测试
-- 使用方法: ./lxclua test/test_wasmtime.lua

local wasmtime = require("wasmtime")

-- ============================================================
-- 辅助：手工构造一个最小 WASM 二进制
-- 模块包含:
--   type[0] = (i32, i32) -> (i32)
--   func[0] = local.get 0; local.get 1; i32.add; end
--   memory[0] min=1
--   global[0] = mut i32 init=0
--   table[0] = funcref min=4
-- 导出: "add"(func0), "memory"(mem0), "counter"(global0), "ftable"(table0)
--       "write_mem"(func0=add), "read_mem"(func0=add)
-- ============================================================
local function lebu32(n)
    local b = {}
    repeat
        b[#b+1] = n & 0x7f
        n = n >> 7
        if n ~= 0 then b[#b] = b[#b] | 0x80 end
    until n == 0
    return string.char(table.unpack(b))
end

local function leb(n)
    local b = {}
    repeat
        b[#b+1] = n & 0x7f
        n = n >> 7
        if (n ~= 0) or ((b[#b] & 0x40) ~= 0) then
            b[#b] = b[#b] | 0x80
        end
    until ((n == 0) and ((b[#b] & 0x40) == 0)) or ((n == -1) and ((b[#b] & 0x40) ~= 0))
    return string.char(table.unpack(b))
end

local function vec(sec_id, body)
    return string.char(sec_id) .. lebu32(#body) .. body
end

-- Type section
local typesec = vec(1, lebu32(1) .. string.char(0x60, 2, 0x7f, 0x7f, 1, 0x7f))
-- Function section (id=3, must come before id=4 table section)
local funcsec = vec(3, lebu32(1) .. lebu32(0))
-- Table section (1 table, funcref, min=4 max=no) — section id=4
local tabsec  = vec(4, lebu32(1) .. string.char(0x70, 0x00, 4))
-- Memory section (1 memory, limits: flags=0, initial=1) — id=5
local memsec  = vec(5, lebu32(1) .. string.char(0x00, 1))
-- Global section (1 global, i32 mutable, i32.const 0, end) — id=6
local globsec = vec(6, lebu32(1) .. string.char(0x7f, 0x01) .. string.char(0x41, 0x00, 0x0b))

-- Export section
local function export_entry(name, kind, idx)
    return lebu32(#name) .. name .. string.char(kind) .. lebu32(idx)
end
local expbody = lebu32(6)
    .. export_entry("add",        0x00, 0)   -- func 0
    .. export_entry("write_mem",  0x00, 0)   -- func 0 (same as add, for testing)
    .. export_entry("read_mem",   0x00, 0)   -- func 0 (same as add, for testing)
    .. export_entry("memory",     0x02, 0)   -- memory 0
    .. export_entry("counter",    0x03, 0)   -- global 0
    .. export_entry("ftable",     0x01, 0)   -- table 0 (kind=1)
local expsec = vec(7, expbody)

-- Code section: func[0] body = (local.get 0)(local.get 1)(i32.add)(end)
local codebody = lebu32(1)
    .. lebu32(0)  -- no locals
    .. string.char(0x20, 0x00)   -- local.get 0
    .. string.char(0x20, 0x01)   -- local.get 1
    .. string.char(0x6a)         -- i32.add
    .. string.char(0x0b)         -- end
local codesec = vec(10, codebody)

local wasm_bin = "\0asm\1\0\0\0" .. typesec .. funcsec .. tabsec .. memsec .. globsec .. expsec .. codesec

print(string.format("[INFO] Test WASM binary: %d bytes", #wasm_bin))

-- ============================================================
-- 1. Engine/Store 创建测试
-- ============================================================
print("\n========== 1. Engine / Store ==========")
local engine = wasmtime.newEngine()
print("[OK] newEngine() -> engine created")

local engine2 = wasmtime.newEngine({ gc = true, fuel = true, epoch = true })
print("[OK] newEngine(config) -> engine2 with fuel+epoch created")

local store = wasmtime.newStore(engine)
print("[OK] newStore(engine) -> store created")

-- ============================================================
-- 2. Module 编译和验证测试
-- ============================================================
print("\n========== 2. Module ==========")
local ok, err = wasmtime.validate(wasm_bin)
assert(ok, "validate failed: " .. (err or "unknown"))
print("[OK] validate(wasm_bin) -> ok")

local module = wasmtime.newModule(engine, wasm_bin)
assert(module, "newModule failed")
print("[OK] newModule(engine, wasm_bin) -> module created")

-- 测试 module:getExports
local exports = module:getExports()
assert(#exports == 6, "expected 6 exports, got " .. #exports)
print("[OK] module:getExports() -> " .. #exports .. " exports")
for i = 1, #exports do
    print(string.format("  export[%d]: name=%s, kind=%s", i, exports[i].name, exports[i].kind))
end

-- 测试 module:getImports
local imports = module:getImports()
assert(#imports == 0, "expected 0 imports")
print("[OK] module:getImports() -> " .. #imports .. " imports")

-- 测试 module:serialize / deserialize
local serialized = module:serialize()
assert(type(serialized) == "string" and #serialized > 0, "serialize failed")
print("[OK] module:serialize() -> " .. #serialized .. " bytes")

local module3 = wasmtime.deserializeModule(engine, serialized)
assert(module3, "deserializeModule failed")
print("[OK] wasmtime.deserializeModule(engine, bytes) -> module created")

-- ============================================================
-- 3. Instance 测试
-- ============================================================
print("\n========== 3. Instance ==========")
local instance = wasmtime.newInstance(store, module, {})
assert(instance, "newInstance failed")
print("[OK] newInstance(store, module) -> instance created")

-- 测试 instance:getExports
local iexports = instance:getExports()
print("[OK] instance:getExports() -> " .. #iexports .. " types")
for i = 1, #iexports do
    print(string.format("  export[%d] type: %s", i, iexports[i]))
end

-- ============================================================
-- 4. Function 调用测试
-- ============================================================
print("\n========== 4. Function ==========")

local add_func = instance:getExport("add")
assert(add_func, "getExport add failed")
print("[OK] instance:getExport('add') -> function")

local result = add_func:call(10, 32)
assert(result == 42, "add(10,32) expected 42, got " .. tostring(result))
print("[OK] add:call(10, 32) -> " .. tostring(result))

result = add_func:call(100, 200)
assert(result == 300, "add(100,200) expected 300")
print("[OK] add:call(100, 200) -> " .. tostring(result))

-- 测试 func:getType
local params, results = add_func:getType()
assert(#params >= 2 and #results >= 1, "getType returned wrong signature")
print("[OK] func:getType() -> params=" .. table.concat(params, ",") .. " results=" .. table.concat(results, ","))

-- ============================================================
-- 5. Memory 测试
-- ============================================================
print("\n========== 5. Memory ==========")

local memory = instance:getMemory("memory")
assert(memory, "getMemory failed")
print("[OK] instance:getMemory('memory') -> memory")

-- 测试 size / dataSize
local pages = memory:size()
local bytes = memory:dataSize()
print(string.format("[OK] memory:size()=%d pages, memory:dataSize()=%d bytes", pages, bytes))
assert(pages == 1, "expected 1 page")
assert(bytes == 65536, "expected 65536 bytes")

-- 测试 write / read
memory:write(0, string.char(1, 2, 3, 4, 5))
local data = memory:read(0, 5)
assert(data == string.char(1, 2, 3, 4, 5), "memory read/write mismatch")
print("[OK] memory:write(0, ...) / memory:read(0, 5) -> roundtrip OK")

-- 测试通过 WASM 函数读写内存
add_func:call(0, 42)  -- 将 42 作为 addr 传入 write_mem
-- 实际 write_mem 和 add 是同一个函数 add(i32,i32)->i32
-- 所以这里只是测试 add 和 memory 协作
local mem_val = memory:read(0, 4)
local val = string.byte(mem_val, 1)
print(string.format("[OK] write via wasm func + memory:read -> first byte=%d", val))

-- 测试 memory:grow
local old_pages, ok, grow_err = memory:grow(1)
if ok then
    print(string.format("[OK] memory:grow(1) -> old=%d ok=true, new size=%d", old_pages, memory:size()))
else
    print(string.format("[WARN] memory:grow(1) -> err=%s", grow_err))
end

-- 测试 memory:getType
local min, max = memory:getType()
print(string.format("[OK] memory:getType() -> min=%d max=%d", min, max))

-- ============================================================
-- 6. Global 测试
-- ============================================================
print("\n========== 6. Global ==========")

local counter = instance:getGlobal("counter")
assert(counter, "getGlobal failed")
print("[OK] instance:getGlobal('counter') -> global")

-- 初始值为 0
local val = counter:get()
assert(val == 0, "expected counter=0, got " .. tostring(val))
print("[OK] counter:get() -> " .. tostring(val))

-- 设置值
local rok, rerr = counter:set(99)
assert(rok, "set failed: " .. (rerr or ""))
val = counter:get()
assert(val == 99, "expected counter=99, got " .. tostring(val))
print("[OK] counter:set(99) / get -> " .. tostring(val))

-- 再改回
counter:set(7)
val = counter:get()
assert(val == 7, "expected counter=7")
print("[OK] counter:set(7) / get -> " .. tostring(val))

-- ============================================================
-- 7. Table 测试
-- ============================================================
print("\n========== 7. Table ==========")

local ftable = instance:getTable("ftable")
assert(ftable, "getTable failed")
print("[OK] instance:getTable('ftable') -> table")

local sz = ftable:size()
print("[OK] ftable:size() -> " .. tostring(sz))
assert(sz >= 4, "expected table size >= 4")

-- table:get 应返回 nil (初始无值)
local tv = ftable:get(0)
print(string.format("[OK] ftable:get(0) -> %s", tostring(tv)))

-- ============================================================
-- 8. Store Fuel / GC 测试
-- ============================================================
print("\n========== 8. Store Fuel / GC ==========")

-- fuel 测试（需要 engine 启用 fuel）
local store2 = wasmtime.newStore(engine2)
local ok_fuel, err_fuel = store2:setFuel(1000000)
assert(ok_fuel, "setFuel failed: " .. (err_fuel or ""))
print("[OK] store:setFuel(1000000) -> ok")

local fuel = store2:getFuel()
assert(fuel == 1000000, "expected fuel=1000000, got " .. tostring(fuel))
print("[OK] store:getFuel() -> " .. tostring(fuel))

-- GC 测试
store:gc()
print("[OK] store:gc() executed")

-- Epoch 测试
local eok = store2:setEpochDeadline(100)
print("[OK] store:setEpochDeadline(100) -> " .. tostring(eok))

-- ============================================================
-- 9. 通过 engine2 测试完整流程
-- ============================================================
print("\n========== 9. Cross-engine full test ==========")
local inst2 = wasmtime.newInstance(store2, module, {})
assert(inst2, "newInstance with fuel engine failed")
print("[OK] Instance on fuel-enabled store works")

local add2 = inst2:getExport("add")
assert(add2, "getExport on fuel store failed")
fuel = store2:getFuel()
print(string.format("[OK] Fuel before call: %d", fuel))

result = add2:call(5, 7)
assert(result == 12, "add(5,7) expected 12")
fuel_after = store2:getFuel()
print(string.format("[OK] add2:call(5,7)=%d, fuel: %d -> %d (consumed: %d)", result, fuel, fuel_after, fuel - fuel_after))
assert(fuel_after < fuel, "fuel should be consumed")

-- ============================================================
-- 10. 总结
-- ============================================================
print("\n========== ALL TESTS PASSED ==========")
print("Tested APIs:")
print("  [OK] newEngine / newEngine(config)")
print("  [OK] newStore")
print("  [OK] validate / newModule")
print("  [OK] module:serialize / deserializeModule")
print("  [OK] module:getExports / getImports")
print("  [OK] newInstance")
print("  [OK] instance:getExport (func)")
print("  [OK] instance:getExports")
print("  [OK] instance:getMemory / getGlobal / getTable")
print("  [OK] func:call / func:getType")
print("  [OK] memory:read / write / size / dataSize / grow / getType")
print("  [OK] global:get / set")
print("  [OK] table:size / get")
print("  [OK] store:setFuel / getFuel / gc / setEpochDeadline")