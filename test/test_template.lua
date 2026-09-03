-- 模板字符串测试 (反引号)
print("=== 模板字符串测试 ===")

-- 1. 基本无插值
local s1 = `hello world`
print("1. plain: " .. s1)  -- 期望: hello world

-- 2. 变量插值
local name = "Alice"
local age = 25
local s2 = `Name: ${name}, Age: ${age}`
print("2. interp: " .. s2)  -- 期望: Name: Alice, Age: 25

-- 3. 表达式插值
local s3 = `Sum: ${[1 + 2 + 3]}`
print("3. expr: " .. s3)  -- 期望: Sum: 6

-- 4. 多行模板字符串
local s4 = `Line1
Line2
Line3`
print("4. multi: " .. s4)  -- 期望: 三行文本

-- 5. $$ 转义
local s5 = `Price: $$100`
print("5. escape: " .. s5)  -- 期望: Price: $100

print("PASS: template")