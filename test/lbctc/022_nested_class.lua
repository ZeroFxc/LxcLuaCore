-- 嵌套类（内部类）测试

print("=== 022 nested_class START ===")

-- 外部类包含嵌套类
class Outer
  function init(self, name)
    self.name = name
  end

  -- 嵌套类：在外部类内部定义
  class Inner
    function init(self, value)
      self.value = value
    end

    function get_value(self)
      return self.value
    end
  end

  function get_name(self)
    return self.name
  end
end

-- 创建外部类实例
local outer = Outer("outer_obj")
assert(outer:get_name() == "outer_obj", "outer get_name failed")
print("outer name: " .. outer:get_name())

-- 通过 Outer.__statics.Inner 访问嵌套类
-- 嵌套类存储在父类的 __statics 表中
local inner_class = Outer.__statics.Inner
assert(inner_class ~= nil, "Inner class not found in Outer.__statics")
print("inner_class found: " .. tostring(inner_class ~= nil))

-- 创建嵌套类实例
local inner = inner_class(42)
assert(inner:get_value() == 42, "inner get_value failed")
print("inner value: " .. inner:get_value())

-- 使用 is 运算符检查
print("inner is Inner: " .. tostring(inner is Inner))
print("inner is Outer: " .. tostring(inner is Outer))

print("=== 022 nested_class END ===")