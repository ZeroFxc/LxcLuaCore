-- 最简单的混淆器测试
-- 使用 string.dump 来验证

local code = [[
local a, b = 10, 20
if a > b then
    return a
else
    return b
end
]]

-- 编译为函数
local fn = load(code)
print("加载成功")

-- dump 为字节码
local dump = string.dump(fn)
print("字节码长度: " .. #dump)

-- 写入临时文件
local f = io.open("test_temp.luac", "wb")
f:write(dump)
f:close()

-- 直接执行原始代码
print("原始代码执行结果: " .. tostring(fn()))

-- 加载字节码并执行
local fn2 = load(dump)
print("字节码加载后执行结果: " .. tostring(fn2()))
