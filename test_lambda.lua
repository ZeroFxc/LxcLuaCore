-- ========== LAMBDA 使用指南 ==========

-- 1) 表达式体 (用 ':') — 单表达式，隐式return
local f1 = lambda x: x + 1
print("f1(5):", f1(5))

local add = lambda (a, b): a + b
print("add(3,7):", add(3, 7))

-- 2) 语句体 (用 '=>') — 需要显式return
local f2 = lambda x => return x * 2
print("f2(10):", f2(10))

local abs = lambda x =>
    if x > 0 then
        return x
    else
        return -x
    end
print("abs(-5):", abs(-5))

-- 3) 无参数lambda
local hello = lambda: print("无参lambda")
hello()

-- 4) 作为回调
local t = {1, 2, 3, 4}
local doubled = {}
for _, v in ipairs(t) do
    table.insert(doubled, (lambda x: x * 2)(v))
end
for _, v in ipairs(doubled) do print(v) end

-- 5) IIFE 立即调用
local result = (lambda x: x * x)(7)
print("IIFE:", result)
