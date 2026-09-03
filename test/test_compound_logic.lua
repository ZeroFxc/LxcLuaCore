-- 测试 &&= 和 ||= 短路逻辑赋值
-- 以及 0b / 0o 字面量

-- 测试 &&= (a = a and b)
print("=== &&= 测试 ===")
local a = "hello"
a &&= "world"
assert(a == "world", "&&= 1: expected 'world', got " .. tostring(a))

local b = nil
b &&= "should not assign"
assert(b == nil, "&&= 2: expected nil, got " .. tostring(b))

local c = false
c &&= "should not assign"
assert(c == false, "&&= 3: expected false, got " .. tostring(c))

local d = 0
d &&= 42
assert(d == 42, "&&= 4: expected 42, got " .. tostring(d))

-- 测试 ||= (a = a or b)
print("=== ||= 测试 ===")
local x = nil
x ||= "default"
assert(x == "default", "||= 1: expected 'default', got " .. tostring(x))

local y = "existing"
y ||= "should not override"
assert(y == "existing", "||= 2: expected 'existing', got " .. tostring(y))

local z = false
z ||= "fallback"
assert(z == "fallback", "||= 3: expected 'fallback', got " .. tostring(z))

-- 测试 0b 二进制字面量
print("=== 0b 测试 ===")
assert(0b1010 == 10, "0b1010 expected 10, got " .. tostring(0b1010))
assert(0b1111 == 15, "0b1111 expected 15, got " .. tostring(0b1111))
assert(0b0 == 0, "0b0 expected 0, got " .. tostring(0b0))
assert(0b1 == 1, "0b1 expected 1, got " .. tostring(0b1))
assert(0b11111111 == 255, "0b11111111 expected 255, got " .. tostring(0b11111111))

-- 测试 0o 八进制字面量
print("=== 0o 测试 ===")
assert(0o777 == 511, "0o777 expected 511, got " .. tostring(0o777))
assert(0o10 == 8, "0o10 expected 8, got " .. tostring(0o10))
assert(0o0 == 0, "0o0 expected 0, got " .. tostring(0o0))
assert(0o7 == 7, "0o7 expected 7, got " .. tostring(0o7))

-- 测试数字分隔符
print("=== 数字分隔符测试 ===")
assert(1_000_000 == 1000000, "1_000_000 expected 1000000, got " .. tostring(1_000_000))
assert(0xFF_EE == 0xFFEE, "0xFF_EE expected 65518, got " .. tostring(0xFF_EE))

print("all tests passed!")