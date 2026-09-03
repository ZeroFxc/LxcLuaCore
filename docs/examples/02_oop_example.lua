-- 02_oop_example.lua
-- 面向对象编程示例 (已通过实际运行验证)
-- 运行: lxclua.exe examples/02_oop_example.lua

-- 抽象基类
abstract class Animal {
  function init(self, name)
    self.name = name
  end
  abstract function speak(self)
  function desc(self)
    return $"Animal {self.name}"
  end
}

-- 接口
interface Movable {
  function move(self, x, y)
}

-- 具体类继承 + 实现接口
class Dog extends Animal implements Movable {
  function init(self, name, breed)
    super.init(self, name)
    self.breed = breed
  end
  function speak(self)
    io.write(self.name .. " says: Woof!\n")
  end
  function move(self, x, y)
    io.write(self.name .. " moves to (" .. x .. ", " .. y .. ")\n")
  end
}

class Cat extends Animal implements Movable {
  function speak(self)
    io.write(self.name .. " says: Meow!\n")
  end
  function move(self, x, y)
    io.write(self.name .. " sneaks to (" .. x .. ", " .. y .. ")\n")
  end
}

-- 使用
local dog = Dog("Buddy", "Golden Retriever")
local cat = Cat("Whiskers")

dog:speak()
cat:speak()
dog:move(10, 20)
cat:move(5, 5)

-- instanceof 检查
io.write("dog instanceof Dog: ", tostring(dog instanceof Dog), "\n")
io.write("dog instanceof Animal: ", tostring(dog instanceof Animal), "\n")
io.write("cat instanceof Dog: ", tostring(cat instanceof Dog), "\n")

-- 静态成员
class MathUtil {
  static PI = 3.14159
  static function square(x) return x * x end
}
io.write("PI: ", tostring(MathUtil.PI), "\n")
io.write("square(5): ", tostring(MathUtil.square(5)), "\n")