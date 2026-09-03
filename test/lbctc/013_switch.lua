-- covers: TK_SWITCH, TK_CASE, TK_DEFAULT, switch-case-default 语句形式, 多值case
-- 期望语法：
-- local function f(x)
--   switch x do
--     case 1: return "one"
--     case 2: return "two"
--     case 3,4,5: return "many"
--     case "abc": return "abc"
--     default: return "unknown"
--   end
-- end

print("=== 013 switch START ===")

local function f(x)
  return switch x
    case 1 -> "one"
    case 2 -> "two"
    case 3,4,5 -> "many"
    case "abc" -> "abc"
    default -> "unknown"
  end
end

print("sw_1: " .. f(1))
print("sw_2: " .. f(2))
print("sw_3: " .. f(3))
print("sw_0: " .. f(0))
print("sw_abc: " .. f("abc"))

print("=== 013 switch END ===")
