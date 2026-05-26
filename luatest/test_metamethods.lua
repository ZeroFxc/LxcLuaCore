-- 测试新增元方法: __eq, __lt, __le, __concat
local quickjs = require("quickjs")
local rt = quickjs.newRuntime()
local ctx = rt:newContext()

-- ========== __eq 测试 ==========
print("========== __eq ==========")
local obj1 = ctx:eval("({x:1})")
local obj2 = ctx:eval("({x:1})")
local obj3 = obj1  -- 同一个 JSValue userdata

print("同一对象 (obj1 == obj1):", obj1 == obj1)        -- true (JS_SameValue)
print("相同内容 (obj1 == obj2):", obj1 == obj2)        -- false (不同 JS 对象)
print("同一引用 (obj1 == obj3):", obj1 == obj3)        -- true (同一个 userdata)

-- JS 原始值 (被 js_to_lua 转成了 Lua 类型，__eq 不触发)
local num1 = ctx:eval("42")
local num2 = ctx:eval("42")
print("数字 42 == 42 (Lua):", num1, num2, num1 == num2) -- true (Lua number 比较)

print("")

-- ========== __lt / __le 测试 ==========
print("========== __lt / __le ==========")
local a = ctx:eval("10")
local b = ctx:eval("20")
-- 注意: 数字已被 js_to_lua 转为 Lua number，所以这里用 JS 值包再测试
-- 直接创建 JS 数组内含数字元素来获取 JSValue 引用
local nums = ctx:eval("[10, 20, 15]")
print("nums[1] < nums[2]:", nums[1] < nums[2])   -- 10 < 20 → true
print("nums[1] < nums[3]:", nums[1] < nums[3])   -- 10 < 15 → true
print("nums[2] < nums[1]:", nums[2] < nums[1])   -- 20 < 10 → false
print("nums[1] <= nums[1]:", nums[1] <= nums[1]) -- 10 <= 10 → true
print("nums[2] <= nums[1]:", nums[2] <= nums[1]) -- 20 <= 10 → false

-- 字符串比较
local strs = ctx:eval("['apple', 'banana', 'cherry']")
print("'apple' < 'banana':", strs[1] < strs[2])   -- true
print("'cherry' < 'banana':", strs[3] < strs[2])  -- false
print("'apple' <= 'apple':", strs[1] <= strs[1])  -- true

print("")

-- ========== __concat 测试 ==========
print("========== __concat ==========")
local js_str = ctx:eval("'Hello'")
print("JS字符串 + Lua字符串:", js_str .. " World!")
print("Lua字符串 + JS字符串:", "Result: " .. js_str)
print("JS + JS:", js_str .. ctx:eval("' Lua'"))

print("")
print("=== 全部测试完成 ===")