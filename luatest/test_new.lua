-- 隔离测试 new 表达式
print("test start")
class Animal
  function __init__(self, name)
    self.name = name
  end
  function speak(self)
    return self.name .. " says hi"
  end
end
print("class ok")
local p1 = Animal("dog")
print("Animal() ok:", p1:speak())
local p2 = new Animal("cat")
print("new Animal() ok:", p2:speak())