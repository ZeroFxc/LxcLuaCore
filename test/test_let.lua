-- 测试独立 let 语句

-- 测试1: 基本 let 声明
let x = 10
print("测试1 (let x = 10):", x)
assert(x == 10, "测试1 失败")

-- 测试2: let 声明带表达式
let y = x * 2 + 5
print("测试2 (let y = x * 2 + 5):", y)
assert(y == 25, "测试2 失败")

-- 测试3: 多变量 let 声明
let a, b = 1, 2
print("测试3 (let a, b = 1, 2):", a, b)
assert(a == 1 and b == 2, "测试3 失败")

-- 测试4: let 在函数内使用
function test_let_inside_func()
    let msg = "hello from let"
    let count = 42
    return msg, count
end

local msg, count = test_let_inside_func()
print("测试4 (let 在函数内):", msg, count)
assert(msg == "hello from let" and count == 42, "测试4 失败")

-- 测试5: let 与 if let 共存
let base = 100
if let val = base > 50 and base then
    print("测试5 (if let 与 let 共存): val =", val)
    assert(val == 100, "测试5 失败")
end

-- 测试6: let 在 guard 之后使用
function test_let_with_guard(n)
    guard n > 0 else {
        return "error"
    }
    let doubled = n * 2
    return doubled
end

local result = test_let_with_guard(5)
print("测试6 (let 在 guard 之后):", result)
assert(result == 10, "测试6 失败")

local result = test_let_with_guard(-1)
print("测试6 (let 在 guard 之后，guard 触发):", result)
assert(result == "error", "测试6a 失败")

-- 测试7: let 在循环中
let sum = 0
for i = 1, 5 do
    let temp = i * i
    sum = sum + temp
end
print("测试7 (let 在循环中):", sum)
assert(sum == 55, "测试7 失败")  -- 1+4+9+16+25 = 55

-- 测试8: let 与 table
let t = {x = 1, y = 2}
print("测试8 (let t = table):", t.x, t.y)
assert(t.x == 1 and t.y == 2, "测试8 失败")

print("\n所有独立 let 语句测试通过!")