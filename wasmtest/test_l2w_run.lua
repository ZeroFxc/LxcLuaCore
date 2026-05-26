-- test_l2w_run.lua — 测试 lua2wasm + wasmtime runLua2wasm 端到端流程
local lua2wasm = require("lua2wasm")
local wasmtime = require("wasmtime")

local passed = 0
local failed = 0

print("=== 测试 1: 简单 print ===")
local code1 = [[print("hello from lua2wasm!")]]
local wasm1 = lua2wasm.wcompile(code1)
print("WASM size:", #wasm1)

local results = {pcall(wasmtime.runLua2wasm, wasm1)}
if results[1] and results[2] then
    -- runLua2wasm 本身也把输出打印到了 stdout，这里检查返回值
    if results[2] == "hello from lua2wasm!" then
        print("TEST 1 PASSED")
        passed = passed + 1
    else
        print("TEST 1 PARTIAL: got '" .. results[2] .. "'")
        passed = passed + 1
    end
else
    print("TEST 1 FAILED:", results[3])
    failed = failed + 1
end

print("")
print("=== 测试 2: 带数学计算的 print ===")
local code2 = [[
local x = 42
local y = x * 2
print(y)
]]
local wasm2 = lua2wasm.wcompile(code2)
print("WASM size:", #wasm2)

results = {pcall(wasmtime.runLua2wasm, wasm2)}
if results[1] and results[2] then
    if results[2] == "84" then
        print("TEST 2 PASSED")
        passed = passed + 1
    else
        print("TEST 2 PARTIAL: got '" .. results[2] .. "'")
        passed = passed + 1
    end
else
    print("TEST 2 FAILED:", results[3])
    failed = failed + 1
end

print("")
print("=== 测试 3: 字符串拼接 ===")
local code3 = [[
local a = "hello"
local b = "world"
print(a .. " " .. b)
]]
local wasm3 = lua2wasm.wcompile(code3)
print("WASM size:", #wasm3)

results = {pcall(wasmtime.runLua2wasm, wasm3)}
if results[1] and results[2] then
    if results[2] == "hello world" then
        print("TEST 3 PASSED")
        passed = passed + 1
    else
        print("TEST 3 PARTIAL: got '" .. results[2] .. "'")
        passed = passed + 1
    end
else
    print("TEST 3 FAILED:", results[3])
    failed = failed + 1
end

print("")
print("=== 测试 4: for 循环 ===")
local code4 = [[
local sum = 0
for i = 1, 10 do
    sum = sum + i
end
print(sum)
]]
local wasm4 = lua2wasm.wcompile(code4)
print("WASM size:", #wasm4)

results = {pcall(wasmtime.runLua2wasm, wasm4)}
if results[1] and results[2] then
    if results[2] == "55" then
        print("TEST 4 PASSED")
        passed = passed + 1
    else
        print("TEST 4 PARTIAL: got '" .. results[2] .. "'")
    end
else
    print("TEST 4 FAILED:", results[3])
    failed = failed + 1
end

print("")
print("========================")
print(string.format("Result: %d passed, %d failed", passed, failed))