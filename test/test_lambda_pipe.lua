-- 测试 || 无参lambda、|params| lambda 和 $"..." 字符串插值

print("=== 测试1: || 无参lambda -> ===")
local producer = || -> 42
local result = producer()
assert(result == 42, "expected 42, got " .. tostring(result))
print("  通过: producer() =", result)

print("=== 测试2: |x| 单参lambda -> ===")
local double = |x| -> x * 2
print("  double(5) =", double(5))
print("  double(0) =", double(0))
print("  通过: double(5) =", double(5), ", double(0) =", double(0))

print("=== 测试3: |x, y| 多参lambda -> ===")
local add = |x, y| -> x + y
assert(add(3, 5) == 8, "expected 8")
print("  通过: add(3, 5) =", add(3, 5))

print("=== 测试4: pipeline中的 |res| lambda ===")
local result2 = producer() |> |res| -> res + 10
assert(result2 == 52, "expected 52, got " .. tostring(result2))
print("  通过: producer() |> |res| -> res + 10 =", result2)

print("=== 测试5: $\"...\" 字符串插值 ===")
local name = "World"
local msg = $"Hello, {name}!"
assert(msg == "Hello, World!", "expected 'Hello, World!', got '" .. msg .. "'")
print("  通过: $\"Hello, {name}!\" =", msg)

print("=== 测试6: $\"...\" 表达式插值 ===")
local a = 10
local b = 20
local msg2 = $"Sum: {a + b}"
assert(msg2 == "Sum: 30", "expected 'Sum: 30', got '" .. msg2 .. "'")
print("  通过: $\"Sum: {a + b}\" =", msg2)

print("=== 测试7: || lambda 在管道中 ===")
local get100 = || -> 100
local result3 = get100() |> |x| -> x / 2
assert(result3 == 50, "expected 50, got " .. tostring(result3))
print("  通过: get100() |> |x| -> x / 2 =", result3)

print("=== 测试8: 已有语法不受影响 ===")
local t = lambda(x): x * 2
assert(t(5) == 10, "lambda broken")
local t2 = () => 42
assert(t2() == 42, "arrow broken")
local t3 = f"value: {t(5)}"
assert(t3 == "value: 10", "f-string broken")
print("  通过: 已有语法正常")

print("\n所有测试通过!")