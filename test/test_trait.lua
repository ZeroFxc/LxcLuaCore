-- test_trait.lua - Trait/Mixin 系统测试

print("===== Trait 基础测试 =====")

-- 1. 基本 trait 定义和使用
trait Serializable
    function toString(self)
        local parts = {}
        for k, v in pairs(self) do
            if type(v) ~= "function" and k:sub(1, 2) ~= "__" then
                parts[#parts + 1] = k .. "=" .. tostring(v)
            end
        end
        return "{" .. table.concat(parts, ", ") .. "}"
    end
end

class Point use Serializable
    function __init__(self, x, y)
        self.x = x
        self.y = y
    end
end

local p = Point(10, 20)
local str = p:toString()
print("Serializable trait: " .. str)

-- 2. 多个 trait
trait Loggable
    function getLogLevel(self)
        return "INFO"
    end
    
    function log(self, msg)
        print("[" .. self:getLogLevel() .. "] " .. msg)
    end
end

class User use Serializable, Loggable
    function __init__(self, name, age)
        self.name = name
        self.age = age
    end
end

local u = User("Alice", 30)
u:log("User created")
print("Multi-trait toString: " .. u:toString())

-- 3. trait require 方法测试
trait Comparable
    require function compare(self, other)
    function isGreater(self, other)
        return self:compare(other) > 0
    end
    function isLess(self, other)
        return self:compare(other) < 0
    end
end

class Number use Comparable
    function __init__(self, value)
        self.value = value
    end
    function compare(self, other)
        return self.value - other.value
    end
end

local n1 = Number(100)
local n2 = Number(50)
print("n1 > n2: " .. tostring(n1:isGreater(n2)))
print("n1 < n2: " .. tostring(n1:isLess(n2)))

print("===== Trait 测试全部通过 =====")