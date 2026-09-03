--[[
example_nativevm.lua — 纯 C 原生高速 VM 完整教学示例

本文件演示 lnativevm.c 的所有功能模块：
  1. 指令常量一览
  2. native.asm() — 助记符汇编器
  3. native.new() — 创建 VM 实例
  4. native.call() — 执行并获取返回值
  5. native.disasm() — 反汇编输出
  6. 控制流 — JMP / JT / JF / HALT / RET
  7. 算术运算 — 整数四则 / 比较
  8. 综合示例 — 累加循环 / 斐波那契 / 阶乘 / 判断素数
  9. 性能基准 — 对比纯 Lua

关键约定:
  - ADD/SUB/MUL/DIV/EQ/LT 等三操作数指令 全部使用寄存器操作数
  - 常量需用 LOADK 预加载到寄存器, 再做运算
  - JF/JT 偏移 = RET所在pc - 当前pc - 1
  - JMP 偏移 = 循环头所在pc - 当前pc - 1
  - 循环终止条件用 LT Rx, counter, limit (limit = 期望次数+1)
--]]

local native = require("nativevm")

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

-- ==================================================================
-- 1. 模块常量一览
-- ==================================================================
print([[
        纯 C 原生高速 VM (NativeVM) 示例
========================================================

1. 模块常量与指令集]])

check(native.NOP   == 0,  "NOP    = 0   (空操作)")
check(native.MOV   == 5,  "MOV    = 5   (寄存器复制)")
check(native.LOADK == 1,  "LOADK  = 1   (载入整数常量)")
check(native.LOADKF == 2, "LOADKF = 2   (载入浮点常量)")
check(native.ADD   == 6,  "ADD    = 6   (整数加法)")
check(native.SUB   == 7,  "SUB    = 7   (整数减法)")
check(native.MUL   == 8,  "MUL    = 8   (整数乘法)")
check(native.DIV   == 9,  "DIV    = 9   (整数除法)")
check(native.MOD   == 10, "MOD    = 10  (整数取模)")
check(native.ADDF  == 11, "ADDF   = 11  (浮点加法)")
check(native.SUBF  == 12, "SUBF   = 12  (浮点减法)")
check(native.MULF  == 13, "MULF   = 13  (浮点乘法)")
check(native.DIVF  == 14, "DIVF   = 14  (浮点除法)")
check(native.EQ    == 20, "EQ     = 20  (比较相等)")
check(native.NE    == 21, "NE     = 21  (比较不等)")
check(native.LT    == 22, "LT     = 22  (比较小于)")
check(native.LE    == 23, "LE     = 23  (比较小于等于)")
check(native.JMP   == 26, "JMP    = 26  (无条件跳转)")
check(native.JT    == 27, "JT     = 27  (为真跳转)")
check(native.JF    == 28, "JF     = 28  (为假跳转)")
check(native.RET   == 29, "RET    = 29  (返回)")
check(native.HALT  == 39, "HALT   = 39  (停止)")

-- ==================================================================
-- 2. native.asm — 助记符汇编器基础
-- ==================================================================
print("\n2. native.asm — 助记符汇编器")

local code_arith = native.asm([[
    LOADK R0, 10
    LOADK R1, 3
    ADD   R2, R0, R1
    RET   R2, 1
]])
check(type(code_arith) == "table", "asm 返回 table")
check(#code_arith == 4, "asm 生成了 4 条指令")

print("   反汇编输出:")
for i = 1, #code_arith do
    print("     [" .. i .. "] " .. native.disasm(code_arith[i]))
end

-- ==================================================================
-- 3. native.new + native.call — 创建与执行
-- ==================================================================
print("\n3. native.new + native.call — 创建 VM 并执行")

local nv = native.new(code_arith, 16)
check(type(nv) == "userdata", "native.new 返回 userdata")

local r = native.call(nv)
check(r == 13, "10 + 3 = " .. tostring(r))

-- 带参数的调用 (参数自动写入 R0..Rn)
local code_mul = native.asm([[
    MUL R2, R0, R1
    RET R2, 1
]])
local nv2 = native.new(code_mul, 8)
r = native.call(nv2, 6, 7)
check(r == 42, "native.call(nv, 6, 7) = 6*7 = " .. tostring(r))

-- ==================================================================
-- 4. 基本运算指令
-- ==================================================================
print("\n4. 基本运算指令 (ADD/SUB/MUL/DIV/LT/EQ)")

-- 整数四则
local code_ops = native.asm([[
    LOADK R0, 100
    LOADK R1, 7
    ADD   R2, R0, R1
    SUB   R3, R0, R1
    MUL   R4, R0, R1
    DIV   R5, R0, R1
    RET   R2, 4
]])
local nv3 = native.new(code_ops, 16)
local add, sub, mul, divv = native.call(nv3)
check(add == 107, "100 + 7 = " .. tostring(add))
check(sub == 93,  "100 - 7 = " .. tostring(sub))
check(mul == 700, "100 * 7 = " .. tostring(mul))
check(divv == 14, "100 / 7 = " .. tostring(divv))

-- 比较运算
local code_cmp = native.asm([[
    LOADK R0, 10
    LOADK R1, 20
    LT    R2, R0, R1
    LT    R3, R1, R0
    EQ    R4, R0, R0
    EQ    R5, R0, R1
    RET   R2, 4
]])
local nv4 = native.new(code_cmp, 16)
local lt1, lt2, eq1, eq2 = native.call(nv4)
check(lt1 == 1, "10 < 20  = " .. tostring(lt1))
check(lt2 == 0, "20 < 10  = " .. tostring(lt2))
check(eq1 == 1, "10 == 10 = " .. tostring(eq1))
check(eq2 == 0, "10 == 20 = " .. tostring(eq2))

-- ==================================================================
-- 5. 控制流 — JMP / JT / JF / HALT
-- ==================================================================
print("\n5. 控制流 — JMP / JT / JF")

-- 5.1 JMP 无条仨跳转
local code_jmp = native.asm([[
    LOADK R0, 42
    JMP   2
    LOADK R0, 99
    LOADK R0, 999
    RET   R0, 1
]])
local nv5 = native.new(code_jmp, 8)
r = native.call(nv5)
check(r == 42, "JMP 跳过中间代码 → " .. tostring(r))

-- 5.2 JT 为真跳转: if-else
local code_jt = native.asm([[
    LOADK R0, 10
    LOADK R1, 5
    LT    R4, R1, R0
    JT    R4, 2
    LOADK R2, 0
    JMP   1
    LOADK R2, 100
    RET   R2, 1
]])
nv5 = native.new(code_jt, 8)
r = native.call(nv5)
check(r == 100, "JT: 5<10 为真, 跳过假分支 → " .. tostring(r))

-- 5.3 JF 循环: 1+2+...+10 = 55
print("\n   循环示例: 1+2+...+10 = 55")
local code_loop = native.asm([[
    LOADK R0, 0
    LOADK R1, 11
    LOADK R2, 1
    LOADK R3, 1
    LOADK R4, 0
    LT    R4, R2, R1
    JF    R4, 3
    ADD   R0, R0, R2
    ADD   R2, R2, R3
    JMP   -5
    RET   R0, 1
]])
nv5 = native.new(code_loop, 16)
r = native.call(nv5)
check(r == 55, "循环累加 1..10 = " .. tostring(r))

-- 5.4 HALT 终止
local code_halt = native.asm([[
    LOADK R0, 88
    HALT
    LOADK R0, 99
]])
nv5 = native.new(code_halt, 8)
r = native.call(nv5)
check(r == 88, "HALT 提前终止 → " .. tostring(r))

-- ==================================================================
-- 6. RET 多返回值
-- ==================================================================
print("\n6. RET 多返回值")

local code_ret = native.asm([[
    LOADK R0, 10
    LOADK R1, 20
    LOADK R2, 30
    RET   R0, 3
]])
nv5 = native.new(code_ret, 8)
local ra, rb, rc = native.call(nv5)
check(ra == 10 and rb == 20 and rc == 30,
    string.format("多返回值: %d, %d, %d", ra, rb, rc))


-- ==================================================================
-- 7. 斐波那契 fib(20) = 6765
-- ==================================================================
print("\n7. 斐波那契 fib(20) = 6765")
-- 迭代: a=0, b=1; for i=2..n: c=a+b, a=b, b=c; return b
-- LT 检查 i <= n (即 i < n+1)

local code_fib = native.asm([[
    LOADK R0, 0
    LOADK R1, 1
    LOADK R2, 21
    LOADK R6, 1
    LOADK R7, 2
    LOADK R4, 0
    LOADK R5, 0
    LT    R4, R7, R2
    JF    R4, 5
    ADD   R5, R0, R1
    MOV   R0, R1
    MOV   R1, R5
    ADD   R7, R7, R6
    JMP   -7
    RET   R1, 1
]])
nv5 = native.new(code_fib, 16)
r = native.call(nv5)
check(r == 6765, "fib(20) = " .. tostring(r))


-- ==================================================================
-- 8. 阶乘 10! = 3628800
-- ==================================================================
print("\n8. 阶乘 10! = 3628800")

local code_fact = native.asm([[
    LOADK R0, 1
    LOADK R1, 11
    LOADK R2, 1
    LOADK R3, 1
    LOADK R4, 0
    LT    R4, R2, R1
    JF    R4, 3
    MUL   R0, R0, R2
    ADD   R2, R2, R3
    JMP   -5
    RET   R0, 1
]])
nv5 = native.new(code_fact, 16)
r = native.call(nv5)
check(r == 3628800, "10! = " .. tostring(r))


-- ==================================================================
-- 9. 判断素数
-- ==================================================================
print("\n9. 判断素数 (97 是素数, 100 不是)")
-- 试除法: i 从 2 到 sqrt(n), 若 n % i == 0 则不是素数
-- n%i = n - (n/i)*i

local code_prime = native.asm([[
    LOADK R1, 1
    LOADK R2, 2
    LOADK R0, 97
    LOADK R7, 1
    LOADK R4, 0
    LOADK R5, 0
    LOADK R6, 0
    MUL   R4, R2, R2
    LT    R5, R0, R4
    JT    R5, 8
    DIV   R6, R0, R2
    MUL   R6, R6, R2
    EQ    R6, R0, R6
    JF    R6, 2
    LOADK R1, 0
    JMP   2
    ADD   R2, R2, R7
    JMP   -11
    RET   R1, 1
]])
nv5 = native.new(code_prime, 16)
r = native.call(nv5)
check(r == 1, "97 是素数 → " .. tostring(r))

-- 合数 100
local code_prime2 = native.asm([[
    LOADK R1, 1
    LOADK R2, 2
    LOADK R0, 100
    LOADK R7, 1
    LOADK R4, 0
    LOADK R5, 0
    LOADK R6, 0
    MUL   R4, R2, R2
    LT    R5, R0, R4
    JT    R5, 8
    DIV   R6, R0, R2
    MUL   R6, R6, R2
    EQ    R6, R0, R6
    JF    R6, 2
    LOADK R1, 0
    JMP   2
    ADD   R2, R2, R7
    JMP   -11
    RET   R1, 1
]])
nv5 = native.new(code_prime2, 16)
r = native.call(nv5)
check(r == 0, "100 不是素数 → " .. tostring(r))


-- ==================================================================
-- 10. 性能基准 — NativeVM vs 纯 Lua
-- ==================================================================
print("\n10. 性能基准 — NativeVM vs 纯 Lua 整数累加")
print("  (注意: 简单循环中字节码 VM 的指令解码开销 > 纯 Lua 内置 for 循环)")

local NV_ROUNDS = 5000
local NV_ITER = 100000
local code_bench = native.asm([[
    LOADK R0, 0
    LOADK R1, ]] .. NV_ITER .. [[
    LOADK R2, 0
    LOADK R3, 1
    LOADK R4, 0
    LT    R4, R2, R1
    JF    R4, 3
    ADD   R0, R0, R3
    ADD   R2, R2, R3
    JMP   -5
    RET   R0, 1
]])
local nv_bench = native.new(code_bench, 16)
native.call(nv_bench)  -- warmup

local t0 = os.clock()
for i = 1, NV_ROUNDS do
    native.call(nv_bench)
end
local t_nv = os.clock() - t0
local total_nv = NV_ROUNDS * NV_ITER
print(string.format("  NativeVM:  %.4f 秒 (%d 次运算) | %.0f 万 ops/s",
    t_nv, total_nv, total_nv / (t_nv > 0 and t_nv or 0.001) / 10000))

-- 纯 Lua 同样次数
t0 = os.clock()
for r = 1, NV_ROUNDS do
    local s = 0
    for i = 1, NV_ITER do s = s + 1 end
end
local t_lua = os.clock() - t0
local total_lua = NV_ROUNDS * NV_ITER
print(string.format("  纯 Lua:   %.4f 秒 (%d 次运算) | %.0f 万 ops/s",
    t_lua, total_lua, total_lua / (t_lua > 0 and t_lua or 0.001) / 10000))

print(string.format("  NativeVM ≈ %.1fx 纯Lua 速度", t_lua / (t_nv > 0 and t_nv or 0.001)))
print("  (解释型 VM 每条指令都需要解码+switch, 额外开销在此)")


-- ==================================================================
-- 测试结果汇总
-- ==================================================================
print("\n========================================================")
print(string.format("测试完毕: 通过 %d, 失败 %d, 总计 %d", PASS, FAIL, PASS + FAIL))
print("========================================================")

if FAIL > 0 then
    os.exit(1)
end