-- 测试 switch 表达式 (无 do)
print("=== Test 1: switch 表达式基本 ===")
print(switch 42
    case 1, 2, 3 -> "1-3"
    case 4, 5, 6 -> "4-6"
end)  -- 期望: nil (无匹配)

-- 测试 switch 表达式多值
print("=== Test 2: switch 表达式多值 ===")
print(switch 3
    case 1, 2, 3 -> "low"
    case 4, 5, 6 -> "high"
    default -> "other"
end)  -- 期望: low

-- 测试 switch 语句多值
print("=== Test 3: switch 语句多值 ===")
switch 5 do
    case 1, 2, 3 -> print("low")
    case 4, 5, 6 -> print("high")
    default -> print("other")
end  -- 期望: high

-- 测试 switch 语句块体
print("=== Test 4: switch 语句块体 ===")
switch 3 do
    case 1, 2: print("low")
    case 3: print("mid")
end  -- 期望: mid

-- 测试 switch 表达式多值 + default
print("=== Test 5: switch 表达式 default ===")
print(switch 99
    case 1, 2, 3 -> "low"
    case 4, 5, 6 -> "high"
    default -> "other"
end)  -- 期望: other

print("=== All switch tests completed ===")