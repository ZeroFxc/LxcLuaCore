--[[
example_nativevm_advanced.lua — NativeVM 高级语法完整示例

本文件演示 lnativevm.c 的三大高级语法层:

=== Layer 1: native.asm() 表达式语法 ===
  R0 = 42                  → LOADK
  R0 = 3.14                → LOADKF
  R0 = R1                  → MOV
  R0 = R1 + R2             → ADD
  R0 = R1 - R2 / * / / / % / & / | / ^ / << / >>
  R0 = R1 < R2 / <= / > / >= / == / !=
  R0 = -R1                 → NEG
  R0 = !R1                 → EQ R0,R1,R0
  R0 = R1 + 100            → LOADK tmp,100; ADD R0,R1,tmp
  R0 = 100 + R1            → LOADK tmp,100; ADD R0,tmp,R1

=== Layer 2: native.asm() 标签系统 ===
  .loop:                   ← 定义标签
  .done:                   ← 定义标签
  JMP .loop                ← 引用标签, 自动计算偏移
  JT R4, .done             ← 条件跳转到标签
  JF R4, .next             ← 条件假时跳转

=== Layer 3: native.compile() HLL 编译器 ===
  .regs <n>                寄存器总数提示
  .program <name>          程序名 (注释用)
  @name = R<num>           寄存器别名定义
  @dest = @src             赋值 MOV
  @dest = <int>/<float>    常量加载
  @dest = @a op @b         二元运算
  @dest = -(@src)          取负
  @dest = !(@src)          逻辑非
  while @a <op> @b ... end  while 循环
  if @a <op> @b ... else ... end   if/else 条件
  ret @start [, count]     返回
  halt                     停止
]]

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

print([[
╔══════════════════════════════════════════════════════════╗
║    NativeVM 高级语法示例                                 ║
╚══════════════════════════════════════════════════════════╝
]])

-- ==================================================================
-- Part A: native.asm() 表达式语法
-- ==================================================================
print([[
┌──────────────────────────────────────────────────────────┐
│ A. native.asm() — 表达式语法                              │
└──────────────────────────────────────────────────────────┘
]])

-- A1: 基本赋值: 整数和浮点常量
print("A1. 表达式赋值: 整数/浮点常量")
local code_expr1 = native.asm([[
    R0 = 42
    R1 = 3.14
    RET R0, 2
]])
print("   源码: R0 = 42 ; R1 = 3.14")
local nv_a1 = native.new(code_expr1, 4)
local a_int, a_fp = native.call(nv_a1)
check(a_int == 42, "R0 = 42   → " .. tostring(a_int))
check(math.abs(a_fp - 3.14) < 0.01, "R1 = 3.14 → " .. tostring(a_fp))

-- A2: 寄存器间复制 MOV
print("\nA2. 表达式赋值: MOV")
local code_expr2 = native.asm([[
    R0 = 99
    R1 = R0
    R2 = R0
    ADD R3, R1, R2
    RET R3, 1
]])
local nv_a2 = native.new(code_expr2, 8)
local r = native.call(nv_a2)
check(r == 198, "R1=R0; R2=R0; R3=R1+R2 → 198")

-- A3: 二元运算 — 整数算术
print("\nA3. 表达式二元运算: 整数算术 (+, -, *, /, %, &, |, ^, <<, >>)")
local code_expr3 = native.asm([[
    R1 = 100
    R2 = 7
    R3  = R1 + R2
    R4  = R1 - R2
    R5  = R1 * R2
    R6  = R1 / R2
    R7  = R1 % R2
    R8  = R1 & R2
    R9  = R1 | R2
    R10 = R1 ^ R2
    R11 = R1 << R2
    R12 = R1 >> R2
    RET R3, 10
]])
local nv_a3 = native.new(code_expr3, 16)
local add, sub, mul, divv, modv,
      andv, orv, xorv, shl, shr = native.call(nv_a3)
check(add == 107, "100 + 7   = " .. tostring(add))
check(sub == 93,  "100 - 7   = " .. tostring(sub))
check(mul == 700, "100 * 7   = " .. tostring(mul))
check(divv == 14, "100 / 7   = " .. tostring(divv))
check(modv == 2,  "100 % 7   = " .. tostring(modv))
check(andv == 4,  "100 & 7   = " .. tostring(andv))
check(orv == 103, "100 | 7   = " .. tostring(orv))
check(xorv == 99, "100 ^ 7   = " .. tostring(xorv))
check(shl == 12800, "100 << 7 = " .. tostring(shl))
check(shr == 0,   "100 >> 7 = " .. tostring(shr))

-- A4: 比较运算
print("\nA4. 表达式比较运算 (<, <=, >, >=, ==, !=)")
local code_expr4 = native.asm([[
    R1 = 10
    R2 = 20
    R3 = 10
    R4 = R1 < R2
    R5 = R1 <= R2
    R6 = R1 <= R3
    R7 = R1 > R2
    R8 = R1 >= R2
    R9 = R1 == R3
    R10 = R1 != R2
    RET R4, 7
]])
local nv_a4 = native.new(code_expr4, 16)
local lt, le_true, le_eq, gt, ge, eq, ne = native.call(nv_a4)
check(lt == 1,     "10 < 20     = " .. tostring(lt))
check(le_true == 1,"10 <= 20    = " .. tostring(le_true))
check(le_eq == 1,  "10 <= 10    = " .. tostring(le_eq))
check(gt == 0,     "10 > 20     = " .. tostring(gt))
check(ge == 0,     "10 >= 20    = " .. tostring(ge))
check(eq == 1,     "10 == 10    = " .. tostring(eq))
check(ne == 1,     "10 != 20    = " .. tostring(ne))

-- A5: 一元运算 — 取负 NEG
print("\nA5. 表达式一元运算: 取负 R0 = -R1")
local code_expr5 = native.asm([[
    R1 = 42
    R0 = -R1
    RET R0, 1
]])
local nv_a5 = native.new(code_expr5, 4)
r = native.call(nv_a5)
check(r == -42, "R0 = -R1; R1=42 → " .. tostring(r))

-- A6: 一元运算 — 逻辑非 !
print("\nA6. 表达式一元运算: 逻辑非 R0 = !R1")
local code_expr6 = native.asm([[
    R1 = 0
    R0 = !R1
    R3 = R0
    R2 = 42
    R4 = !R2
    RET R3, 2
]])
local nv_a6 = native.new(code_expr6, 8)
local not_zero, not_nonzero = native.call(nv_a6)
check(not_zero == 1,     "!0  (R1=0)   → " .. tostring(not_zero))
check(not_nonzero == 0,  "!42 (R2=42)  → " .. tostring(not_nonzero))

-- A7: 混合寄存器+立即数运算 (R0 = R1 + imm)
print("\nA7. 表达式混合运算: 寄存器 + 立即数 R0 = R1 + 100")
local code_expr7 = native.asm([[
    R1 = 50
    R0 = R1 + 100
    R2 = R0 * 3
    RET R2, 1
]], 250)
local nv_a7 = native.new(code_expr7, 256)
r = native.call(nv_a7)
check(r == 450, "(50 + 100) * 3 = " .. tostring(r))

-- A8: 浮点运算
print("\nA8. 表达式浮点运算 (ADDF/SUBF/MULF/DIVF)")
-- 注意: 表达式语法对浮点的直接支持有限, 使用传统助记符
local code_expr8 = native.asm([[
    LOADKF R0, 3.5
    LOADKF R1, 2.0
    ADDF R2, R0, R1
    SUBF R3, R0, R1
    MULF R4, R0, R1
    DIVF R5, R0, R1
    RET R2, 4
]])
local nv_a8 = native.new(code_expr8, 8)
local fadd, fsub, fmul, fdiv = native.call(nv_a8)
check(math.abs(fadd - 5.5) < 0.001, "3.5 + 2.0 = " .. tostring(fadd))
check(math.abs(fsub - 1.5) < 0.001, "3.5 - 2.0 = " .. tostring(fsub))
check(math.abs(fmul - 7.0) < 0.001, "3.5 * 2.0 = " .. tostring(fmul))
check(math.abs(fdiv - 1.75) < 0.001,"3.5 / 2.0 = " .. tostring(fdiv))

-- A9: 类型转换 I2F / F2I
print("\nA9. 类型转换: I2F / F2I / MOVF / MOVI")
local code_expr9 = native.asm([[
    LOADK  R0, 42
    I2F    R1, R0
    F2I    R2, R1
    LOADKF R3, 3.14
    MOVI   R4, R3
    MOVF   R5, R4
    RET R1, 5
]])
local nv_a9 = native.new(code_expr9, 8)
local i2fv, f2iv, moviv, movfv = native.call(nv_a9)
check(math.abs(i2fv - 42.0) < 0.001, "I2F: 42 → " .. tostring(i2fv))
check(f2iv == 42,       "F2I: 42.0 → " .. tostring(f2iv))
check(moviv == 3,       "MOVI: 3.14 → " .. tostring(moviv))
check(math.abs(movfv - 3.0) < 0.001, "MOVF: 3 → " .. tostring(movfv))

-- A10: SETNIL / ISNIL
print("\nA10. 特殊操作: SETNIL / ISNIL / SQRT / NEGF / NOP")
local code_expr10 = native.asm([[
    LOADK  R0, 1
    SETNIL R0
    ISNIL  R1, R0
    LOADKF R2, 9.0
    SQRT   R3, R2
    LOADKF R4, 5.0
    NEGF   R5, R4
    NOP
    RET R1, 5
]])
local nv_a10 = native.new(code_expr10, 8)
local isnil, sqrtv, nilval, negfv, nil2 = native.call(nv_a10)
check(isnil == 1, "ISNIL: nil → " .. tostring(isnil))
check(math.abs(sqrtv - 3.0) < 0.001, "SQRT: sqrt(9) = " .. tostring(sqrtv))
check(math.abs(negfv + 5.0) < 0.001, "NEGF: -5.0 = " .. tostring(negfv))


-- ==================================================================
-- Part B: native.asm() 标签系统
-- ==================================================================
print([[
┌──────────────────────────────────────────────────────────┐
│ B. native.asm() — 标签系统                                │
└──────────────────────────────────────────────────────────┘
]])

-- B1: 基本标签跳转
print("B1. 基本标签: JMP .label")
local code_label1 = native.asm([[
    R0 = 1
    JMP .skip
    R0 = 999
.skip:
    RET R0, 1
]])
local nv_b1 = native.new(code_label1, 4)
r = native.call(nv_b1)
check(r == 1, "JMP .skip 跳过中间, R0 保持 = " .. tostring(r))

-- B2: 条件跳转 JT / JF 结合标签 — while 循环 1+2+...+10=55
print("\nB2. 标签 + 条件跳转: while 循环 1+2+...+10=55")
local code_label2 = native.asm([[
    R0 = 0
    R1 = 11
    R2 = 1
    R3 = 1
.loop:
    R4 = R2 < R1
    JF R4, .done
    R0 = R0 + R2
    R2 = R2 + R3
    JMP .loop
.done:
    RET R0, 1
]])
local nv_b2 = native.new(code_label2, 8)
r = native.call(nv_b2)
check(r == 55, "标签循环: 1..10 累加 = " .. tostring(r))

-- B3: if-else 用标签实现
print("\nB3. 标签 if-else: x=5, if x<10 then 100 else 0")
local code_label3 = native.asm([[
    R0 = 5
    R1 = 10
    R2 = R0 < R1
    JT R2, .then_branch
    R3 = 0
    JMP .endif
.then_branch:
    R3 = 100
.endif:
    RET R3, 1
]])
local nv_b3 = native.new(code_label3, 8)
r = native.call(nv_b3)
check(r == 100, "5<10 走 then → " .. tostring(r))

-- B4: 斐波那契 fib(20)=6765 (使用标签)
print("\nB4. 斐波那契 fib(20)=6765 (标签版本)")
local code_label4 = native.asm([[
    R0 = 0
    R1 = 1
    R2 = 21
    R6 = 1
    R7 = 2
.loop:
    R4 = R7 < R2
    JF R4, .done
    R5 = R0 + R1
    R0 = R1
    R1 = R5
    R7 = R7 + R6
    JMP .loop
.done:
    RET R1, 1
]])
local nv_b4 = native.new(code_label4, 16)
r = native.call(nv_b4)
check(r == 6765, "fib(20) 标签版 = " .. tostring(r))

-- B5: 嵌套标签 — 双层循环 (九九乘法表示例: 计算 9*9 结果)
print("\nB5. 嵌套标签: 双层循环 计算 ∑i=1..9 i*9")
-- i 从 1 到 9, 每次加 i*9
local code_label5 = native.asm([[
    R0 = 0
    R1 = 1
    R2 = 10
    R10 = 9
    R11 = 1
.outer:
    R3 = R1 < R2
    JF R3, .outer_done
    R4 = R1 * R10
    R0 = R0 + R4
    R1 = R1 + R11
    JMP .outer
.outer_done:
    RET R0, 1
]])
local nv_b5 = native.new(code_label5, 16)
r = native.call(nv_b5)
check(r == 405, "∑i=1..9 i*9 = 9+18+...+81 = " .. tostring(r))

-- B6: 阶乘 10! = 3628800 (标签版本)
print("\nB6. 阶乘 10! = 3628800 (标签版本)")
local code_label6 = native.asm([[
    R0 = 1
    R1 = 11
    R2 = 1
    R3 = 1
.loop:
    R4 = R2 < R1
    JF R4, .done
    R0 = R0 * R2
    R2 = R2 + R3
    JMP .loop
.done:
    RET R0, 1
]])
local nv_b6 = native.new(code_label6, 16)
r = native.call(nv_b6)
check(r == 3628800, "10! 标签版 = " .. tostring(r))


-- ==================================================================
-- Part C: native.compile() HLL 编译器 — 高级语言
-- ==================================================================
print([[
┌──────────────────────────────────────────────────────────┐
│ C. native.compile() — HLL 高级语言编译器                   │
└──────────────────────────────────────────────────────────┘
]])

-- C1: 基本别名与赋值
print("C1. 别名定义与基本赋值 (@name = Rnum, @dest = @src)")
local code_compile1 = native.compile([[
    .program  basic_assign
    .regs     8
    @a = R0
    @b = R1
    @result = R2

    @a = 10
    @b = 20
    @result = @a + @b

    ret @result
]])
check(type(code_compile1) == "table", "compile 返回 table")
check(#code_compile1 > 0, "compile 生成了 " .. #code_compile1 .. " 条指令")
print("   反汇编 output:")
for i = 1, #code_compile1 do
    print("     [" .. i .. "] " .. native.disasm(code_compile1[i]))
end
local nv_c1 = native.new(code_compile1, 256)
r = native.call(nv_c1)
check(r == 30, "compile: @a=10; @b=20; @result=@a+@b → " .. tostring(r))

-- C2: 常量赋值 (整数和浮点)
print("\nC2. HLL 常量赋值")
local code_compile2 = native.compile([[
    .program  const_assign
    .regs     8
    @val1 = R0
    @val2 = R1
    @sum   = R2

    @val1 = 100
    @val2 = 200
    @sum   = @val1 + @val2

    ret @sum
]])
local nv_c2 = native.new(code_compile2, 256)
r = native.call(nv_c2)
check(r == 300, "compile: 100 + 200 = " .. tostring(r))

-- C3: 二元运算全集 (+, -, *, /, %, &, |, ^, <<, >>)
print("\nC3. HLL 二元运算: @a op @b")
local code_compile3 = native.compile([[
    .program  binary_ops
    .regs     32
    @x = R0
    @y = R1
    @add = R2
    @sub = R3
    @mul = R4
    @div = R5
    @mod = R6
    @band = R7
    @bor  = R8
    @bxor = R9
    @shl  = R10
    @shr  = R11

    @x = 100
    @y = 7
    @add = @x + @y
    @sub = @x - @y
    @mul = @x * @y
    @div = @x / @y
    @mod = @x % @y
    @band = @x & @y
    @bor  = @x | @y
    @bxor = @x ^ @y
    @shl  = @x << @y
    @shr  = @x >> @y

    ret @add, 10
]])
local nv_c3 = native.new(code_compile3, 256)
local sadd, ssub, smul, sdiv, smod,
      sband, sbor, sbxor, sshl, sshr = native.call(nv_c3)
check(sadd == 107,  "100 + 7   = " .. tostring(sadd))
check(ssub == 93,   "100 - 7   = " .. tostring(ssub))
check(smul == 700,  "100 * 7   = " .. tostring(smul))
check(sdiv == 14,   "100 / 7   = " .. tostring(sdiv))
check(smod == 2,    "100 % 7   = " .. tostring(smod))
check(sband == 4,   "100 & 7   = " .. tostring(sband))
check(sbor == 103,  "100 | 7   = " .. tostring(sbor))
check(sbxor == 99,  "100 ^ 7   = " .. tostring(sbxor))
check(sshl == 12800,"100 << 7  = " .. tostring(sshl))
check(sshr == 0,    "100 >> 7  = " .. tostring(sshr))

-- C4: 比较运算
print("\nC4. HLL 比较运算: <, <=, >, >=, ==, !=")
local code_compile4 = native.compile([[
    .program  compare_ops
    .regs     32
    @a   = R0
    @b   = R1
    @lt  = R2
    @le1 = R3
    @le2 = R4
    @gt  = R5
    @ge  = R6
    @eq  = R7
    @ne  = R8

    @a = 10
    @b = 20
    @lt  = @a < @b
    @le1 = @a <= @b
    @le2 = @a <= @a
    @gt  = @a > @b
    @ge  = @a >= @b
    @eq  = @a == @a
    @ne  = @a != @b

    ret @lt, 7
]])
local nv_c4 = native.new(code_compile4, 256)
local clt, cle1, cle2, cgt, cge, ceq, cne = native.call(nv_c4)
check(clt == 1,  "10 < 20     = " .. tostring(clt))
check(cle1 == 1, "10 <= 20    = " .. tostring(cle1))
check(cle2 == 1, "10 <= 10    = " .. tostring(cle2))
check(cgt == 0,  "10 > 20     = " .. tostring(cgt))
check(cge == 0,  "10 >= 20    = " .. tostring(cge))
check(ceq == 1,  "10 == 10    = " .. tostring(ceq))
check(cne == 1,  "10 != 20    = " .. tostring(cne))

-- C5: 一元运算 — 取负
print("\nC5. HLL 一元运算: @dest = -(@src)")
local code_compile5 = native.compile([[
    .program  negate
    .regs     4
    @val = R0
    @neg = R1

    @val = 42
    @neg = -(@val)

    ret @neg
]])
local nv_c5 = native.new(code_compile5, 256)
r = native.call(nv_c5)
check(r == -42, "neg: -(42) = " .. tostring(r))

-- C6: 一元运算 — 逻辑非
print("\nC6. HLL 一元运算: @dest = !(@src)")
local code_compile6 = native.compile([[
    .program  logical_not
    .regs     8
    @zero   = R0
    @nzero  = R1
    @not0   = R2
    @not42  = R3

    @zero  = 0
    @nzero = 42
    @not0  = !(@zero)
    @not42 = !(@nzero)

    ret @not0, 2
]])
local nv_c6 = native.new(code_compile6, 256)
local cnot0, cnot42 = native.call(nv_c6)
check(cnot0 == 1,   "!0  → " .. tostring(cnot0))
check(cnot42 == 0,  "!42 → " .. tostring(cnot42))

-- C7: while 循环 — 阶乘 10! = 3628800
print("\nC7. HLL while 循环: 阶乘 10! = 3628800")
local code_compile7 = native.compile([[
    .program  factorial
    .regs     16
    @result = R0
    @i      = R1
    @limit  = R2
    @one    = R3

    @result = 1
    @i      = 1
    @limit  = 11
    @one    = 1

    while @i < @limit
        @result = @result * @i
        @i = @i + @one
    end

    ret @result
]])
local nv_c7 = native.new(code_compile7, 256)
r = native.call(nv_c7)
check(r == 3628800, "HLL while: 10! = " .. tostring(r))

-- C8: while 循环 — 累加 1+2+...+100 = 5050
print("\nC8. HLL while: 累加 1..100 = 5050")
local code_compile8 = native.compile([[
    .program  sum_1_to_100
    .regs     16
    @sum   = R0
    @i     = R1
    @limit = R2
    @one   = R3

    @sum   = 0
    @i     = 1
    @limit = 101
    @one   = 1

    while @i < @limit
        @sum = @sum + @i
        @i = @i + @one
    end

    ret @sum
]])
local nv_c8 = native.new(code_compile8, 256)
r = native.call(nv_c8)
check(r == 5050, "HLL while: 1..100 累加 = " .. tostring(r))

-- C9: while 循环 — 斐波那契 fib(20) = 6765
print("\nC9. HLL while: 斐波那契 fib(20) = 6765")
local code_compile9 = native.compile([[
    .program  fibonacci_hll
    .regs     16
    @a     = R0
    @b     = R1
    @i     = R2
    @limit = R3
    @one   = R4
    @c     = R5

    @a     = 0
    @b     = 1
    @i     = 2
    @limit = 21
    @one   = 1

    while @i < @limit
        @c = @a + @b
        @a = @b
        @b = @c
        @i = @i + @one
    end

    ret @b
]])
local nv_c9 = native.new(code_compile9, 256)
r = native.call(nv_c9)
check(r == 6765, "HLL while: fib(20) = " .. tostring(r))

-- C10: if-else 条件分支
print("\nC10. HLL if-else: 条件分支")
-- x=15, if x<10 → 100 else → 0
local code_compile10 = native.compile([[
    .program  if_else_demo
    .regs     8
    @x      = R0
    @limit  = R1
    @result = R2

    @x     = 15
    @limit = 10

    if @x < @limit
        @result = 100
    else
        @result = 0
    end

    ret @result
]])
local nv_c10 = native.new(code_compile10, 256)
r = native.call(nv_c10)
check(r == 0, "HLL if-else: 15<10 为假 → else → " .. tostring(r))

-- 第二个分支测试: x=5, if x<10 → 100 else → 0
local code_compile10b = native.compile([[
    .program  if_else_demo2
    .regs     8
    @x      = R0
    @limit  = R1
    @result = R2

    @x     = 5
    @limit = 10

    if @x < @limit
        @result = 100
    else
        @result = 0
    end

    ret @result
]])
local nv_c10b = native.new(code_compile10b, 256)
r = native.call(nv_c10b)
check(r == 100, "HLL if-else: 5<10 为真 → then → " .. tostring(r))

-- C11: if (无 else) 条件分支
print("\nC11. HLL if (无 else): 条件分支")
local code_compile11 = native.compile([[
    .program  if_only_demo
    .regs     8
    @x      = R0
    @limit  = R1
    @result = R2

    @result = -1
    @x      = 15
    @limit  = 10

    if @x < @limit
        @result = 100
    end

    ret @result
]])
local nv_c11 = native.new(code_compile11, 256)
r = native.call(nv_c11)
check(r == -1, "HLL if-only: 15<10 假, result 保持 = " .. tostring(r))

local code_compile11b = native.compile([[
    .program  if_only_demo2
    .regs     8
    @x      = R0
    @limit  = R1
    @result = R2

    @result = -1
    @x      = 5
    @limit  = 10

    if @x < @limit
        @result = 100
    end

    ret @result
]])
local nv_c11b = native.new(code_compile11b, 256)
r = native.call(nv_c11b)
check(r == 100, "HLL if-only: 5<10 真, result=100 → " .. tostring(r))

-- C12: ret 多返回值
print("\nC12. HLL ret 多返回值")
local code_compile12 = native.compile([[
    .program  multi_ret
    .regs     8
    @a = R0
    @b = R1
    @c = R2

    @a = 10
    @b = 20
    @c = 30

    ret @a, 3
]])
local nv_c12 = native.new(code_compile12, 256)
local ra, rb, rc = native.call(nv_c12)
check(ra == 10 and rb == 20 and rc == 30,
    string.format("HLL ret 多返回值: %d, %d, %d", ra, rb, rc))

-- C13: halt 终止执行
print("\nC13. HLL halt: 提前终止")
local code_compile13 = native.compile([[
    .program  halt_demo
    .regs     4
    @result = R0
    @temp   = R1

    @result = 88
    halt
    @temp   = 99
]])
local nv_c13 = native.new(code_compile13, 4)
r = native.call(nv_c13)
check(r == 88, "HLL halt: 提前终止, result = " .. tostring(r))

-- C14: 嵌套 while (双层循环)
print("\nC14. HLL 嵌套 while: 外层 1..5, 内层 1..3 累加到 sum")
-- sum = Σ(i=1..5) Σ(j=1..3) j = 5 * (1+2+3) = 30
-- 注意: native.compile() 目前不支持嵌套控制流, 这里仅作语法演示
local code_compile14 = native.compile([[
    .program  nested_while
    .regs     32
    @i     = R0
    @sum   = R1
    @ilim  = R2
    @one   = R3
    @j     = R4
    @jlim  = R5

    @i    = 1
    @sum  = 0
    @ilim = 6
    @one  = 1
    @jlim = 4

    while @i < @ilim
        @j = 1
        while @j < @jlim
            @sum = @sum + @j
            @j = @j + @one
        end
        @i = @i + @one
    end

    ret @sum
]])
local nv_c14 = native.new(code_compile14, 32)
r = native.call(nv_c14)
check(r == 30, "HLL 嵌套 while: Σi=1..5 Σj=1..3 j = " .. tostring(r))

-- C15: 判断素数 (HLL 版本)
print("\nC15. HLL 判断素数: 97 是素数, 100 不是")
local code_compile15 = native.compile([[
    .program  is_prime_hll
    .regs     32
    @n       = R0
    @is_prime = R1
    @i       = R2
    @zero    = R3
    @one     = R4
    @two     = R5
    @sqr     = R6
    @nsqrt   = R7
    @q       = R8
    @prod    = R9
    @is_eq   = R10

    @n       = 97
    @is_prime = 1
    @i       = 2
    @zero    = 0
    @one     = 1
    @two     = 2

    @sqr = @i * @i
    while @sqr <= @n
        @q = @n / @i
        @prod = @q * @i
        @is_eq = @prod == @n
        if @is_eq != @zero
            @is_prime = 0
        end
        @i = @i + @one
        @sqr = @i * @i
    end

    ret @is_prime
]])
local nv_c15 = native.new(code_compile15, 32)
r = native.call(nv_c15)
check(r == 1, "HLL 素数判断: 97 是素数 → " .. tostring(r))

local code_compile15b = native.compile([[
    .program  is_prime_hll2
    .regs     32
    @n       = R0
    @is_prime = R1
    @i       = R2
    @zero    = R3
    @one     = R4
    @two     = R5
    @sqr     = R6
    @nsqrt   = R7
    @q       = R8
    @prod    = R9
    @is_eq   = R10

    @n       = 100
    @is_prime = 1
    @i       = 2
    @zero    = 0
    @one     = 1
    @two     = 2

    @sqr = @i * @i
    while @sqr <= @n
        @q = @n / @i
        @prod = @q * @i
        @is_eq = @prod == @n
        if @is_eq != @zero
            @is_prime = 0
        end
        @i = @i + @one
        @sqr = @i * @i
    end

    ret @is_prime
]])
local nv_c15b = native.new(code_compile15b, 256)
r = native.call(nv_c15b)
check(r == 0, "HLL 素数判断: 100 不是素数 → " .. tostring(r))

-- C16: 计算平方和 Σi² (i=1..10) = 385
print("\nC16. HLL 平方和: Σi=1..10 i² = 385")
local code_compile16 = native.compile([[
    .program  sum_of_squares
    .regs     16
    @sum   = R0
    @i     = R1
    @lim   = R2
    @one   = R3
    @sq    = R4

    @sum = 0
    @i   = 1
    @lim = 11
    @one = 1

    while @i < @lim
        @sq = @i * @i
        @sum = @sum + @sq
        @i = @i + @one
    end

    ret @sum
]])
local nv_c16 = native.new(code_compile16, 16)
r = native.call(nv_c16)
check(r == 385, "HLL 平方和 1²+...+10² = " .. tostring(r))


-- ==================================================================
-- Part D: 混合使用 — asm 表达式 + compile 对比
-- ==================================================================
print([[
┌──────────────────────────────────────────────────────────┐
│ D. 混合演示: asm 表达式 vs compile HLL 对比                │
└──────────────────────────────────────────────────────────┘
]])

print("\nD1. 同一算法两种写法: 计算 1*2*3*...*10 = 3628800")

-- 方法1: native.asm 表达式 + 标签
local code_asm_fact = native.asm([[
    R0 = 1
    R1 = 11
    R2 = 1
    R3 = 1
.loop:
    R4 = R2 < R1
    JF R4, .done
    R0 = R0 * R2
    R2 = R2 + R3
    JMP .loop
.done:
    RET R0, 1
]])
local nv_asm = native.new(code_asm_fact, 8)
local r_asm = native.call(nv_asm)

-- 方法2: native.compile HLL
local code_hll_fact = native.compile([[
    .regs 16
    @r = R0
    @i = R1
    @lim = R2
    @one = R3
    @r = 1
    @i = 1
    @lim = 11
    @one = 1
    while @i < @lim
        @r = @r * @i
        @i = @i + @one
    end
    ret @r
]])
local nv_hll = native.new(code_hll_fact, 16)
local r_hll = native.call(nv_hll)

check(r_asm == 3628800, "asm标签版: 10! = " .. tostring(r_asm))
check(r_hll == 3628800, "compile版: 10! = " .. tostring(r_hll))
print("   两者结果一致!")


-- ==================================================================
-- 测试结果汇总
-- ==================================================================
print([[
╔══════════════════════════════════════════════════════════╗
║                    测试结果汇总                           ║
╚══════════════════════════════════════════════════════════╝
]])
print(string.format("通过: %d   失败: %d   总计: %d", PASS, FAIL, PASS + FAIL))
print("========================================================")

if FAIL > 0 then
    os.exit(1)
end