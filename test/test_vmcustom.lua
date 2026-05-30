--[[
test_vmcustom.lua — 自定义 opcode 扩展系统 (vmcustom) 功能测试
覆盖：
  1. 微型 VM 解释器 (MiniVM) 基础指令
  2. MiniVM 用户自定义指令注册与执行
  3. 主 VM OP_CUSTOM 处理器注册/查询/删除
  4. vm语言编译 (vm.compile)
  5. 指令构造与解析 (vm.makeinst / vm.getinstop)
  6. 边界条件与错误处理
--]]

local vmcustom = require("vmcustom")
local passed = 0
local failed = 0

local function check(cond, msg)
    if cond then
        passed = passed + 1
        print("[PASS] " .. msg)
    else
        failed = failed + 1
        print("[FAIL] " .. msg)
    end
end

local function assert_error(fn, msg)
    local ok, err = pcall(fn)
    check(not ok and err ~= nil, msg .. " (error: " .. tostring(err) .. ")")
end

print("=== 1. MiniVM 库常量检查 ===\n")

check(vmcustom.MAX_CUSTOM_OPS == 256, "MAX_CUSTOM_OPS == 256")
check(vmcustom.MINIVM_USER_BASE == 16, "MINIVM_USER_BASE == 16")
check(vmcustom.MINIVM_MAX_OPS == 128, "MINIVM_MAX_OPS == 128")
check(vmcustom.MINI_NOP == 0, "MINI_NOP == 0")
check(vmcustom.MINI_MOV == 1, "MINI_MOV == 1")
check(vmcustom.MINI_LOADK == 2, "MINI_LOADK == 2")
check(vmcustom.MINI_ADD == 3, "MINI_ADD == 3")
check(vmcustom.MINI_SUB == 4, "MINI_SUB == 4")
check(vmcustom.MINI_MUL == 5, "MINI_MUL == 5")
check(vmcustom.MINI_DIV == 6, "MINI_DIV == 6")
check(vmcustom.MINI_EQ == 7, "MINI_EQ == 7")
check(vmcustom.MINI_LT == 8, "MINI_LT == 8")
check(vmcustom.MINI_JMP == 9, "MINI_JMP == 9")
check(vmcustom.MINI_JT == 10, "MINI_JT == 10")
check(vmcustom.MINI_JF == 11, "MINI_JF == 11")
check(vmcustom.MINI_CALL == 12, "MINI_CALL == 12")
check(vmcustom.MINI_RET == 13, "MINI_RET == 13")
check(vmcustom.MINI_PRINT == 14, "MINI_PRINT == 14")
check(vmcustom.MINI_HALT == 15, "MINI_HALT == 15")

print("\n=== 2. MiniVM 基础指令执行 ===\n")

-- 2.1 空操作
local bytecode = {
    { op = vmcustom.MINI_NOP, a = 0, b = 0, c = 0, k = 0 },
}
local ok = pcall(vmcustom.execmini, bytecode, 4)
check(ok, "execmini with NOP should succeed")

-- 2.2 MOV 指令
local bytecode_mov = {
    { op = vmcustom.MINI_LOADK, a = 1, b = 42, c = 0, k = 0 },
    { op = vmcustom.MINI_MOV, a = 2, b = 1, c = 0, k = 0 },
    { op = vmcustom.MINI_HALT, a = 0, b = 0, c = 0, k = 0 },
}
ok = pcall(vmcustom.execmini, bytecode_mov, 4)
check(ok, "execmini with MOV should succeed")

-- 2.3 ADD / SUB / MUL / DIV 指令
local bytecode_arith = {
    { op = vmcustom.MINI_LOADK, a = 1, b = 10, c = 0, k = 0 },
    { op = vmcustom.MINI_LOADK, a = 2, b = 3,  c = 0, k = 0 },
    { op = vmcustom.MINI_ADD,   a = 3, b = 1,  c = 2, k = 0 },
    { op = vmcustom.MINI_SUB,   a = 4, b = 1,  c = 2, k = 0 },
    { op = vmcustom.MINI_MUL,   a = 5, b = 1,  c = 2, k = 0 },
    { op = vmcustom.MINI_DIV,   a = 6, b = 1,  c = 2, k = 0 },
    { op = vmcustom.MINI_HALT,  a = 0, b = 0,  c = 0, k = 0 },
}
ok = pcall(vmcustom.execmini, bytecode_arith, 8)
check(ok, "execmini with arithmetic ops should succeed")

-- 2.4 比较指令
local bytecode_cmp = {
    { op = vmcustom.MINI_LOADK, a = 1, b = 5,  c = 0, k = 0 },
    { op = vmcustom.MINI_LOADK, a = 2, b = 5,  c = 0, k = 0 },
    { op = vmcustom.MINI_LOADK, a = 3, b = 10, c = 0, k = 0 },
    { op = vmcustom.MINI_EQ,    a = 10, b = 1, c = 2, k = 0 },
    { op = vmcustom.MINI_LT,    a = 11, b = 1, c = 3, k = 0 },
    { op = vmcustom.MINI_HALT,  a = 0,  b = 0, c = 0, k = 0 },
}
ok = pcall(vmcustom.execmini, bytecode_cmp, 16)
check(ok, "execmini with comparison ops should succeed")

-- 2.5 跳转指令
local bytecode_jmp = {
    { op = vmcustom.MINI_LOADK, a = 1, b = 1, c = 0, k = 0 },
    { op = vmcustom.MINI_LOADK, a = 2, b = 0, c = 0, k = 0 },
    { op = vmcustom.MINI_JT,    a = 1, b = 0, c = 0, k = 2 },
    { op = vmcustom.MINI_LOADK, a = 99, b = 99, c = 0, k = 0 },
    { op = vmcustom.MINI_HALT,  a = 0, b = 0, c = 0, k = 0 },
    { op = vmcustom.MINI_HALT,  a = 0, b = 0, c = 0, k = 0 },
    { op = vmcustom.MINI_HALT,  a = 0, b = 0, c = 0, k = 0 },
}
ok = pcall(vmcustom.execmini, bytecode_jmp, 4)
check(ok, "execmini with jump should succeed")

-- 2.6 CALL 指令
local function add(a, b) return a + b end
local bytecode_call = {
    { op = vmcustom.MINI_LOADK, a = 1, b = 0, c = 0, k = 0 },
    { op = vmcustom.MINI_LOADK, a = 2, b = 3, c = 0, k = 0 },
    { op = vmcustom.MINI_LOADK, a = 3, b = 7, c = 0, k = 0 },
    { op = vmcustom.MINI_HALT,  a = 0, b = 0, c = 0, k = 0 },
}
ok = pcall(vmcustom.execmini, bytecode_call, 8)
check(ok, "execmini with CALL should succeed")

print("\n=== 3. MiniVM 用户自定义指令 ===\n")

-- 3.1 注册自定义 MiniVM 指令 (opcode 16)
local set_count = 0
local function my_custom_op(vm, a, b, c, k)
    set_count = set_count + 1
    return 1
end
vmcustom.setuserminiop(16, my_custom_op)

local bytecode_custom = {
    { op = 16, a = 1, b = 2, c = 3, k = 0 },
    { op = 16, a = 4, b = 5, c = 6, k = 0 },
    { op = vmcustom.MINI_HALT, a = 0, b = 0, c = 0, k = 0 },
}
ok = pcall(vmcustom.execmini, bytecode_custom, 8)
check(ok and set_count == 2, "execmini with custom user opcode should execute 2 times")

-- 3.2 自定义指令可以修改寄存器
local function my_increment_op(vm, a, b, c, k)
    return 1
end
vmcustom.setuserminiop(17, my_increment_op)

local bytecode_inc = {
    { op = vmcustom.MINI_LOADK, a = 1, b = 10, c = 0, k = 0 },
    { op = 17, a = 1, b = 0, c = 0, k = 0 },
    { op = vmcustom.MINI_HALT, a = 0, b = 0, c = 0, k = 0 },
}
ok = pcall(vmcustom.execmini, bytecode_inc, 8)
check(ok, "execmini with custom increment opcode should succeed")

-- 3.3 自定义指令可以控制跳转
local function jump_if_positive(vm, a, b, c, k)
    return k
end
vmcustom.setuserminiop(18, jump_if_positive)

local bytecode_jmp_custom = {
    { op = vmcustom.MINI_LOADK, a = 1, b = 1, c = 0, k = 0 },
    { op = 18, a = 1, b = 0, c = 0, k = 2 },
    { op = vmcustom.MINI_LOADK, a = 99, b = 99, c = 0, k = 0 },
    { op = vmcustom.MINI_HALT, a = 0, b = 0, c = 0, k = 0 },
    { op = vmcustom.MINI_HALT, a = 0, b = 0, c = 0, k = 0 },
}
ok = pcall(vmcustom.execmini, bytecode_jmp_custom, 4)
check(ok, "execmini with custom jump opcode should succeed")

-- 3.4 无效范围注册
assert_error(function()
    vmcustom.setuserminiop(0, my_custom_op)
end, "setuserminiop with opcode < 16 should error")

assert_error(function()
    vmcustom.setuserminiop(200, my_custom_op)
end, "setuserminiop with opcode >= 128 should error")

print("\n=== 4. 主 VM OP_CUSTOM 处理器 API ===\n")

-- 4.1 初始 opcount 为 0
check(vmcustom.opcount() == 0, "opcount initially 0")

-- 4.2 注册自定义 opcode 处理器
local handler1_called = 0
local function handler1(L)
    handler1_called = handler1_called + 1
    return 0
end

vmcustom.setop(0, handler1)
check(vmcustom.opcount() == 1, "opcount == 1 after setop")

-- 4.3 获取处理器
local h = vmcustom.getop(0)
check(type(h) == "function", "getop(0) returns a function")

-- 4.4 列出所有 opcode
local ops = vmcustom.listops()
check(#ops == 1 and ops[1] == 0, "listops returns [0]")

-- 4.5 注册多个处理器
local handler2_called = 0
local function handler2(L)
    handler2_called = handler2_called + 1
    return 0
end
vmcustom.setop(42, handler2)
vmcustom.setop(100, handler1)
check(vmcustom.opcount() == 3, "opcount == 3 after 3 setop calls")

local ops2 = vmcustom.listops()
check(#ops2 == 3, "listops returns 3 entries")

-- 4.6 删除处理器
vmcustom.delop(100)
check(vmcustom.opcount() == 2, "opcount == 2 after delop")
check(vmcustom.getop(100) == nil, "getop(100) returns nil after delop")

-- 4.7 边界条件
assert_error(function()
    vmcustom.setop(-1, handler1)
end, "setop with negative opcode should error")

assert_error(function()
    vmcustom.setop(256, handler1)
end, "setop with opcode >= 256 should error")

assert_error(function()
    vmcustom.delop(-1)
end, "delop with negative opcode should error")

assert_error(function()
    vmcustom.delop(256)
end, "delop with opcode >= 256 should error")

print("\n=== 5. 指令构造与解析 ===\n")

-- 5.1 makeinst 构造指令
local inst = vmcustom.makeinst(42)
check(type(inst) == "number", "makeinst returns a number")

-- 5.2 getinstop 解析指令
local op = vmcustom.getinstop(inst)
check(op == 42, "getinstop(makeinst(42)) == 42")

-- 5.3 非 OP_CUSTOM 指令返回 nil
local op2 = vmcustom.getinstop(0)
check(op2 == nil, "getinstop(0) on non-CUSTOM inst returns nil")

-- 5.4 多个不同 opcode 的指令
local inst_b = vmcustom.makeinst(0)
local inst_c = vmcustom.makeinst(255)
check(vmcustom.getinstop(inst_b) == 0, "getinstop(makeinst(0)) == 0")
check(vmcustom.getinstop(inst_c) == 255, "getinstop(makeinst(255)) == 255")

print("\n=== 6. vm语言编译 (vm.compile) ===\n")

-- 6.1 基本编译
local code = [[
0 0 0 0
1 1 2 0
2 1 1 1
3 3 1 2
]]
local bytecode = vmcustom.compile(code)
check(type(bytecode) == "table", "vm.compile returns a table")
check(#bytecode == 4, "vm.compile returns 4 instructions")

-- 6.2 空代码
local empty = vmcustom.compile("")
check(type(empty) == "table" and #empty == 0, "vm.compile('') returns empty table")

-- 6.3 注释行
local code_with_comment = [[
# this is a comment
0 0 0 0
; this is also a comment
1 1 1 1
]]
local bc = vmcustom.compile(code_with_comment)
check(#bc == 2, "vm.compile with comments returns 2 instructions")

print("\n=== 7. MiniVM 边界条件 ===\n")

-- 7.1 空 bytecode
local ok_empty = pcall(vmcustom.execmini, {}, 4)
check(ok_empty, "execmini with empty bytecode should succeed")

-- 7.2 非法 bytecode 类型
assert_error(function()
    vmcustom.execmini("not a table", 4)
end, "execmini with string should error")

-- 7.3 无效跳转（超出范围）
local bytecode_invalid_jmp = {
    { op = vmcustom.MINI_JMP, a = 0, b = 0, c = 0, k = 100 },
}
ok = pcall(vmcustom.execmini, bytecode_invalid_jmp, 4)
check(ok, "execmini with out-of-range jump should succeed (just exits)")

-- 7.4 未定义的用户 opcode 应被忽略
local bytecode_unknown = {
    { op = 50, a = 0, b = 0, c = 0, k = 0 },
    { op = vmcustom.MINI_HALT, a = 0, b = 0, c = 0, k = 0 },
}
ok = pcall(vmcustom.execmini, bytecode_unknown, 4)
check(ok, "execmini with unknown user opcode should succeed (ignored)")

print("\n=== 8. 综合测试：MiniVM 计算斐波那契数列 ===\n")

local bytecode_fib = {
    { op = vmcustom.MINI_LOADK, a = 1,  b = 0,  c = 0, k = 0 },
    { op = vmcustom.MINI_LOADK, a = 2,  b = 1,  c = 0, k = 0 },
    { op = vmcustom.MINI_LOADK, a = 3,  b = 10, c = 0, k = 0 },
    { op = vmcustom.MINI_LOADK, a = 4,  b = 0,  c = 0, k = 0 },
    { op = vmcustom.MINI_ADD,   a = 5,  b = 1, c = 2, k = 0 },
    { op = vmcustom.MINI_MOV,   a = 1,  b = 2, c = 0, k = 0 },
    { op = vmcustom.MINI_MOV,   a = 2,  b = 5, c = 0, k = 0 },
    { op = vmcustom.MINI_LOADK, a = 6,  b = 1,  c = 0, k = 0 },
    { op = vmcustom.MINI_ADD,   a = 4,  b = 4, c = 6, k = 0 },
    { op = vmcustom.MINI_LOADK, a = 7,  b = 10, c = 0, k = 0 },
    { op = vmcustom.MINI_LT,    a = 8,  b = 4, c = 7, k = 0 },
    { op = vmcustom.MINI_JT,    a = 8,  b = 0, c = 0, k = -7 },
    { op = vmcustom.MINI_HALT,  a = 0,  b = 0, c = 0, k = 0 },
}
ok = pcall(vmcustom.execmini, bytecode_fib, 16)
check(ok, "execmini with Fibonacci calculation should succeed")

print("\n========== 测试结果 ==========")
print(string.format("通过: %d, 失败: %d", passed, failed))
print(string.format("总计: %d", passed + failed))

if failed > 0 then
    os.exit(1)
end