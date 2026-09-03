--[[
test_syntax_boundary.lua — 语法边界测试
针对 AST codegen 的表达组合路径，检查是否存在类似 _ENV[func_call()] 双重 codegen 的隐蔽 bug
每个测试用例聚焦于 codegen 中可能出错的寄存器分配、求值顺序、表达式嵌套等边界情况
--]]

local passed = 0
local failed = 0

-- 自检函数
local function check(cond, msg, expected, actual)
    if cond then
        passed = passed + 1
    else
        failed = failed + 1
        local info = "[FAIL] " .. msg
        if expected ~= nil then
            info = info .. " | expected: " .. tostring(expected) .. " | got: " .. tostring(actual)
        end
        print(info)
    end
end

-- 辅助函数：用于测试 _ENV 动态索引
local function get_print() return "print" end
local function get_type() return "type" end
local function get_pairs() return "pairs" end

-- ============================================================
-- 1. 函数调用作为表索引键
-- ============================================================
print("\n=== 1. 函数调用作为表索引键 ===")

-- _ENV[func()] 返回查表值
local v1 = _ENV[get_print()]
check(type(v1) == "function" and v1 == print,
    "_ENV[get_print()] should return print function", "function", type(v1))

-- t[func1()][func2()] 嵌套查表
local function get_x() return "x" end
local function get_y() return "y" end
local t1 = { x = { y = 42 } }
check(t1[get_x()][get_y()] == 42,
    "t[get_x()][get_y()] nested lookup", 42, t1[get_x()][get_y()])

-- _ENV[func1()] 再赋值给变量后使用
local v_print = _ENV[get_print()]
check(v_print("hello") == nil and true,
    "_ENV[get_print()]('hello') should call print", "ok", "failed")

-- _ENV[func()] 返回 type 函数
local v_type = _ENV[get_type()]
check(v_type("hello") == "string",
    "_ENV[get_type()]('hello') should return 'string'", "string", v_type("hello"))

-- ============================================================
-- 2. 二元运算、字符串拼接、条件表达式作为表索引键
-- ============================================================
print("\n=== 2. 二元运算、字符串拼接、条件表达式作为索引 ===")

local t2 = { [1] = "one", [2] = "two", [3] = "three", ten = "ten", twenty = "twenty" }
local a, b = 1, 2

-- 二元运算结果作为索引
check(t2[a + b] == "three",
    "t2[a + b] with a=1,b=2", "three", t2[a + b])

-- 字符串拼接作为索引
check(t2["t" .. "en"] == "ten",
    "t2['t'..'en'] string concat", "ten", t2["t" .. "en"])

-- 条件表达式作为索引
check(t2[true and 1 or 2] == "one",
    "t2[true and 1 or 2] conditional", "one", t2[true and 1 or 2])
check(t2[false and 1 or 2] == "two",
    "t2[false and 1 or 2] conditional", "two", t2[false and 1 or 2])

-- 混合运算作为索引
local c = 10
check(t2[c - 8] == "two",
    "t2[c - 8] with c=10", "two", t2[c - 8])

-- ============================================================
-- 3. 嵌套表索引
-- ============================================================
print("\n=== 3. 嵌套表索引 ===")

local t3 = { a = { b = { c = 99 } } }
local function get_k1() return "a" end
local function get_k2() return "b" end
local function get_k3() return "c" end

-- 嵌套索引 _ENV -> t3 -> a -> b -> c (全部动态)
check(t3[get_k1()][get_k2()][get_k3()] == 99,
    "t3[get_k1()][get_k2()][get_k3()] deep nested", 99, t3[get_k1()][get_k2()][get_k3()])

-- 混合静态/动态嵌套索引
check(t3["a"][get_k2()]["c"] == 99,
    "t3['a'][get_k2()]['c'] mixed static/dynamic", 99, t3["a"][get_k2()]["c"])

-- 深层嵌套 + 赋值
local t3b = { a = { b = {} } }
t3b[get_k1()][get_k2()][get_k3()] = 77
check(t3b.a.b.c == 77,
    "t3b[get_k1()][get_k2()][get_k3()] = 77 assignment", 77, t3b.a.b.c)

-- ============================================================
-- 4. 表构造器、函数字面量、一元运算作为表索引键
-- ============================================================
print("\n=== 4. 表构造器、函数字面量、一元运算作为索引 ===")

-- 表构造器作为索引
local t4 = {}
local k_table = { type = "test" }
t4[k_table] = "found"
check(t4[k_table] == "found",
    "t4[{type='test'}] table as key", "found", t4[k_table])

-- 一元运算作为索引
local t4b = { [-1] = "neg", [0] = "zero", [1] = "pos" }
local n = 1
check(t4b[-n] == "neg",
    "t4b[-n] with n=1", "neg", t4b[-n])

-- 取长运算作为索引
local arr = { 10, 20, 30 }
check(arr[#arr - 1] == 20,
    "arr[#arr - 1] length minus 1", 20, arr[#arr - 1])

-- ============================================================
-- 5. 方法调用结果作为表索引键
-- ============================================================
print("\n=== 5. 方法调用结果作为索引 ===")

local t5 = { hello = "world", world = "hello" }
local obj = {
    get_key = function(self) return "hello" end
}

-- obj:method() 结果作为索引
check(t5[obj:get_key()] == "world",
    "t5[obj:get_key()] method call as key", "world", t5[obj:get_key()])

-- 链式方法调用
local obj2 = {
    get_obj = function(self) return obj end
}
check(t5[obj2:get_obj():get_key()] == "world",
    "t5[obj2:get_obj():get_key()] chained method", "world", t5[obj2:get_obj():get_key()])

-- ============================================================
-- 6. 表构造器内的表达式键值
-- ============================================================
print("\n=== 6. 表构造器内的表达式键值 ===")

local function key_suffix() return "_suffix" end
local key_base = "base"

-- 动态键
local t6 = { [key_base .. key_suffix()] = "dynamic" }
check(t6["base_suffix"] == "dynamic",
    "{ [key_base .. key_suffix()] = 'dynamic' }", "dynamic", t6["base_suffix"])

-- 表达式值
local function get_val() return 123 end
local t6b = { x = get_val(), y = get_val() + 1 }
check(t6b.x == 123 and t6b.y == 124,
    "{ x = get_val(), y = get_val() + 1 }", "123,124", t6b.x .. "," .. t6b.y)

-- 混合静态/动态键（使用不同键名避免覆盖）
local function get_k4() return "d" end
local function get_k5() return "e" end
local t6c = { a = 1, [get_k4()] = 2, [get_k5()] = 3 }
check(t6c.a == 1 and t6c.d == 2 and t6c.e == 3,
    "mixed static/dynamic table keys", "1,2,3", t6c.a .. "," .. t6c.d .. "," .. t6c.e)

-- ============================================================
-- 7. 函数调用参数中的表达式
-- ============================================================
print("\n=== 7. 函数调用参数中的表达式 ===")

local function fn7(a, b, c) return a, b, c end
local t7 = { x = 10, y = 20, z = 30 }

-- 表索引作为参数
local r1, r2, r3 = fn7(t7["x"], t7["y"], t7["z"])
check(r1 == 10 and r2 == 20 and r3 == 30,
    "fn7(t7['x'], t7['y'], t7['z']) params", "10,20,30", r1 .. "," .. r2 .. "," .. r3)

-- 函数调用结果作为参数
local function get_a() return 1 end
local function get_b() return 2 end
local function add(x, y) return x + y end
check(add(get_a(), get_b()) == 3,
    "add(get_a(), get_b()) call results as params", 3, add(get_a(), get_b()))

-- 嵌套函数调用作为参数
check(add(add(get_a(), get_b()), add(get_a(), get_b())) == 6,
    "add(add(get_a(),get_b()), add(get_a(),get_b())) nested", 6,
    add(add(get_a(), get_b()), add(get_a(), get_b())))

-- 表达式作为参数
check(add(t7["x"] + t7["y"], t7["z"]) == 60,
    "add(t7['x'] + t7['y'], t7['z']) expr params", 60, add(t7["x"] + t7["y"], t7["z"]))

-- ============================================================
-- 8. 多重赋值和 return 语句中的表达式
-- ============================================================
print("\n=== 8. 多重赋值和 return 语句 ===")

local function fn8a() return "a", "b" end
local function fn8b() return 1, 2 end

-- 多重赋值
local x8a, x8b = fn8b()
check(x8a == 1 and x8b == 2,
    "local x8a, x8b = fn8b() multi-assign", "1,2", x8a .. "," .. x8b)

-- 多重赋值 + 表索引
local t8 = { x = 10, y = 20 }
local a8, b8 = t8["x"], t8["y"]
check(a8 == 10 and b8 == 20,
    "local a8, b8 = t8['x'], t8['y'] multi-assign index", "10,20", a8 .. "," .. b8)

-- return 语句中的表达式
local function return_test()
    local t = { a = 1, b = 2 }
    local function get_key() return "a" end
    return t[get_key()], t["b"]
end
local r8a, r8b = return_test()
check(r8a == 1 and r8b == 2,
    "return t[get_key()], t['b'] return expr", "1,2", r8a .. "," .. r8b)

-- return 中的函数调用
local function return_call()
    local function f() return 42 end
    return f()
end
check(return_call() == 42,
    "return f() return call", 42, return_call())

-- ============================================================
-- 9. 可选链表达式
-- ============================================================
print("\n=== 9. 可选链表达式 ===")

-- 可选链 . 语法
local t9 = { x = { y = 10 } }
check(t9?.x?.y == 10,
    "t9?.x?.y with existing table", 10, t9?.x?.y)

-- 可选链 nil 安全
local nil9 = nil
check(nil9?.x == nil,
    "nil9?.x with nil", nil, nil9?.x)

-- 可选链 [ ] 语法
check(t9[?"x"]?.y == 10,
    "t9[?'x']?.y bracket optional", 10, t9[?"x"]?.y)

-- 可选链 + 函数调用
local function get_key_opt() return "x" end
check(t9[?get_key_opt()]?.y == 10,
    "t9[?get_key_opt()]?.y optional with call", 10, t9[?get_key_opt()]?.y)

-- 可选链 + 深层 nil
local t9b = { x = nil }
check(t9b?.x?.y == nil,
    "t9b?.x?.y with nil intermediate", nil, t9b?.x?.y)

-- ============================================================
-- 10. 边界组合：_ENV 动态索引 + 方法调用
-- ============================================================
print("\n=== 10. _ENV 动态索引 + 方法调用 ===")

local s = "hello world"
local function get_sub() return "sub" end
local result = _ENV[get_type()](s)
check(result == "string",
    "_ENV[get_type()](s) call", "string", result)

-- 动态方法调用模拟
local obj10 = { val = 42 }
local function get_val_method() return "val" end
check(obj10[get_val_method()] == 42,
    "obj10[get_val_method()] dynamic method", 42, obj10[get_val_method()])

-- ============================================================
-- 11. 边界组合：复杂表达式链
-- ============================================================
print("\n=== 11. 复杂表达式链 ===")

local t11 = {
    add = function(a, b) return a + b end,
    mul = function(a, b) return a * b end,
    ops = {
        add = function(a, b) return a + b + 100 end,
        mul = function(a, b) return a * b + 100 end
    }
}

-- _ENV 动态查找 + 嵌套表索引 + 调用
local function get_add() return "add" end
local function get_ops() return "ops" end

local result11 = t11[get_ops()][get_add()](2, 3)
check(result11 == 105,
    "t11[get_ops()][get_add()](2, 3) complex chain", 105, result11)

-- 多重函数调用链
local function inc(x) return x + 1 end
local function dbl(x) return x * 2 end
local function sqr(x) return x * x end

local result11b = sqr(dbl(inc(5)))
check(result11b == 144,
    "sqr(dbl(inc(5))) triple compose", 144, result11b)

-- 表索引 + 函数调用 + 二元运算
local result11c = t11[get_add()](t11[get_ops()][get_add()](1, 2), 3)
check(result11c == 106,
    "t11[get_add()](t11[get_ops()][get_add()](1,2), 3) mixed", 106, result11c)

-- ============================================================
-- 12. 结果汇总
-- ============================================================
print("\n=== SUMMARY ===")
print(string.format("PASSED: %d, FAILED: %d", passed, failed))
if failed == 0 then
    print("ALL TESTS PASSED!")
end