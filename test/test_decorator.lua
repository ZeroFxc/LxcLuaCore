-- test_decorator.lua - 装饰器/注解系统测试

print("===== 装饰器基础测试 =====")

-- 1. 函数装饰器
function logCall(func)
    return function(...)
        print("[LOG] calling function")
        return func(...)
    end
end

@logCall
function add(a, b)
    return a + b
end

print("Decorated add: " .. add(3, 4))

-- 2. 多个装饰器
function double(func)
    return function(...)
        return func(...) * 2
    end
end

function addOne(func)
    return function(...)
        return func(...) + 1
    end
end

@double
@addOne
function getValue()
    return 5
end

print("Multi-decorator: " .. getValue())  -- (5 + 1) * 2 = 12

-- 3. 类装饰器
function sealed(cls)
    -- 自定义 sealed 行为，通过装饰器实现
    print("sealed decorator applied to class")
    return cls
end

@sealed
class MyClass
    function __init__(self)
        self.value = 42
    end
    function getValue(self)
        return self.value
    end
end

local obj = MyClass()
print("MyClass value: " .. obj:getValue())

print("===== 装饰器测试全部通过 =====")