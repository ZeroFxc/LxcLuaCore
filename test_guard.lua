-- 测试 Guard 语句

-- 测试1: 基本 guard 语句 - 条件为真，不进入 else 块
function test_guard_true()
    local x = 10
    guard x > 0 else {
        return "error: x <= 0"
    }
    return "ok: x > 0"
end

local result = test_guard_true()
print("测试1 (guard 条件为真):", result)
assert(result == "ok: x > 0", "测试1 失败")

-- 测试2: 基本 guard 语句 - 条件为假，进入 else 块并返回
function test_guard_false()
    local x = -5
    guard x > 0 else {
        return "error: x <= 0"
    }
    return "ok: x > 0"
end

local result = test_guard_false()
print("测试2 (guard 条件为假):", result)
assert(result == "error: x <= 0", "测试2 失败")

-- 测试3: guard 带复杂条件
function test_guard_complex(a, b)
    guard a ~= nil and b ~= nil and a + b > 0 else {
        return "invalid params"
    }
    return "valid: " .. (a + b)
end

local result = test_guard_complex(5, 3)
print("测试3 (guard 复杂条件，有效):", result)
assert(result == "valid: 8", "测试3 失败")

local result = test_guard_complex(nil, 3)
print("测试3 (guard 复杂条件，a为nil):", result)
assert(result == "invalid params", "测试3a 失败")

local result = test_guard_complex(-10, 5)
print("测试3 (guard 复杂条件，和为负):", result)
assert(result == "invalid params", "测试3b 失败")

-- 测试4: guard 在循环中使用 break
function test_guard_loop()
    local result = 0
    for i = 1, 10 do
        guard i <= 5 else {
            break
        }
        result = result + i
    end
    return result
end

local result = test_guard_loop()
print("测试4 (guard 在循环中 break):", result)
assert(result == 15, "测试4 失败")  -- 1+2+3+4+5 = 15

-- 测试5: guard 在循环中使用 continue
function test_guard_continue()
    local result = 0
    for i = 1, 10 do
        if i % 2 == 0 then
            guard i <= 5 else {
                goto continue
            }
            result = result + i
        end
        ::continue::
    end
    return result
end

local result = test_guard_continue()
print("测试5 (guard 在循环中 goto continue):", result)
assert(result == 6, "测试5 失败")  -- 2+4 = 6

-- 测试6: guard let 模式
function test_guard_let(t)
    guard let value = t else {
        return "table is nil"
    }
    guard let inner = value.x else {
        return "x is nil"
    }
    return "x = " .. inner
end

local result = test_guard_let({x = 42})
print("测试6 (guard let 有效):", result)
assert(result == "x = 42", "测试6 失败")

local result = test_guard_let({})
print("测试6 (guard let x为nil):", result)
assert(result == "x is nil", "测试6a 失败")

local result = test_guard_let(nil)
print("测试6 (guard let table为nil):", result)
assert(result == "table is nil", "测试6b 失败")

print("\n所有 Guard 语句测试通过!")