-- NativeVM 性能基准测试
-- 对比 NativeVM 与 Lua 在各种运算上的速度差异

local nv = require("nativevm")

print("========================================")
print("  NativeVM 性能基准测试")
print("========================================")
print("")

-- 工具函数
local function timeit(name, n, fn)
  local start = os.clock()
  for i = 1, n do fn(i) end
  local elapsed = os.clock() - start
  print(string.format("  %-30s x%d: %.4f 秒", name, n, elapsed))
  return elapsed
end

-- ============================================================
-- 测试1: 纯整数算术
-- ============================================================
print("--- 测试1: 纯整数算术 ---")

local N = 200000

local asm_arith = [[
  MOV R1,1
  MOV R2,2
  MOV R3,3
  .loop:
  R4 = R1 + R2
  R4 = R4 - R3
  R4 = R4 * R2
  R4 = R4 + 1
  R1 = R1 + 1
  R5 = R1 < R0
  JT R5,.loop
  RET R4,1
]]
local inst_arith = nv.asm(asm_arith, 6)
local vm_arith = nv.new(inst_arith, 6)

timeit("NativeVM 整数四则运算", 1, function()
  nv.call(vm_arith, N)
end)

timeit("Lua 整数四则运算", N, function()
  local a, b, c = 1, 2, 3
  local r = 0
  r = a + b
  r = r - c
  r = r * b
  r = r + 1
  a = a + 1
end)

print("")

-- ============================================================
-- 测试2: 斐波那契
-- ============================================================
print("--- 测试2: 斐波那契 ---")

local function lua_fib(n)
  if n <= 1 then return n end
  return lua_fib(n-1) + lua_fib(n-2)
end

timeit("Lua fib(35) 递归", 1, function()
  lua_fib(35)
end)

print("")

-- ============================================================
-- 测试3: 位运算
-- ============================================================
print("--- 测试3: 位运算 ---")

local asm_bit = [[
  MOV R1,0
  MOV R2,0x12345678
  MOV R3,0x0F0F0F0F
  .loop:
  R4 = R2 & R3
  R4 = R4 | R1
  R4 = R4 ^ R2
  R4 = R4 << 1
  R4 = R4 >> 2
  R1 = R1 + 1
  R5 = R1 < R0
  JT R5,.loop
  RET R4,1
]]
local inst_bit = nv.asm(asm_bit, 6)
local vm_bit = nv.new(inst_bit, 6)

timeit("NativeVM 位运算", 1, function()
  nv.call(vm_bit, N)
end)

timeit("Lua 位运算", N, function()
  local a = 0
  local b = 0x12345678
  local c = 0x0F0F0F0F
  local r = b & c
  r = r | a
  r = r ~ b
  r = r << 1
  r = r >> 2
end)

print("")

-- ============================================================
-- 测试4: 浮点运算
-- ============================================================
print("--- 测试4: 浮点运算 ---")

local asm_float = [[
  MOV R1,0
  LOADKF R2,3.14
  LOADKF R3,2.71
  .loop:
  R4 = R2 + R3
  R4 = R4 - R3
  R4 = R4 * R2
  R4 = R4 / R3
  R1 = R1 + 1
  R5 = R1 < R0
  JT R5,.loop
  RET R4,1
]]
local inst_float = nv.asm(asm_float, 6)
local vm_float = nv.new(inst_float, 6)

timeit("NativeVM 浮点运算", 1, function()
  nv.call(vm_float, N)
end)

timeit("Lua 浮点运算", N, function()
  local a = 3.14
  local b = 2.71
  local r = a + b
  r = r - b
  r = r * a
  r = r / b
end)

print("")

-- ============================================================
-- 测试5: 素数判断
-- ============================================================
print("--- 测试5: 素数判断 ---")

local asm_prime = [[
  MOV R1,2
  .loop:
  MOV R2,2
  .inner:
  R3 = R1 % R2
  R3 = R3 == 0
  JT R3,.notprime
  R2 = R2 + 1
  R4 = R2 * R2
  R5 = R4 <= R1
  JT R5,.inner
  .notprime:
  R1 = R1 + 1
  R6 = R1 < R0
  JT R6,.loop
  RET R1,1
]]
local inst_prime = nv.asm(asm_prime, 7)
local vm_prime = nv.new(inst_prime, 7)

timeit("NativeVM 素数(<10000)", 1, function()
  nv.call(vm_prime, 10000)
end)

timeit("Lua 素数(<10000)", 1, function()
  for i = 2, 9999 do
    for j = 2, math.floor(math.sqrt(i)) do
      if i % j == 0 then break end
    end
  end
end)

print("")

-- ============================================================
-- 测试6: 空循环对比（纯解释开销）
-- ============================================================
print("--- 测试6: 空循环对比 ---")

local asm_empty = [[
  MOV R1,0
  .loop:
  R1 = R1 + 1
  R2 = R1 < R0
  JT R2,.loop
  RET R1,1
]]
local inst_empty = nv.asm(asm_empty, 3)
local vm_empty = nv.new(inst_empty, 3)

timeit("NativeVM 空循环", 1, function()
  nv.call(vm_empty, N)
end)

timeit("Lua 空循环", N, function()
  local a = 0
  a = a + 1
end)

print("")

print("========================================")
print("  测试完成")
print("========================================")