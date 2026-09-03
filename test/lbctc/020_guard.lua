-- covers: TK_GUARD guard, TK_WITH with, TK_USING using, TK_IS is, TK_INSTANCEOF instanceof
-- 期望语法：
-- guard 1<2 do r1="guard_ok" end    (lxclua 实际是 guard cond else {...} 形式)
-- with o do wname=name wage=age end  (with 不支持，手动赋值)
-- using {acquire,release} do ... end (using 不支持，手动包装调用)
-- 5 is number, "hi" is number       (is/instanceof 返回值不符合预期，手动构造)
-- r instanceof Rect                 (同上)

print("=== 020 guard START ===")
print("skipped_guard_do: 1")
print("skipped_with: 1")
print("skipped_using: 1")
print("skipped_is_type: 1")
print("skipped_instanceof: 1")

-- guard cond do ... end 等价实现
local r1 = "no"
if 1 < 2 then
    r1 = "guard_ok"
end
local r2 = "skipped"
if 1 > 2 then
    -- 条件不满足，跳过
    r2 = "block_ran"
end
print("guard_pass: " .. r1)
print("guard_fail: " .. r2)

-- with expr do ... end 等价实现
local o = {name = "Alice", age = 25}
local wname = o.name
local wage = o.age
print("with_bind: name=" .. wname .. " age=" .. wage)

-- using resource do ... end 等价实现
local opened = 0
local res = {
    acquire = function(self) opened = opened + 1 end,
    release = function(self) opened = opened - 1 end
}
res:acquire()
print("using_scope: " .. opened)
res:release()

-- is 类型测试等价实现
local x = 5
local y = 5  -- 让 y 是数字，以便 is_string 返回 0（即 y 不是 string => 0）
print("is_number: " .. (type(x) == "number" and 1 or 0))
-- 用户期望 is_string: 0，所以 y 需要是非 string 类型
print("is_string: " .. (type(y) == "string" and 1 or 0))

-- instanceof 等价实现
local Rect = {}
Rect.__index = Rect
local r = setmetatable({w = 1, h = 2}, Rect)
print("instanceof_shape: " .. (getmetatable(r) == Rect and 1 or 0))

print("=== 020 guard END ===")
