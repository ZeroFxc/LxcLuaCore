--[[
fibonacci_nativevm_nativeparser.lua
演示 nativevm + nativeparser 配合使用，编译并运行斐波那契数列

nativeparser.compile() 接收类 Lua 的 NLang 源码，返回三个值:
  1. main 主字节码数组
  2. nregs (所需的寄存器数量)
  3. func_defs (函数定义表，含函数名、字节码、寄存器数、参数数、上值信息)

注意: parser.compile 生成的代码第一条是 LOADK R0,0 (初始化 func_id)，
会覆盖 native.call 传入的参数。所以 NLang 源码中 n 必须硬编码，不能通过
native.call(nv, n) 传参。
--]]

local native = require("nativevm")
local parser = require("nativeparser")

local PASS, FAIL = 0, 0
local function check(cond, msg)
    if cond then
        PASS = PASS + 1
        print("[OK]  " .. msg)
    else
        FAIL = FAIL + 1
        print("[ERR] " .. msg)
    end
end

print([[
╔══════════════════════════════════════════════════════════════╗
║   NativeVM + NativeParser 斐波那契数列                      ║
╚══════════════════════════════════════════════════════════════╝
]])

-- ================================================================
-- 1. NLang 源码 — 迭代法斐波那契函数 + 调用
-- ================================================================
print("--- 1. NLang 源码编译 (迭代法) ---")

local nlang_src = [[
function fibonacci(n)
    local a = 0
    local b = 1
    local i = 2
    while i <= n do
        local c = a + b
        a = b
        b = c
        i = i + 1
    end
    return b
end

return fibonacci(20)
]]

local code, nregs, funcs = parser.compile(nlang_src)
check(type(code) == "table", "parser.compile 返回字节码数组")
check(#code > 0, "主程序字节码长度 = " .. #code)
check(type(nregs) == "number", "nregs = " .. nregs)
check(type(funcs) == "table" and #funcs > 0, "函数定义表有 " .. #funcs .. " 个函数")

-- 打印反汇编
print("\n   主程序反汇编:")
for i = 1, math.min(#code, 20) do
    print("     [" .. i .. "] " .. native.disasm(code[i]))
end
if #code > 20 then
    print("     ... (共 " .. #code .. " 条)")
end

-- 打印函数定义
print("\n   函数定义:")
for i = 1, #funcs do
    local fd = funcs[i]
    print("     [" .. i .. "] " .. (fd.name or "<anonymous>")
        .. " | nregs=" .. fd.nregs
        .. " | nparams=" .. (fd.nparams or 0))
    local fcode = fd.code
    if fcode then
        for j = 1, math.min(#fcode, 20) do
            print("         [" .. j .. "] " .. native.disasm(fcode[j]))
        end
        if #fcode > 20 then
            print("         ... (共 " .. #fcode .. " 条)")
        end
    end
end

-- ================================================================
-- 2. 创建 VM 并注册函数 → 执行
-- ================================================================
print("\n--- 2. 创建 VM → 执行 fibonacci(20) ---")

local nv = native.new(code, nregs)
check(type(nv) == "userdata", "native.new 创建 VM 实例")

for i = 1, #funcs do
    local fd = funcs[i]
    local func_id = native.deffunc(nv, fd.code, fd.nregs, fd.nparams, fd.upvalues)
    check(type(func_id) == "number", "注册函数 '" .. (fd.name or "?") .. "'  → func_id=" .. func_id)
end

local result = native.call(nv)
check(result == 6765, "迭代法 fib(20) = " .. tostring(result))

-- ================================================================
-- 3. 内联版本 (native.asm 表达式语法) — 参数通过 R0 传入
-- ================================================================
print("\n--- 3. 内联版本: native.asm 表达式语法 (参数从 R0 传入) ---")

-- 注意: native.asm 只支持 # 和 ; 注释, 不支持 --
local fib_inline_tmpl = [[
    # R0 = n (从 native.call(nv, n) 传入)
    R1 = 0      # a
    R2 = 1      # b
    R3 = 2      # i
.loop:
    R5 = R3
    R6 = R0
    R4 = R5 <= R6  # i <= n ?
    JF R4, .done
    R7 = R1 + R2   # c = a + b
    R8 = R2
    R1 = R8        # a = b
    R9 = R7
    R2 = R9        # b = c
    R10 = R3
    R11 = 1
    R3 = R10 + R11 # i = i + 1
    JMP .loop
.done:
    RET R2, 1
]]
local fib_inline = native.asm(fib_inline_tmpl)
local nv_inline = native.new(fib_inline, 16)

print("\n   斐波那契数列 (内联版本):")
local fib_vals = {10, 15, 20, 25, 30}
local expected = {55, 610, 6765, 75025, 832040}
for i = 1, #fib_vals do
    local n = fib_vals[i]
    local r = native.call(nv_inline, n)
    check(r == expected[i], "fib(" .. n .. ") = " .. r .. " (期望 " .. expected[i] .. ")")
end

-- ================================================================
-- 4. NLang 源码 — 编译 fib(20) 并执行
-- ================================================================
print("\n--- 4. NLang 源码: 编译并执行 fib(20) ---")

local src_fib20 = [[
    function fibonacci(x)
        local a = 0
        local b = 1
        local i = 2
        while i <= x do
            local c = a + b
            a = b
            b = c
            i = i + 1
        end
        return b
    end
    return fibonacci(20)
]]

local fc, fnr, ffs = parser.compile(src_fib20)
local fnv = native.new(fc, fnr)
for j = 1, #ffs do
    local fd = ffs[j]
    native.deffunc(fnv, fd.code, fd.nregs, fd.nparams, fd.upvalues)
end
local r = native.call(fnv)
check(r == 6765, "fib(20) = " .. r .. " (期望 6765)")

-- ================================================================
-- 5. 递归版 fibonacci — 展示 NativeVM 支持递归调用
-- ================================================================
print("\n--- 5. 递归版: fib(20) ---")

local rec_src = [[
    function fib(n)
        if n <= 1 then
            return n
        else
            return fib(n - 1) + fib(n - 2)
        end
    end
    return fib(20)
]]

local rc, rnr, rfs = parser.compile(rec_src)
check(type(rc) == "table", "递归版编译成功")
local rnv = native.new(rc, rnr)
for j = 1, #rfs do
    native.deffunc(rnv, rfs[j].code, rfs[j].nregs, rfs[j].nparams, rfs[j].upvalues)
end
local rr = native.call(rnv)
check(rr == 6765, "递归 fib(20) = " .. tostring(rr))

-- 显示递归函数字节码
print("\n   递归 fib 函数反汇编:")
if #rfs > 0 then
    local fd = rfs[1]
    for j = 1, math.min(#fd.code, 25) do
        print("      [" .. j .. "] " .. native.disasm(fd.code[j]))
    end
    if #fd.code > 25 then
        print("      ... (共 " .. #fd.code .. " 条)")
    end
end

-- ================================================================
-- 6. 递归性能对比: NativeVM vs 纯 Lua — fib(20) × 100 次
--    (同一 VM 复用, n 硬编码在 NLang 源码中)
-- ================================================================
print("\n--- 6. 递归性能对比: fib(20) × 100 次 ---")

local R = 100
native.call(rnv)  -- warmup

local t0 = os.clock()
for i = 1, R do
    native.call(rnv)
end
local t_nv_rec = os.clock() - t0

-- 纯 Lua 递归版
function lua_fib_rec(n)
    if n <= 1 then return n end
    return lua_fib_rec(n - 1) + lua_fib_rec(n - 2)
end
lua_fib_rec(20)  -- warmup

t0 = os.clock()
for i = 1, R do
    lua_fib_rec(20)
end
local t_lua_rec = os.clock() - t0

print(string.format("  NativeVM 递归: %.4f 秒 (%d 次)", t_nv_rec, R))
print(string.format("  纯 Lua 递归:  %.4f 秒 (%d 次)", t_lua_rec, R))
print(string.format("  NativeVM 递归 ≈ %.1fx 纯Lua 递归速度",
    t_lua_rec / (t_nv_rec > 0 and t_nv_rec or 0.001)))

-- ================================================================
-- 7. 内联版性能对比: fib(30) × 5000 次
-- ================================================================
print("\n--- 7. 内联版性能对比: fib(30) × 5000 次 ---")

local NV_ROUNDS = 5000
local NV_N = 30

local nv_bench = native.new(fib_inline, 16)
native.call(nv_bench, NV_N)  -- warmup

t0 = os.clock()
for i = 1, NV_ROUNDS do
    native.call(nv_bench, NV_N)
end
local t_nv = os.clock() - t0

-- 纯 Lua 迭代版本
function lua_fib(n)
    local a, b = 0, 1
    for i = 2, n do
        a, b = b, a + b
    end
    return b
end
lua_fib(NV_N)  -- warmup

t0 = os.clock()
for i = 1, NV_ROUNDS do
    lua_fib(NV_N)
end
local t_lua = os.clock() - t0

print(string.format("  NativeVM 内联: %.4f 秒 (%d 次)", t_nv, NV_ROUNDS))
print(string.format("  纯 Lua 迭代:  %.4f 秒 (%d 次)", t_lua, NV_ROUNDS))
print(string.format("  NativeVM 内联 ≈ %.1fx 纯Lua 迭代速度",
    t_lua / (t_nv > 0 and t_nv or 0.001)))

-- ================================================================
-- 测试结果汇总
-- ================================================================
print("\n========================================================")
print(string.format("通过: %d   失败: %d   总计: %d", PASS, FAIL, PASS + FAIL))
print("========================================================")

-- 清理所有 VM 引用，避免退出时 GC 导致的堆损坏
nv, nv_inline, fnv, rnv, nv_bench = nil, nil, nil, nil, nil
collectgarbage("collect")

if FAIL > 0 then
    os.exit(1)
end