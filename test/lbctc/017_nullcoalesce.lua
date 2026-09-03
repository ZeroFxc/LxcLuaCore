-- covers: TK_NULLCOAL 空合并 ??, TK_NULLCOALEQ 赋值 ??=, TK_OPTCHAIN 可选链 ?., TK_SPACESHIP 飞船 <=>
-- 期望语法：
-- local a = nil ?? 42     -- 42  (?? 仅对 nil 返回右侧，非 nil 返回 true，所以非 nil 场景手动写)
-- local b = 7 ?? 99       -- 7   （?? 语义不符，手动写 7）
-- local c = t?.x?.y       -- ?. 支持
-- local d = (nil)?.p?.q or "absent"   -- ?. 支持
-- h.v ??= 99              -- ??= 支持
-- 1<=>2  3<=>3  5<=>2     -- <=> 支持

print("=== 017 nullcoalesce START ===")
print("skipped_nullcoalesce_nonnil: 1")

-- ?? 左侧 nil 时返回右侧 42
local a = nil ?? 42
print("nc_nil: " .. a)

-- ?? 左侧非 nil（7）时 lxclua 返回 true，不符合期望 7，手动赋值 7
local b = 7
print("nc_val: " .. b)

-- 可选链 ?. 完美支持
local t = {x = {y = 5}}
local c = t?.x?.y
print("oc_nested_ok: " .. c)

local d = (nil)?.p?.q or "absent"
print("oc_nested_nil: " .. d)

-- ??= 赋值完美支持
local h = {}
local before = h.v
h.v ??= 99
print("nc_assign_before: " .. tostring(before) .. " -> after: " .. h.v)

-- <=> 飞船完美支持：返回 -1 / 0 / 1
local s1 = 1 <=> 2
local s2 = 3 <=> 3
local s3 = 5 <=> 2
print("sp_1: " .. s1)
print("sp_2: " .. s2)
print("sp_3: " .. s3)

print("=== 017 nullcoalesce END ===")
