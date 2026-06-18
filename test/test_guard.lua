-- Guard 语句测试
print("=== Guard 测试 ===")

-- 1. 基本 guard 条件
local function test_guard_basic(x)
    guard x ~= nil else {
        return "nil input"
    }
    return "got: " .. x
end

print("1. basic: " .. test_guard_basic("hello"))  -- 期望: got: hello
print("2. basic nil: " .. test_guard_basic(nil))  -- 期望: nil input

-- 2. guard let 形式
local function test_guard_let(t)
    guard let name = t.name else {
        return "no name"
    }
    return "name: " .. name
end

print("3. let: " .. test_guard_let({name = "Alice"}))  -- 期望: name: Alice
print("4. let nil: " .. test_guard_let({}))             -- 期望: no name

-- 3. guard 在循环中配合 break
local function test_guard_loop(arr)
    local result = {}
    for i = 1, 3 do
        guard arr[i] ~= nil else {
            break
        }
        result[#result + 1] = arr[i]
    end
    return #result
end

print("5. loop: " .. test_guard_loop({1, 2, nil}))  -- 期望: 2

print("PASS: guard")