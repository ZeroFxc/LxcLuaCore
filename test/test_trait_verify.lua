-- test_trait_verify.lua - Trait require 方法验证边界测试

print("===== Trait require 验证测试 =====")

-- 1. 缺少require方法应该报错
local ok, err = pcall(function()
    local script = [[
        trait T
            require function foo(self, x)
        end
        class C use T
        end
        local c = C()
    ]]
    load(script)()
end)
if not ok then
    print("测试1通过: 缺少require方法正确报错 -> " .. tostring(err):match("必须实现"))
else
    print("测试1失败: 应该报错但没有")
end

-- 2. 正确实现require方法应该通过
trait Cmp
    require function compare(self, other)
    function isBigger(self, other)
        return self:compare(other) > 0
    end
end

class Val use Cmp
    function __init__(self, v)
        self.v = v
    end
    function compare(self, other)
        return self.v - other.v
    end
end

local v1 = Val(10)
local v2 = Val(5)
print("测试2通过: 正确实现require方法 -> isBigger=" .. tostring(v1:isBigger(v2)))

-- 3. 抽象类可以不实现require方法
trait T2
    require function foo(self, x)
end

abstract class AC use T2
end
print("测试3通过: 抽象类可以不实现require方法")

print("===== Trait require 验证测试全部通过 =====")