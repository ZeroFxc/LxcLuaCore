-- 中缀函数调用语法测试
-- 语法: expr1 NAME expr2  =>  expr1:NAME(expr2)

print("=== 中缀函数调用测试 ===\n")

-- 测试1: 基本中缀调用
local test = { value = 10 }
function test:add(n)
    self.value = self.value + n
    return self
end
function test:get()
    return self.value
end

test add 5
assert(test:get() == 15, "Test 1 failed: basic infix call")
print("PASS 1: 基本中缀调用 test add 5 => " .. test:get())

-- 测试2: 中缀调用的返回值
local result = test get
assert(result == 15, "Test 2 failed: infix call return value")
print("PASS 2: 中缀调用返回值 test get => " .. result)

-- 测试3: 链式中缀调用
test add 3 add 7
assert(test:get() == 25, "Test 3 failed: chained infix calls")
print("PASS 3: 链式中缀调用 test add 3 add 7 => " .. test:get())

-- 测试4: 中缀与算术运算的优先级
-- infix 优先级(5) 低于加法(10)
-- a add b + c => a:add(b + c)
local calc_val = 100
local holder = { val = 0 }
function holder:add(n)
    return n
end
local v = holder add 100 + 20  -- holder:add(100 + 20) = 120
assert(v == 120, "Test 4 failed: arithmetic precedence")
print("PASS 4: 算术优先级 holder add 100 + 20 => " .. v)

-- 测试5: 中缀调用作为表达式的一部分
local s = { str = "hello" }
function s:concat(t)
    return self.str .. t
end
local greeting = s concat " world"
assert(greeting == "hello world", "Test 5 failed: infix with string literal")
print("PASS 5: 字符串字面量 s concat \" world\" => \"" .. greeting .. "\"")

-- 测试6: 中缀与比较运算符
-- a infix b == c => (a:infix(b)) == c
function s:equals(t)
    return self.str == t
end
local eq = s equals "hello"
assert(eq == true, "Test 6 failed: infix with comparison")
print("PASS 6: 与比较运算符结合")

-- 测试7: 中缀调用括号
-- (1 + 2) add 3 => (1+2):add(3)
local num = { value = 10 }
function num:mul(x)
    return self.value * x
end
local m = num mul (2 + 3)  -- num:mul(5)
assert(m == 50, "Test 7 failed: parenthesized arg")
print("PASS 7: 括号参数 num mul (2 + 3) => " .. m)

-- 测试8: 负数参数
function test:sub(n)
    self.value = self.value - n
    return self
end
test sub -3  -- test:sub(-3), value: 25 - (-3) = 28
assert(test:get() == 28, "Test 8 failed: negative arg")
print("PASS 8: 负数参数 test sub -3 => " .. test:get())

-- 测试9: nil/false/true
function s:set(v)
    self.str = v
    return self
end
s set nil
assert(s.str == nil, "Test 9 failed: nil arg")
s set true
assert(s.str == true, "Test 9b failed: true arg")
s set false
assert(s.str == false, "Test 9c failed: false arg")
print("PASS 9: nil/true/false 参数")

-- 测试10: 表构造器参数
function test:merge(t)
    for k, v in pairs(t) do
        self[k] = v
    end
    return self
end
test merge { a = 1, b = 2 }
assert(test.a == 1 and test.b == 2, "Test 10 failed: table literal arg")
print("PASS 10: 表构造器参数")

print("\n=== 所有测试通过! ===")