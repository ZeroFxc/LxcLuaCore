--[[
  Phase 1 JIT 修复验证测试
  测试四个修复:
    1.1 自递归标记遗漏修复 (OP_NEWMAP 等新增写入寄存器操作码)
    1.2 Codegen 静默丢弃 IR 修复 (改为 fallback)
    1.3 CSE clobbered_regs 数组越界修复 (动态分配)
    1.4 IR_CALL 两套实现统一 (标记废弃)

  运行方式: lxclua test\test_phase1_fix.lua
  预期: 所有测试 PASS, 无 FAIL
]]

local passed = 0
local failed = 0

local function test(name, fn)
  local ok, err = pcall(fn)
  if ok then
    passed = passed + 1
    print(string.format("[PASS] %s", name))
  else
    failed = failed + 1
    print(string.format("[FAIL] %s: %s", name, tostring(err)))
  end
end

local function check(cond, msg)
  if not cond then
    error(msg or "assertion failed", 2)
  end
end

-- ============================================================
-- 1. 自递归标记修复 (Fix 1.1)
-- ============================================================
print("\n========== 1. 自递归标记修复 ==========")

-- 1.1 基础自递归: fib 递归 (OP_NEWTABLE 路径)
test("1.1 fib递归 self_calls>0", function()
  jit.off()
  jit.on()
  -- 先重置统计
  local s0 = jit.stats()
  local function fib(n)
    if n < 2 then return n end
    return fib(n - 1) + fib(n - 2)
  end
  fib(25)  -- 触发编译
  local s1 = jit.stats()
  print(string.format("   compiled=%d, self_calls=%d, fallback=%d",
    s1.compiled - s0.compiled,
    s1.self_calls - s0.self_calls,
    s1.fallback - s0.fallback))
  check(s1.compiled > s0.compiled, "fib should be compiled")
  check(s1.self_calls > s0.self_calls, "self_calls should be >0 for recursive fib")
end)

-- 1.2 自递归 + OP_NEWMAP: 在递归函数之前有 map 字面量 (修复前 OP_NEWMAP 不在 writes_reg 中)
test("1.2 map字面量后自递归 self_calls>0", function()
  jit.off()
  jit.on()
  local s0 = jit.stats()
  -- OP_NEWMAP: 创建 map 写入一个寄存器, 之前这个 opcode 不在 writes_reg 列表中
  -- 导致后续的对 func_reg 的写指令扫描会跳过这个 map 操作
  local cache = [a = 1, b = 2]
  local function factorial(n)
    if n <= 1 then return 1 end
    return n * factorial(n - 1)
  end
  -- 预热: 调用多次触发 JIT
  for i = 1, 10 do factorial(10) end
  local s1 = jit.stats()
  print(string.format("   compiled=%d, self_calls=%d, fallback=%d",
    s1.compiled - s0.compiled,
    s1.self_calls - s0.self_calls,
    s1.fallback - s0.fallback))
  check(s1.compiled > s0.compiled, "factorial should be compiled")
  check(s1.self_calls > s0.self_calls, "self_calls should be >0 for recursive factorial after NEWMAP")
  check(#cache == 2, "cache should have 2 entries")  -- 使用 cache 避免 unused 警告
end)

-- 1.3 自递归 + OP_NEWTABLE: 在递归函数之前有 table 字面量
test("1.3 table字面量后自递归 self_calls>0", function()
  jit.off()
  jit.on()
  local s0 = jit.stats()
  -- 创建 table 占用寄存器, 测试 writes_reg 扫描不受 OP_NEWTABLE 影响
  local t = {1, 2, 3}
  local function sum_to(n)
    if n <= 0 then return 0 end
    return n + sum_to(n - 1)
  end
  for i = 1, 10 do sum_to(10) end
  local s1 = jit.stats()
  print(string.format("   compiled=%d, self_calls=%d, fallback=%d",
    s1.compiled - s0.compiled,
    s1.self_calls - s0.self_calls,
    s1.fallback - s0.fallback))
  check(s1.compiled > s0.compiled, "sum_to should be compiled")
  check(s1.self_calls > s0.self_calls, "self_calls should be >0")
  check(#t == 3, "table should have 3 elements")  -- 使用 t 避免 unused 警告
end)

-- 1.4 局部递归函数 (upvalue 路径)
test("1.4 局部递归函数 self_calls>0", function()
  jit.off()
  jit.on()
  local s0 = jit.stats()
  local function ackermann(m, n)
    if m == 0 then return n + 1 end
    if n == 0 then return ackermann(m - 1, 1) end
    return ackermann(m - 1, ackermann(m, n - 1))
  end
  ackermann(3, 4)  -- 触发编译
  local s1 = jit.stats()
  print(string.format("   compiled=%d, self_calls=%d, fallback=%d",
    s1.compiled - s0.compiled,
    s1.self_calls - s0.self_calls,
    s1.fallback - s0.fallback))
  check(s1.compiled > s0.compiled, "ackermann should be compiled")
  check(s1.self_calls > s0.self_calls, "self_calls should be >0 for ackermann")
end)

-- 1.5 自递归正确性: fib(20) 结果正确
test("1.5 fib(20) 结果正确", function()
  jit.off()
  jit.on()
  local function fib(n)
    if n < 2 then return n end
    return fib(n - 1) + fib(n - 2)
  end
  fib(20)  -- 预热编译
  local r = fib(20)
  check(r == 6765, "fib(20) should be 6765, got " .. tostring(r))
end)

-- 1.6 非递归函数不受影响
test("1.6 非递归函数 self_calls 不增加", function()
  jit.off()
  jit.on()
  local s0 = jit.stats()
  local function add(a, b) return a + b end
  for i = 1, 100 do add(i, i + 1) end
  local s1 = jit.stats()
  print(string.format("   compiled=%d, self_calls_delta=%d",
    s1.compiled - s0.compiled,
    s1.self_calls - s0.self_calls))
  check(s1.compiled > s0.compiled, "add should be compiled")
  check(s1.self_calls == s0.self_calls, "non-recursive function should not have self_calls")
end)


-- ============================================================
-- 2. Codegen Fallback 修复 (Fix 1.2)
-- ============================================================
print("\n========== 2. Codegen Fallback 修复 ==========")

-- 2.1 Map 操作在 JIT 下正确执行 (之前 IR_NEWMAP/IR_GETMAP/IR_SETMAP 被静默跳过)
test("2.1 map 创建+读写 JIT下正确", function()
  jit.off()
  jit.on()
  local function make_map()
    local m = [a = 1, b = 2, c = 3]
    m["d"] = 4
    return m["a"], m["d"], #m
  end
  -- 预热
  for i = 1, 10 do make_map() end
  local a, d, n = make_map()
  check(a == 1, "m['a'] should be 1, got " .. tostring(a))
  check(d == 4, "m['d'] should be 4, got " .. tostring(d))
  check(n == 4, "#m should be 4, got " .. tostring(n))
end)

-- 2.2 Map 迭代在 JIT 下正确
test("2.2 map 迭代 JIT下正确", function()
  jit.off()
  jit.on()
  local function iter_map()
    local m = [x = 10, y = 20, z = 30]
    local sum = 0
    for k, v in mpairs(m) do
      sum = sum + v
    end
    return sum
  end
  for i = 1, 10 do iter_map() end
  local r = iter_map()
  check(r == 60, "sum of map values should be 60, got " .. tostring(r))
end)

-- 2.3 Trait 在 JIT 下正确执行
test("2.3 trait 使用 JIT下正确", function()
  jit.off()
  jit.on()
  local function use_trait_make()
    trait Logger
      require function log(_self, msg)
    end

    class MyClass
      use Logger
      function log(self, msg)
        return "LOG: " .. msg
      end
    end

    local obj = MyClass()
    return obj:log("hello")
  end
  for i = 1, 10 do use_trait_make() end
  local r = use_trait_make()
  check(r == "LOG: hello", "trait method should return 'LOG: hello', got " .. tostring(r))
end)

-- 2.4 Regex 在 JIT 下正确执行
test("2.4 regex JIT下正确", function()
  jit.off()
  jit.on()
  local function regex_test()
    local s = "hello123world"
    local r = string.match(s, "([0-9]+)")
    return r
  end
  for i = 1, 10 do regex_test() end
  local r = regex_test()
  check(r == "123", "regex match should return '123', got " .. tostring(r))
end)

-- 2.5 Switch 在 JIT 下正确
test("2.5 switch JIT下正确", function()
  jit.off()
  jit.on()
  local function sw_test(x)
    return switch x
    case 1, 2, 3 -> "small"
    case 4, 5, 6 -> "medium"
    case 7, 8, 9 -> "large"
    end
  end
  for i = 1, 10 do sw_test(3) end
  local r1, r2, r3 = sw_test(2), sw_test(5), sw_test(10)
  check(r1 == "small", "switch(2) should be 'small', got " .. tostring(r1))
  check(r2 == "medium", "switch(5) should be 'medium', got " .. tostring(r2))
  check(r3 == nil, "switch(10) unmatched should be nil, got " .. tostring(r3))
end)

-- 2.6 枚举在 JIT 下正确
test("2.6 枚举 JIT下正确", function()
  jit.off()
  jit.on()
  local function enum_test()
    enum Color do
      Red = 1
      Green = 2
      Blue = 3
    end
    return Color.Red, Color.Green, Color.Blue
  end
  for i = 1, 10 do enum_test() end
  local r, g, b = enum_test()
  check(r == 1, "Color.Red should be 1, got " .. tostring(r))
  check(g == 2, "Color.Green should be 2, got " .. tostring(g))
  check(b == 3, "Color.Blue should be 3, got " .. tostring(b))
end)

-- 2.7 类继承在 JIT 下正确
test("2.7 类继承 JIT下正确", function()
  jit.off()
  jit.on()
  local function class_test()
    class Animal
      function speak(self)
        return "animal"
      end
    end
    class Dog : Animal
      function speak(self)
        return "dog"
      end
    end
    local d = Dog()
    return d:speak()
  end
  for i = 1, 10 do class_test() end
  local r = class_test()
  check(r == "dog", "Dog:speak() should be 'dog', got " .. tostring(r))
end)

-- 2.8 装饰器在 JIT 下正确
test("2.8 装饰器 JIT下正确", function()
  jit.off()
  jit.on()
  local function decorator_test()
    local calls = 0
    local function logCall(fn)
      return function(...)
        calls = calls + 1
        return fn(...)
      end
    end
    @logCall
    local function add(a, b) return a + b end
    local r = add(1, 2)
    return r, calls
  end
  for i = 1, 10 do decorator_test() end
  local r, c = decorator_test()
  check(r == 3, "add(1,2) should be 3, got " .. tostring(r))
  check(c > 0, "decorator should have been called, got " .. tostring(c))
end)

-- 2.9 解构赋值在 JIT 下正确
test("2.9 解构赋值 JIT下正确", function()
  jit.off()
  jit.on()
  local function destr_test()
    local t = {name = "Alice", age = 30}
    local {name, age} = t
    return name, age
  end
  for i = 1, 10 do destr_test() end
  local n, a = destr_test()
  check(n == "Alice", "name should be 'Alice', got " .. tostring(n))
  check(a == 30, "age should be 30, got " .. tostring(a))
end)

-- 2.10 验证 JIT 没有过度 fallback (关键特性应有原生 codegen)
test("2.10 纯算术循环 fallback=0", function()
  jit.off()
  jit.on()
  local s0 = jit.stats()
  local function pure_loop(n)
    local s = 0
    for i = 1, n do
      s = s + i * i - i / 2
    end
    return s
  end
  pure_loop(1000)  -- 触发编译
  -- 多次运行, 收集 fallback 统计
  for i = 1, 100 do pure_loop(100) end
  local s1 = jit.stats()
  print(string.format("   compiled=%d, fallback_delta=%d",
    s1.compiled - s0.compiled,
    s1.fallback - s0.fallback))
  -- 纯算术循环不应有 fallback
  check(s1.fallback == s0.fallback,
    "pure arithmetic loop should not trigger fallback, got " .. (s1.fallback - s0.fallback) .. " fallbacks")
end)


-- ============================================================
-- 3. CSE 越界修复 (Fix 1.3)
-- 测试: 大量局部变量的函数, 超过旧数组 256 限制
-- ============================================================
print("\n========== 3. CSE 越界修复 ==========")

test("3.1 大量局部变量 JIT 编译正确", function()
  jit.off()
  jit.on()
  -- 生成 300 个局部变量, 每个都做简单算术
  local function big_func()
    local v0, v1, v2, v3, v4, v5, v6, v7, v8, v9 = 0,1,2,3,4,5,6,7,8,9
    local v10,v11,v12,v13,v14,v15,v16,v17,v18,v19 = 10,11,12,13,14,15,16,17,18,19
    local v20,v21,v22,v23,v24,v25,v26,v27,v28,v29 = 20,21,22,23,24,25,26,27,28,29
    local v30,v31,v32,v33,v34,v35,v36,v37,v38,v39 = 30,31,32,33,34,35,36,37,38,39
    local v40,v41,v42,v43,v44,v45,v46,v47,v48,v49 = 40,41,42,43,44,45,46,47,48,49
    local v50,v51,v52,v53,v54,v55,v56,v57,v58,v59 = 50,51,52,53,54,55,56,57,58,59
    local v60,v61,v62,v63,v64,v65,v66,v67,v68,v69 = 60,61,62,63,64,65,66,67,68,69
    local v70,v71,v72,v73,v74,v75,v76,v77,v78,v79 = 70,71,72,73,74,75,76,77,78,79
    local v80,v81,v82,v83,v84,v85,v86,v87,v88,v89 = 80,81,82,83,84,85,86,87,88,89
    local v90,v91,v92,v93,v94,v95,v96,v97,v98,v99 = 90,91,92,93,94,95,96,97,98,99
    local v100,v101,v102,v103,v104,v105,v106,v107,v108,v109 = 100,101,102,103,104,105,106,107,108,109
    local v110,v111,v112,v113,v114,v115,v116,v117,v118,v119 = 110,111,112,113,114,115,116,117,118,119
    local v120,v121,v122,v123,v124,v125,v126,v127,v128,v129 = 120,121,122,123,124,125,126,127,128,129
    local v130,v131,v132,v133,v134,v135,v136,v137,v138,v139 = 130,131,132,133,134,135,136,137,138,139
    local v140,v141,v142,v143,v144,v145,v146,v147,v148,v149 = 140,141,142,143,144,145,146,147,148,149
    local v150,v151,v152,v153,v154,v155,v156,v157,v158,v159 = 150,151,152,153,154,155,156,157,158,159
    local v160,v161,v162,v163,v164,v165,v166,v167,v168,v169 = 160,161,162,163,164,165,166,167,168,169
    local v170,v171,v172,v173,v174,v175,v176,v177,v178,v179 = 170,171,172,173,174,175,176,177,178,179
    local v180,v181,v182,v183,v184,v185,v186,v187,v188,v189 = 180,181,182,183,184,185,186,187,188,189
    local v190,v191,v192,v193,v194,v195,v196,v197,v198,v199 = 190,191,192,193,194,195,196,197,198,199
    local v200,v201,v202,v203,v204,v205,v206,v207,v208,v209 = 200,201,202,203,204,205,206,207,208,209
    local v210,v211,v212,v213,v214,v215,v216,v217,v218,v219 = 210,211,212,213,214,215,216,217,218,219
    local v220,v221,v222,v223,v224,v225,v226,v227,v228,v229 = 220,221,222,223,224,225,226,227,228,229
    local v230,v231,v232,v233,v234,v235,v236,v237,v238,v239 = 230,231,232,233,234,235,236,237,238,239
    local v240,v241,v242,v243,v244,v245,v246,v247,v248,v249 = 240,241,242,243,244,245,246,247,248,249
    local v250,v251,v252,v253,v254,v255,v256,v257,v258,v259 = 250,251,252,253,254,255,256,257,258,259
    local v260,v261,v262,v263,v264,v265,v266,v267,v268,v269 = 260,261,262,263,264,265,266,267,268,269
    local v270,v271,v272,v273,v274,v275,v276,v277,v278,v279 = 270,271,272,273,274,275,276,277,278,279
    local v280,v281,v282,v283,v284,v285,v286,v287,v288,v289 = 280,281,282,283,284,285,286,287,288,289
    local v290,v291,v292,v293,v294,v295,v296,v297,v298,v299 = 290,291,292,293,294,295,296,297,298,299
    -- 少做点计算, 关键是让 maxstacksize > 256
    return v0 + v50 + v100 + v150 + v200 + v250 + v299
  end
  -- 预热
  for i = 1, 10 do big_func() end
  local r = big_func()
  -- 0+50+100+150+200+250+299 = 1049
  check(r == 1049, "big_func should return 1049, got " .. tostring(r))
end)

test("3.2 大量重复计算 CSE 正确", function()
  jit.off()
  jit.on()
  -- 函数内有大量重复的相同算术表达式, 测试 CSE 不会越界
  local function cse_func()
    local a, b, c, d, e, f, g, h = 1, 2, 3, 4, 5, 6, 7, 8
    -- 大量重复计算
    local r1 = a + b + c + d + e + f + g + h
    local r2 = a + b + c + d + e + f + g + h  -- 重复
    local r3 = a + b + c + d + e + f + g + h  -- 重复
    local r4 = a + b + c + d + e + f + g + h  -- 重复
    local r5 = a + b + c + d + e + f + g + h  -- 重复
    return r1 + r2 + r3 + r4 + r5
  end
  for i = 1, 10 do cse_func() end
  local r = cse_func()
  -- a+b+c+d+e+f+g+h = 36, 36*5 = 180
  check(r == 180, "cse_func should return 180, got " .. tostring(r))
end)


-- ============================================================
-- 4. IR_CALL 统一 (Fix 1.4)
-- 测试: 函数调用在 JIT 下正确执行
-- ============================================================
print("\n========== 4. IR_CALL 统一 ==========")

test("4.1 普通函数调用 JIT下正确", function()
  jit.off()
  jit.on()
  local function add(a, b) return a + b end
  local function mul(a, b) return a * b end
  local function calc(x, y)
    return add(x, y) + mul(x, y)
  end
  for i = 1, 10 do calc(3, 4) end
  local r = calc(3, 4)
  check(r == 19, "3+4 + 3*4 should be 19, got " .. tostring(r))
end)

test("4.2 多级调用链 JIT下正确", function()
  jit.off()
  jit.on()
  local function square(x) return x * x end
  local function sum_squares(a, b)
    return square(a) + square(b)
  end
  local function wrapper(x, y)
    return sum_squares(x, y) * 2
  end
  for i = 1, 10 do wrapper(3, 4) end
  local r = wrapper(3, 4)
  -- (9+16)*2 = 50
  check(r == 50, "wrapper(3,4) should be 50, got " .. tostring(r))
end)

test("4.3 混合调用 JIT下正确", function()
  -- 混合: 先调用非递归函数, 再调用递归函数
  jit.off()
  jit.on()
  local function helper(x) return x + 1 end
  local function mixed(n)
    if n <= 0 then return helper(0) end
    return helper(n) + mixed(n - 1)
  end
  for i = 1, 10 do mixed(5) end
  local r = mixed(5)
  -- helper(5)+helper(4)+helper(3)+helper(2)+helper(1)+helper(0)
  -- = 6+5+4+3+2+1 = 21
  check(r == 21, "mixed(5) should be 21, got " .. tostring(r))
end)


-- ============================================================
-- 结果汇总
-- ============================================================
print("\n========================================")
print(string.format("  结果: %d PASS, %d FAIL, %d 总计",
  passed, failed, passed + failed))
print("========================================")

if failed > 0 then
  print("\n⚠ 有测试失败, 请检查上面的 [FAIL] 输出")
  os.exit(1)
else
  print("✓ 全部通过!")
  os.exit(0)
end