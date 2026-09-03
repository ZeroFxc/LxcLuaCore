-- covers: TK_MATCH, match begin/end, case, =>, 守卫条件 if, 字面量匹配, 通配符 _
-- 期望语法（lxclua match 使用 case ... => 形式）：
-- local function g(x)
--   match x begin
--     case 1 => "one";
--     case 2 => "two";
--     case v if v >= 6 and v <= 10 => v .. "->range(6..10)";
--     case "hello" => "str:hello";
--     case _ => "wildcard";
--   end
-- end

print("=== 014 match START ===")

local function g(x)
  return match x do
    case 1 => "one"
    case 2 => "two"
    case v if type(v)=="number" and v >= 6 and v <= 10 => v .. "->range(6..10)"
    case "hello" => "str:hello"
    case _ => "wildcard"
  end
end

print("mt_1: " .. g(1))
print("mt_2: " .. g(2))
print("mt_7: " .. g(7))
print("mt_hello: " .. g("hello"))
print("mt_nil: " .. g(nil))

print("=== 014 match END ===")
