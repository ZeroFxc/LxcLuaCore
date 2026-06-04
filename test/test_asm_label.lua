--[[
test_asm_label.lua — vm.asm 标签(label)功能测试

测试 :name 标签定义和 JMP/JT/JF 标签引用功能
注意: 标签定义行不算作指令，不占 PC
--]]

local vm = require("vmcustom")

local PASS, FAIL = 0, 0
local function check(cond, msg)
    if cond then PASS = PASS + 1; print(string.format("  [OK]   %s", msg))
    else          FAIL = FAIL + 1; print(string.format("  [ERR]  %s", msg))
    end
end

local function section(title)
    print(string.format("\n%s", title))
    print(string.rep("-", #title))
end

-- ==================================================================
-- 1. 基本标签定义 + JMP 跳转
-- ==================================================================
section("1. 基本标签定义 + JMP 无条件跳转")

-- 标签 :skip 位于 LOADK R1,42 之前，不算指令
-- pc=1: LOADK R1, 1
-- pc=2: JMP   :skip → :skip 在 pc=4
-- pc=3: LOADK R1, 99  (被跳过)
-- pc=4: LOADK R1, 42  (:skip)
-- pc=5: HALT
local bc_jmp = vm.asm([[
    LOADK R1, 1
    JMP   :skip
    LOADK R1, 99
    :skip
    LOADK R1, 42
    HALT
]])

check(#bc_jmp == 5, string.format("JMP :label → 5 条指令 (实际 %d)", #bc_jmp))
check(bc_jmp[2].k == 1, string.format("JMP :skip k=%d (期望 1)", bc_jmp[2].k))
check(bc_jmp[2].op == vm.MINI_JMP, "JMP opcode 正确")

local ok = pcall(vm.execmini, bc_jmp, 4)
check(ok, "JMP :label 执行成功")


-- ==================================================================
-- 2. JT/JF 条件跳转 + 标签
-- ==================================================================
section("2. JT/JF 条件跳转 + 标签引用")

-- :there 在 pc=4, JT 在 pc=2, k=4-2-1=1
local bc_jt = vm.asm([[
    LOADK R1, 1
    JT    R1, :there
    LOADK R2, 99
    :there
    LOADK R2, 10
    HALT
]])

check(#bc_jt == 5, string.format("JT :label → 5 条指令 (实际 %d)", #bc_jt))
check(bc_jt[2].k == 1, string.format("JT :there k=%d (期望 1)", bc_jt[2].k))
check(bc_jt[2].op == vm.MINI_JT, "JT opcode 正确")
check(bc_jt[2].a == 1, "JT 寄存器 a=1 正确")

ok = pcall(vm.execmini, bc_jt, 4)
check(ok, "JT :label 执行成功")


-- :jump 在 pc=4, JF 在 pc=2, k=4-2-1=1
local bc_jf = vm.asm([[
    LOADK R1, 0
    JF    R1, :jump
    LOADK R3, 99
    :jump
    LOADK R3, 30
    HALT
]])

check(bc_jf[2].op == vm.MINI_JF, "JF opcode 正确")
check(bc_jf[2].k == 1, string.format("JF :jump k=%d (期望 1)", bc_jf[2].k))

ok = pcall(vm.execmini, bc_jf, 4)
check(ok, "JF :label 执行成功")


-- ==================================================================
-- 3. 多重标签 + 多重跳转
-- ==================================================================
section("3. 多重标签 + 多重跳转 (向前 & 向后)")

-- pc=1: LOADK R1, 3
-- pc=2: LOADK R2, 1  (:loop)
-- pc=3: SUB  R1,R1,R2
-- pc=4: JT   R1,:loop → :loop=pc2, k=2-4-1=-3
-- pc=5: LOADK R3,100  (:end)
-- pc=6: HALT
local bc_multi = vm.asm([[
    LOADK R1, 3
    :loop
    LOADK R2, 1
    SUB  R1, R1, R2
    JT   R1, :loop
    :end
    LOADK R3, 100
    HALT
]])

check(#bc_multi == 6, string.format("多重标签 → 6 条指令 (实际 %d)", #bc_multi))
check(bc_multi[4].k == -3, string.format("向后跳转 k=%d (期望 -3)", bc_multi[4].k))
check(bc_multi[4].op == vm.MINI_JT, "多重跳转 opcode 正确")

ok = pcall(vm.execmini, bc_multi, 8)
check(ok, "多重标签跳转执行成功")


-- ==================================================================
-- 4. 标签版阶乘 — 5! = 120
-- ==================================================================
section("4. 标签版阶乘 — 5! = 120")

-- pc= 1: LOADK R1, 1
-- pc= 2: LOADK R2, 5
-- pc= 3: LOADK R3, 1
-- pc= 4: LOADK R6, 1
-- pc= 5: EQ    R4, R2,R3  (:loop_top)
-- pc= 6: JT    R4, :done  → :done=pc10, k=10-6-1=3
-- pc= 7: MUL   R1,R1,R2
-- pc= 8: SUB   R2,R2,R6
-- pc= 9: JMP   :loop_top  → :loop_top=pc5, k=5-9-1=-5
-- pc=10: PRINT R1         (:done)
-- pc=11: HALT
local bc_fact = vm.asm([[
    LOADK R1, 1
    LOADK R2, 5
    LOADK R3, 1
    LOADK R6, 1

    :loop_top
    EQ    R4, R2, R3
    JT    R4, :done

    MUL   R1, R1, R2
    SUB   R2, R2, R6
    JMP   :loop_top

    :done
    PRINT R1
    HALT
]])

check(#bc_fact == 11, string.format("标签阶乘 → 11 条指令 (实际 %d)", #bc_fact))
check(bc_fact[6].k == 3, string.format("JT :done k=%d (期望 3)", bc_fact[6].k))
check(bc_fact[9].k == -5, string.format("JMP :loop_top k=%d (期望 -5)", bc_fact[9].k))

ok = pcall(vm.execmini, bc_fact, 10)
check(ok, "标签阶乘执行成功 → 预期输出 120.0")


-- ==================================================================
-- 5. 标签版倒计时循环 5→1
-- ==================================================================
section("5. 标签版倒计时循环 (5→1)")

-- pc=1: LOADK R1, 5
-- pc=2: LOADK R2, 0
-- pc=3: LOADK R3, 1
-- pc=4: EQ R4, R1, R2  (:check)
-- pc=5: JF R4, :body  → :body=pc7, k=7-5-1=1
-- pc=6: JMP :exit     → :exit=pc9, k=9-6-1=2
-- pc=7: PRINT R1      (:body)
-- pc=8: SUB R1, R1, R3
-- pc=9: JMP :check    → :check=pc4, k=4-9-1=-6
-- pc=10:HALT          (:exit)
local bc_countdown = vm.asm([[
    LOADK R1, 5
    LOADK R2, 0
    LOADK R3, 1

    :check
    EQ    R4, R1, R2
    JF    R4, :body
    JMP   :exit

    :body
    PRINT R1
    SUB   R1, R1, R3
    JMP   :check

    :exit
    HALT
]])

check(#bc_countdown == 10, string.format("倒计时 → 10 条指令 (实际 %d)", #bc_countdown))

ok = pcall(vm.execmini, bc_countdown, 8)
check(ok, "标签倒计时执行成功 → 预期输出 5 / 4 / 3 / 2 / 1")


-- ==================================================================
-- 6. 标签 + mcall 组合使用
-- ==================================================================
section("6. 标签 + mcall 函数式调用")

-- pc=1: ADD R3, R1, R2
-- pc=2: LOADK R5, 10
-- pc=3: LT  R6, R5, R3
-- pc=4: JT  R6, :skip_double → :skip_double=pc7, k=7-4-1=2
-- pc=5: LOADK R4, 2
-- pc=6: MUL R3, R3, R4
-- pc=7: MOV R1, R3     (:skip_double)
-- pc=8: RET
local bc_double_sum = vm.asm([[
    ADD   R3, R1, R2
    LOADK R5, 10
    LT    R6, R5, R3
    JT    R6, :skip_double
    LOADK R4, 2
    MUL   R3, R3, R4
    :skip_double
    MOV   R1, R3
    RET
]])

check(#bc_double_sum == 8, string.format("标签+mcall → 8 条指令 (实际 %d)", #bc_double_sum))

local r = vm.mcall(bc_double_sum, 8, 2, 3)
check(type(r) == "number", string.format("mcall(2,3) → %s (期望 number)", tostring(r)))

local r2 = vm.mcall(bc_double_sum, 8, 30, 20)
check(type(r2) == "number", string.format("mcall(30,20) → %s (期望 number)", tostring(r2)))


-- ==================================================================
-- 7. 数字偏移量向后兼容
-- ==================================================================
section("7. 数字偏移量向后兼容 (旧语法仍可工作)")

local bc_numeric = vm.asm([[
    LOADK R1, 1
    JMP   1
    LOADK R1, 99
    LOADK R2, 42
    HALT
]])

check(#bc_numeric == 5, string.format("数字偏移 → 5 条指令 (实际 %d)", #bc_numeric))
check(bc_numeric[2].k == 1, string.format("数字 JMP 1 → k=%d", bc_numeric[2].k))

ok = pcall(vm.execmini, bc_numeric, 4)
check(ok, "数字偏移 JMP 执行成功")


-- ==================================================================
-- 8. 混合使用: 数字偏移 + 标签
-- ==================================================================
section("8. 混合使用: 数字 JMP + 标签 JT/JF")

-- pc=1: LOADK R1, 3
-- pc=2: LOADK R2, 1   (:again)
-- pc=3: SUB R1, R1, R2
-- pc=4: JT  R1, :again → :again=pc2, k=2-4-1=-3
-- pc=5: JMP 2           (数字跳转, 跳过2条)
-- pc=6: LOADK R3, 99    (被跳过)
-- pc=7: LOADK R3, 77    (被跳过)
-- pc=8: LOADK R3, 42
-- pc=9: HALT
local bc_mixed = vm.asm([[
    LOADK R1, 3
    :again
    LOADK R2, 1
    SUB  R1, R1, R2
    JT   R1, :again
    JMP  2
    LOADK R3, 99
    LOADK R3, 77
    LOADK R3, 42
    HALT
]])

check(#bc_mixed == 9, string.format("混合语法 → 9 条指令 (实际 %d)", #bc_mixed))
check(bc_mixed[4].k == -3, string.format("JT :again k=%d (期望 -3)", bc_mixed[4].k))

ok = pcall(vm.execmini, bc_mixed, 8)
check(ok, "混合语法执行成功")


-- ==================================================================
-- 9. 错误处理: 未定义标签
-- ==================================================================
section("9. 错误处理: 未定义标签")

local ok_status, err = pcall(vm.asm, [[
    LOADK R1, 1
    JMP   :not_exist
    HALT
]])
check(not ok_status and err and err:match("undefined label"),
    string.format("未定义标签报错 → %s", tostring(err)))


-- ==================================================================
-- 10. 错误处理: 重复标签
-- ==================================================================
section("10. 错误处理: 重复标签")

ok_status, err = pcall(vm.asm, [[
    :dup
    LOADK R1, 1
    :dup
    HALT
]])
check(not ok_status and err and err:match("duplicate label"),
    string.format("重复标签报错 → %s", tostring(err)))


-- ==================================================================
-- 11. 空标签名报错
-- ==================================================================
section("11. 错误处理: 空标签名")

ok_status, err = pcall(vm.asm, [[
    :
    LOADK R1, 1
    HALT
]])
check(not ok_status and err and err:match("empty label"),
    string.format("空标签名报错 → %s", tostring(err)))


-- ==================================================================
-- 12. 标签 + 自定义 opcode 混合
-- ==================================================================
section("12. 标签 + 自定义 opcode 混合")

vm.setuserminiop(21, function(vmctx, a, b, c, k)
    local v = vmctx:getreg(a)
    if type(v) == "number" then
        vmctx:setreg(a, v * v)
    end
    return 1
end)

-- pc=1: LOADK R1, 2
-- pc=2: 21    R1       (POW2: 2→4)
-- pc=3: JT    R1,:big  → :big=pc5, k=5-3-1=1
-- pc=4: LOADK R2, 0    (被跳过)
-- pc=5: 21    R1       (:big, POW2: 4→16)
-- pc=6: HALT
local bc_custom_label = vm.asm([[
    LOADK R1, 2
    21     R1
    JT     R1, :big
    LOADK R2, 0
    :big
    21     R1
    HALT
]])

check(#bc_custom_label == 6, string.format("自定义+标签 → 6 条指令 (实际 %d)", #bc_custom_label))
check(bc_custom_label[3].k == 1, string.format("JT :big k=%d (期望 1)", bc_custom_label[3].k))

ok = pcall(vm.execmini, bc_custom_label, 8)
check(ok, "自定义 opcode + 标签执行成功")


-- ==================================================================
-- 13. 标签定义在任意位置
-- ==================================================================
section("13. 标签位置灵活性")

-- pc=1: LOADK R1, 5
-- pc=2: LOADK R2, 1   (:start)
-- pc=3: SUB R1,R1,R2
-- pc=4: JT  R1,:mid   → :mid=pc6, k=6-4-1=1
-- pc=5: JMP :end      → :end=pc7, k=7-5-1=1
-- pc=6: JMP :start    (:mid) → :start=pc2, k=2-6-1=-5
-- pc=7: HALT          (:end)
local bc_pos = vm.asm([[
    LOADK R1, 5
    :start
    LOADK R2, 1
    SUB  R1, R1, R2
    JT   R1, :mid
    JMP   :end
    :mid
    JMP   :start
    :end
    HALT
]])

check(#bc_pos == 7, string.format("多位置标签 → 7 条指令 (实际 %d)", #bc_pos))
check(bc_pos[4].k == 1, string.format("JT :mid k=%d (期望 1)", bc_pos[4].k))
check(bc_pos[5].k == 1, string.format("JMP :end k=%d (期望 1)", bc_pos[5].k))
check(bc_pos[6].k == -5, string.format("JMP :start k=%d (期望 -5)", bc_pos[6].k))

ok = pcall(vm.execmini, bc_pos, 8)
check(ok, "多位置标签执行成功")


-- ==================================================================
-- 14. 空白程序
-- ==================================================================
section("14. 空白程序 (只有 HALT)")

local bc_empty = vm.asm([[
    HALT
]])
check(#bc_empty == 1, string.format("只有 HALT → 1 条指令 (实际 %d)", #bc_empty))

ok = pcall(vm.execmini, bc_empty, 4)
check(ok, "空白程序执行成功")


-- ==================================================================
-- 15. 带注释 + 标签
-- ==================================================================
section("15. 注释 + 标签混合")

-- pc=1: LOADK R1, 10
-- pc=2: LOADK R2, 1   (:loop)
-- pc=3: SUB R1, R1, R2
-- pc=4: JT  R1, :loop → :loop=pc2, k=2-4-1=-3
-- pc=5: HALT
local bc_comment = vm.asm([[
    ; 初始化
    LOADK R1, 10
    # 主循环
    :loop
    LOADK R2, 1
    SUB  R1, R1, R2
    JT   R1, :loop
    ; 结束
    HALT
]])
check(#bc_comment == 5, string.format("注释+标签 → 5 条指令 (实际 %d)", #bc_comment))

ok = pcall(vm.execmini, bc_comment, 4)
check(ok, "注释+标签执行成功")


-- ==================================================================
-- 结果汇总
-- ==================================================================
print("\n" .. string.rep("=", 56))
print(string.format("  vm.asm 标签测试完毕: 通过 %d, 失败 %d, 总计 %d",
    PASS, FAIL, PASS + FAIL))
print(string.rep("=", 56))

if FAIL > 0 then
    os.exit(1)
end