-- 测试方法里 self 是否可以省略
class P {
  -- 不写 self 参数
  function getName()
    return self.name
  end
  -- 显式写 self 参数
  function getAge(self)
    return self.age
  end
  function init(n, a)
    self.name = n
    self.age = a
  end
}

local p = P("tom", 18)
print("name:", p:getName())
print("age:", p:getAge())
