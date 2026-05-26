-- 完整 OOP 段隔离测试 v2
print("test start")

class Animal
  function __init__(self, name)
    self.name = name
  end
  function speak(self)
    return self.name .. " says hi"
  end
end
local a = Animal("dog")
print("Animal:", a:speak())

class Cat extends Animal
  function speak(self)
    return self.name .. " meows"
  end
end
local c = Cat("kitty")
print("Cat:", c:speak())

-- interface 方法无 body，不能有 end
interface Walkable
  function walk(self)
end
class Dog implements Walkable
  function __init__(self, name)
    self.name = name
  end
  function walk(self)
    return self.name .. " walks"
  end
end
local d = Dog("buddy")
print("Dog:", d:walk())

struct Point {
  int x;
  int y;
}
local p = Point()
p.x = 3; p.y = 4
print("Point:", p.x, p.y)

local p2 = new Animal("Charlie")
print("new Animal:", p2:speak())

print("ALL PASSED")