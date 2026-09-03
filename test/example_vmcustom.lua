--[[
example_vmcustom.lua — 自定义 opcode 扩展系统 / MiniVM 完整教学示例

本文件演示 lvmustom.c 的所有功能模块：
  1. MiniVM 基础指令 (execmini) — MOV / LOADK / ADD / SUB / MUL / DIV / EQ / LT
  2. 控制流 — JMP / JT / JF / HALT / RET
  3. CALL — 在 MiniVM 中调用 Lua 函数
  4. vm.mcall() — 函数式调用，传参与返回值
  5. vm.asm() — 助记符汇编器 (NOP / MOV / LOADK / ADD / SUB / MUL / DIV / EQ / LT / JMP / JT / JF / CALL / RET / PRINT / HALT)
  6. vmctx 上下文 — 在自定义 opcode 中 getreg / setreg 操作 MiniVM 寄存器
  7. 主 VM OP_CUSTOM 系统 — setop / getop / delop / listops / opcount / makeinst / getinstop
  8. vm.compile() — 原始数字格式编译器
--]]

local vm = require("vmcustom")

local PASS, FAIL = 0, 0
local function check(cond, msg)
    if cond then PASS = PASS + 1; print("[OK]  " .. msg)
    else          FAIL = FAIL + 1; print("[ERR] " .. msg)
    end
end

-- ==================================================================
-- 1. 模块常量一览
-- ==================================================================
print([[
        自定义 opcode 扩展系统 — MiniVM 示例
========================================================

1. 模块常量]]

)

check(vm.MAX_CUSTOM_OPS  == 256, "MAX_CUSTOM_OPS  == 256  (主VM自定义op总数)")
check(vm.MINIVM_USER_BASE == 16, "MINIVM_USER_BASE == 16   (MiniVM用户op起始)")
check(vm.MINIVM_MAX_OPS   == 128, "MINIVM_MAX_OPS   == 128  (MiniVM opcode上限)")

print("内置指令常量:")
check(vm.MINI_NOP   == 0,  "MINI_NOP   = 0")
check(vm.MINI_MOV   == 1,  "MINI_MOV   = 1")
check(vm.MINI_LOADK == 2,  "MINI_LOADK = 2")
check(vm.MINI_ADD   == 3,  "MINI_ADD   = 3")
check(vm.MINI_SUB   == 4,  "MINI_SUB   = 4")
check(vm.MINI_MUL   == 5,  "MINI_MUL   = 5")
check(vm.MINI_DIV   == 6,  "MINI_DIV   = 6")
check(vm.MINI_EQ    == 7,  "MINI_EQ    = 7")
check(vm.MINI_LT    == 8,  "MINI_LT    = 8")
check(vm.MINI_JMP   == 9,  "MINI_JMP   = 9")
check(vm.MINI_JT    == 10, "MINI_JT    = 10")
check(vm.MINI_JF    == 11, "MINI_JF    = 11")
check(vm.MINI_CALL  == 12, "MINI_CALL  = 12")
check(vm.MINI_RET   == 13, "MINI_RET   = 13")
check(vm.MINI_PRINT == 14, "MINI_PRINT = 14")
check(vm.MINI_HALT  == 15, "MINI_HALT  = 15")


-- ==================================================================
-- 2. execmini — 基础算术与数据移动
-- ==================================================================
print("\n2. execmini — 基础算术运算 (LOADK / MOV / ADD / SUB / MUL / DIV)")

-- 一条指令就是一个 {op, a, b, c, k} 表。
-- a/b/c 对应操作数寄存器编号 (R1 = 1)，k 用于跳转偏移量。
-- 执行: LOADK R1, 10; LOADK R2, 3; ADD R3, R1, R2
local bc_arith = {
    { op = vm.MINI_LOADK, a = 1, b = 10, c = 0  },
    { op = vm.MINI_LOADK, a = 2, b = 3,  c = 0  },
    { op = vm.MINI_ADD,   a = 3, b = 1,  c = 2  },
    { op = vm.MINI_SUB,   a = 4, b = 1,  c = 2  },
    { op = vm.MINI_MUL,   a = 5, b = 1,  c = 2  },
    { op = vm.MINI_DIV,   a = 6, b = 1,  c = 2  },
    { op = vm.MINI_HALT },
}
local ok = pcall(vm.execmini, bc_arith, 8)
check(ok, "算术运算执行成功 (nregs=8)")

-- ==================================================================
-- 3. execmini — 比较与跳转
-- ==================================================================
print("\n3. execmini — 比较 (EQ/LT) 与条件跳转 (JT/JF)")

-- R1=5, R2=10
-- EQ R3, R1, R2 → 5==10 → R3=false → JF 跳过 LOADK → LT
-- LT R4, R1, R2 → 5<10  → R4=true  → JT 跳过 LOADK → HALT
-- 跳转公式: pc += k + 1, 所以 k = 目标位置 - 当前位置 - 1
local bc_jump = {
    { op = vm.MINI_LOADK, a = 1, b = 5,  },          -- pos 1
    { op = vm.MINI_LOADK, a = 2, b = 10 },           -- pos 2
    { op = vm.MINI_EQ,    a = 3, b = 1, c = 2 },     -- pos 3
    { op = vm.MINI_JF,    a = 3, k = 1 },            -- pos 4: R3=false→跳pos6(LT); k=6-4-1=1
    { op = vm.MINI_LOADK, a = 99, b = 99 },           -- pos 5: 被跳过
    { op = vm.MINI_LT,    a = 4, b = 1, c = 2 },     -- pos 6
    { op = vm.MINI_JT,    a = 4, k = 1 },            -- pos 7: R4=true→跳pos9(HALT); k=9-7-1=1
    { op = vm.MINI_LOADK, a = 99, b = 99 },           -- pos 8: 被跳过
    { op = vm.MINI_HALT },                           -- pos 9
}
ok = pcall(vm.execmini, bc_jump, 16)
check(ok, "条件跳转执行成功")

-- ==================================================================
-- 4. execmini — JMP 无条件跳转 / HALT / RET
-- ==================================================================
print("\n4. execmini — 无条件跳转 (JMP) 与终止 (HALT / RET)")

-- JMP k: 向前跳转 k 条指令 (k=0→下一条, k=-1→上一条，即原地死循环)
-- HALT: 终止并返回 -2
-- RET:  终止并返回 -1
local bc_loop = {
    { op = vm.MINI_LOADK, a = 1, b = 1 },
    { op = vm.MINI_JMP,   k = 2 },        -- 跳过死循环 NOP
    { op = vm.MINI_NOP },
    { op = vm.MINI_JMP,   k = -1 },
    { op = vm.MINI_HALT },
}
ok = pcall(vm.execmini, bc_loop, 4)
check(ok, "JMP → HALT 执行成功")

-- ==================================================================
-- 5. vm.mcall — 函数式调用 MiniVM
-- ==================================================================
print("\n5. vm.mcall — 函数式调用 (传入参数 → MiniVM 处理 → 返回结果)")

-- 实现加法: R1 = 参数1, R2 = 参数2 → ADD R3 = R1+R2 → 返回 R1..R3
local bc_add = {
    { op = vm.MINI_ADD,  a = 3, b = 1, c = 2 },
    { op = vm.MINI_RET },
}

-- mcall(bytecode, nregs, arg1, arg2, ...)
-- 参数写入 R1, R2, ...; 返回值取自 R1, R2, ... (直到第一个 nil)
local r1, r2 = vm.mcall(bc_add, 8, 100, 20)
check(r1 == 100 and r2 == 20,
    string.format("mcall: 100+20 参数/返回 → R1=%s, R2=%s", tostring(r1), tostring(r2)))

local r1, r2, r3 = vm.mcall(bc_add, 8, 7, 3)
check(type(r3) == "number",
    string.format("mcall: 7+3 → R3=%s (ADD 结果)", tostring(r3)))


-- ==================================================================
-- 6. vm.asm — 助记符汇编器
-- ==================================================================
print("\n6. vm.asm — 助记符汇编器")

-- vm.asm 支持文本格式的汇编语言：
--   LOADK R1, 10       → 两操作数 (a=1, b=10)
--   ADD   R3, R1, R2   → 三操作数 (a=3, b=1, c=2)
--   JT    R1, 跳偏移    → 一操作数+跳转 (a=1, k=跳偏移)
--   JMP   跳偏移        → 零操作数+跳转 (k=跳偏移)
--   RET                → 零操作数
--   # 注释
--   ; 注释

-- 示例: 计算 10 * 3，打印结果
local bc_asm = vm.asm([[
    LOADK R1, 10
    LOADK R2, 3
    MUL   R3, R1, R2
    PRINT R3
    HALT
]])
check(type(bc_asm) == "table", "asm 返回 table")
check(#bc_asm == 5, "asm 生成了 5 条指令")
ok = pcall(vm.execmini, bc_asm, 8)
check(ok, "asm 编译的 bytecode 执行成功")

-- 带跳转的汇编 (循环计数 5→0)
-- 跳转公式: pc += k + 1, 所以 k = 目标位置 - 当前位置 - 1
local bc_loop_asm = vm.asm([[
    ; 初始化计数器和上限
    LOADK R1, 5       ; pos 1: R1 = 当前计数
    LOADK R2, 0       ; pos 2: R2 = 0 (终止条件)
    LOADK R3, 1       ; pos 3: R3 = 1 (递减步长)

    ; 循环头: 检查 R1 == 0 ?
    EQ    R4, R1, R2  ; pos 4: R4 = (R1 == R2)
    JF    R4, 2       ; pos 5: R4==false→跳到pos7(PRINT); k=7-5-1=2

    ; R4==true(R1==0)时, 不跳转, 执行此JMP跳出循环
    JMP   3           ; pos 6: 跳到pos10(HALT); k=10-6-1=3

    ; 循环体
    PRINT R1          ; pos 7: 打印当前值
    SUB   R1, R1, R3  ; pos 8: R1 = R1 - 1
    JMP   -6          ; pos 9: 跳回pos4(EQ); k=4-9-1=-6

    HALT              ; pos 10
]])
ok = pcall(vm.execmini, bc_loop_asm, 8)
check(ok, "asm 循环 bytecode 执行成功")


-- ==================================================================
-- 7. 用户自定义 MiniVM 指令 + vmctx 上下文
-- ==================================================================
print("\n7. 用户自定义 MiniVM 指令 + vmctx 上下文")

-- 7.1 自定义 opcode 16: INC — 将指定寄存器的值 +1
--     函数签名: function(vmctx, a, b, c, k) → 返回跳转偏移
--     vmctx 方法: vmctx:getreg(n), vmctx:setreg(n, val), vmctx.nregs
vm.setuserminiop(16, function(vmctx, a, b, c, k)
    local val = vmctx:getreg(a)
    if val ~= nil then
        vmctx:setreg(a, val + 1)
    end
    return 1    -- 前进 1 条指令
end)

-- 7.2 自定义 opcode 17: DEC — 递减
vm.setuserminiop(17, function(vmctx, a, b, c, k)
    local val = vmctx:getreg(a)
    if val ~= nil then
        vmctx:setreg(a, val - 1)
    end
    return 1
end)

-- 7.3 自定义 opcode 18: SUM — 将 R[b] 到 R[c] 区间求和存入 R[a]
vm.setuserminiop(18, function(vmctx, a, b, c, k)
    local sum = 0
    for i = b, c do
        local v = vmctx:getreg(i)
        if type(v) == "number" then
            sum = sum + v
        end
    end
    vmctx:setreg(a, sum)
    return 1
end)

-- 7.4 自定义 opcode 19: CPY — 将 R[b] 复制到 R[a]
vm.setuserminiop(19, function(vmctx, a, b, c, k)
    local val = vmctx:getreg(b)
    if val ~= nil then
        vmctx:setreg(a, val)
    end
    return 1
end)

-- 测试自定义指令
local bc_custom = {
    { op = vm.MINI_LOADK, a = 1, b = 10 },
    { op = 16,            a = 1             },   -- INC R1 → 11
    { op = 16,            a = 1             },   -- INC R1 → 12
    { op = 17,            a = 1             },   -- DEC R1 → 11
    { op = vm.MINI_LOADK, a = 2, b = 100 },
    { op = vm.MINI_LOADK, a = 3, b = 200 },
    { op = vm.MINI_LOADK, a = 4, b = 50  },
    { op = 18,            a = 10, b = 1, c = 4 },  -- SUM R[1..4] → R10
    { op = 19,            a = 11, b = 10        },  -- CPY R11 = R10
    { op = vm.MINI_HALT },
}
ok = pcall(vm.execmini, bc_custom, 16)
check(ok, "自定义 opcode (INC/DEC/SUM/CPY) 执行成功")

-- 7.5 带 vmctx 的自定义比较 opcode — GE (>=)
vm.setuserminiop(20, function(vmctx, a, b, c, k)
    local left  = vmctx:getreg(b)
    local right = vmctx:getreg(c)
    if left ~= nil and right ~= nil then
        if left >= right then
            vmctx:setreg(a, true)    -- 类似 setbtvalue
        else
            vmctx:setreg(a, false)
        end
    end
    return 1
end)

local bc_ge = {
    { op = vm.MINI_LOADK, a = 1, b = 10 },
    { op = vm.MINI_LOADK, a = 2, b = 5  },
    { op = 20,            a = 3, b = 1, c = 2 },   -- GE R3, R1, R2 → true (10>=5)
    { op = vm.MINI_LOADK, a = 4, b = 3  },
    { op = 20,            a = 5, b = 4, c = 1 },   -- GE R5, R4, R1 → false (3>=10)
    { op = vm.MINI_HALT },
}
ok = pcall(vm.execmini, bc_ge, 8)
check(ok, "自定义 GE 比较 opcode 执行成功")

-- 7.6 边界条件：无效范围注册
local ok_err, err = pcall(vm.setuserminiop, 0, function() end)
check(not ok_err, string.format("setuserminiop(0) 应报错 → %s", tostring(err)))

ok_err, err = pcall(vm.setuserminiop, 200, function() end)
check(not ok_err, string.format("setuserminiop(200) 应报错 → %s", tostring(err)))


-- ==================================================================
-- 8. 主 VM OP_CUSTOM 系统 — 扩展 Lua VM 指令
-- ==================================================================
print("\n8. 主 VM OP_CUSTOM 系统 — 扩展 Lua VM 自定义指令")

-- 8.1 setop / getop / listops / opcount / delop
check(vm.opcount() == 0,
    string.format("初始 opcount = %d", vm.opcount()))

local handler_0_called = 0
local function handler_0(L)
    handler_0_called = handler_0_called + 1
    return 0
end
vm.setop(0, handler_0)
check(vm.opcount() == 1, "setop(0) → opcount=1")

local h = vm.getop(0)
check(type(h) == "function", "getop(0) 返回函数")

-- 8.2 注册多个不同 opcode
vm.setop(42, function() end)
vm.setop(127, function() end)
vm.setop(255, function() end)
check(vm.opcount() == 4, string.format("4个op注册 → opcount=%d", vm.opcount()))

local list = vm.listops()
check(#list == 4, string.format("listops() 返回 %d 个", #list))

-- 8.3 delop
vm.delop(127)
check(vm.opcount() == 3, "delop(127) → opcount=3")
check(vm.getop(127) == nil, "getop(127) → nil")

-- 8.4 重置: 删除所有
vm.delop(0); vm.delop(42); vm.delop(255)
check(vm.opcount() == 0, "全部删除 → opcount=0")

-- 8.5 边界：越界报错
ok_err, err = pcall(vm.setop, -1, handler_0)
check(not ok_err, string.format("setop(-1) 应报错 → %s", tostring(err)))

ok_err, err = pcall(vm.setop, 256, handler_0)
check(not ok_err, string.format("setop(256) 应报错 → %s", tostring(err)))


-- ==================================================================
-- 9. makeinst / getinstop — 指令构造与解析
-- ==================================================================
print("\n9. makeinst / getinstop — 指令构造与反解析")

-- makeinst: 将用户 opcode 编码为 Lua VM 指令字
local inst_42 = vm.makeinst(42)
check(type(inst_42) == "number", "makeinst(42) 返回 number")

local parsed = vm.getinstop(inst_42)
check(parsed == 42, string.format("getinstop(makeinst(42)) → %s", tostring(parsed)))

local inst_0 = vm.makeinst(0)
check(vm.getinstop(inst_0) == 0, "makeinst(0) → getinstop == 0")

-- 非 OP_CUSTOM 指令 getinstop 返回 nil
check(vm.getinstop(0) == nil, "getinstop(普通指令) → nil")


-- ==================================================================
-- 10. vm.compile — 原始数字编译 (兼容旧格式)
-- ==================================================================
print("\n10. vm.compile — 原始数字格式编译器")

-- 格式: "op a b c" (每行一条指令)
local raw_code = [[
2 1 10 0
2 2 3 0
3 3 1 2
15 0 0 0
]]
local bc = vm.compile(raw_code)
check(#bc == 4, "compile 生成 4 条指令")

-- 空代码
check(#vm.compile("") == 0, "compile('') → 空表")

-- 注释行
local with_comment = [[
# 注释行
2 1 100 0
; 分号注释
2 2 200 0
]]
check(#vm.compile(with_comment) == 2, "compile(含注释) → 2 条指令")


-- ==================================================================
-- 11. 综合示例 — 用 asm 实现阶乘计算
-- ==================================================================
print("\n11. 综合示例 — 用 vm.asm + execmini 计算 5! (120)")
print("    register layout:")
print("    R1 = 累乘器, R2 = 当前乘数, R3 = 目标终点(1), R4 = 比较结果")

-- 跳转公式: pc += k + 1, 所以 k = 目标位置 - 当前位置 - 1
local bc_factorial = vm.asm([[
    ; 初始化
    LOADK R1, 1       ; pos 1: result = 1
    LOADK R2, 5       ; pos 2: n = 5
    LOADK R3, 1       ; pos 3: limit = 1 (循环终点)
    LOADK R6, 1       ; pos 4: 递减步长
    LOADK R7, 1       ; pos 5: 常量 1

    ; 循环头: 检查 n == 1 ?
    EQ    R4, R2, R3  ; pos 6: R4 = (n == 1)
    JT    R4, 3       ; pos 7: 相等→跳到pos11(PRINT); k=11-7-1=3

    ; 循环体
    MUL   R1, R1, R2  ; pos 8: result = result * n
    SUB   R2, R2, R6  ; pos 9: n = n - 1

    ; 跳回循环头
    JMP   -5          ; pos10: 跳回pos6(EQ); k=6-10-1=-5

    ; 结束
    PRINT R1          ; pos11
    HALT              ; pos12
]])

check(#bc_factorial == 12,
    string.format("阶乘 bytecode 共 %d 条指令", #bc_factorial))

ok = pcall(vm.execmini, bc_factorial, 8)
check(ok, "阶乘 MiniVM 执行完成 → 预期输出 120.0")


-- ==================================================================
-- 12. 综合示例 — 用 asm 编译后用 mcall 调用
-- ==================================================================
print("\n12. 综合示例 — asm + mcall 组合使用")

-- 将输入数值加倍: 读取 R1 (参数), ×2, 返回结果
local bc_double = vm.asm([[
    LOADK R2, 2
    MUL   R1, R1, R2    ; R1 = R1 * 2
    RET
]])

-- mcall: 第一个参数 (21) 自动放入 R1, 执行后返回 R1 的值
local result = vm.mcall(bc_double, 4, 21)
check(result == 42,
    string.format("mcall(double, 21) → %s (期望 42)", tostring(result)))


-- ==================================================================
-- 13. 综合示例 — asm + 用户自定义 opcode + mcall
-- ==================================================================
print("\n13. 综合示例 — asm + 自定义 opcode + mcall 组合")

-- 自定义 opcode 21: POW2 — R[a] = R[a]^2 (平方)
vm.setuserminiop(21, function(vmctx, a, b, c, k)
    local v = vmctx:getreg(a)
    if type(v) == "number" then
        vmctx:setreg(a, v * v)
    end
    return 1
end)

-- asm 中可直接使用数字 opcode: "21 R1" → {op=21, a=1}
-- 对输入数连续平方两次 (x → x^4)，再 ×2
local bc_full = vm.asm([[
    21     R1        ; POW2 R1 (平方: x^2)
    21     R1        ; POW2 R1 (再平方: x^4)
    LOADK  R2, 2
    MUL    R1, R1, R2  ; x^4 * 2
    RET
]])

check(#bc_full == 5, string.format("asm 混合自定义 op 生成了 %d 条指令", #bc_full))

-- 测试: mcall(bc_full, 4, 3) → ((3^2)^2) * 2 = (9^2) * 2 = 81 * 2 = 162
local r = vm.mcall(bc_full, 4, 3)
check(r == 162,
    string.format("mcall(transform, 3) → %s (期望 162)", tostring(r)))


-- ==================================================================
-- 测试结果汇总
-- ==================================================================
print("\n========================================================")
print(string.format("测试完毕: 通过 %d, 失败 %d, 总计 %d", PASS, FAIL, PASS + FAIL))
print("========================================================")

if FAIL > 0 then
    os.exit(1)
end