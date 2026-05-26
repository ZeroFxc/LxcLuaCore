-- 快速验证失败项（去掉 C 注释部分）
print("=== 1. 验证 Lambda ===")
-- 尝试不带 end
local f, err = load([[local sq = lambda(x): x * x; assert(sq(6)==36)]], "=lambda_test")
if f then
    local ok = pcall(f)
    print("Lambda(无end) 通过: " .. tostring(ok))
else
    print("Lambda(无end) 失败: " .. (err:match(".-:%d+: (.*)") or err))
end

-- 尝试带 end 块体形式
local f1b, err1b = load([[
    local sq = lambda(x)
        return x * x
    end
    assert(sq(6) == 36)
]], "=lambda_block")
if f1b then
    local ok = pcall(f1b)
    print("Lambda(块体) 通过: " .. tostring(ok))
else
    print("Lambda(块体) 失败: " .. (err1b:match(".-:%d+: (.*)") or err1b))
end

print("\n=== 2. 验证 ^= 复合赋值 ===")
-- 用指数运算
local f2, err2 = load([[local a=10; a^=2; assert(a==100)]], "=pow_test")
if f2 then
    local ok2 = pcall(f2)
    print("^= 通过: " .. tostring(ok2))
else
    print("^= 失败: " .. (err2:match(".-:%d+: (.*)") or err2))
end

print("\n=== 3. 验证 when 语句 ===")
local f3, err3 = load([[
    local result
    when -5 > 0
        result = "positive"
    case -5 < 0
        result = "negative"
    else
        result = "zero"
    end
    assert(result == "negative")
]], "=when_test")
if f3 then
    local ok3 = pcall(f3)
    print("when 通过: " .. tostring(ok3))
else
    print("when 失败: " .. (err3:match(".-:%d+: (.*)") or err3))
end

print("\n=== 4. 验证 [::-1] 切片 ===")
local f4, err4 = load([[local arr={1,2,3}; local s=arr[::-1]; assert(s[1]==3)]], "=slice_rev")
if f4 then
    local ok4 = pcall(f4)
    print("[::-1] 通过: " .. tostring(ok4))
else
    print("[::-1] 失败: " .. (err4:match(".-:%d+: (.*)") or err4))
end

-- 也试试 -1 步长
local f4b, err4b = load([[local arr={1,2,3,4,5}; local s=arr[1:5:-1]; assert(s[1]==5)]], "=slice_step")
if f4b then
    print("[a:b:-c] 通过")
else
    print("[a:b:-c] 失败: " .. (err4b:match(".-:%d+: (.*)") or err4b))
end

print("\n=== 5. 验证字符串插值 直接测试 ===")
print("Hello, World!") -- 先确认基础正常
-- 插值测试放在此处
local name = "Test"
local s = "Hello, ${name}!"
print("插值结果: " .. s)