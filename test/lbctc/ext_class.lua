-- LXCLUA: class 基础、继承、静态、instanceof
class Animal
  function init(self, name)
    self.name = name
  end
  function speak(self)
    return "I am " .. self.name
  end
end

class Dog extends Animal
  function speak(self)
    return Animal.speak(self) .. ", a dog"
  end
  static function info()
    return "Dog class"
  end
end

local a = Animal("Cat")
local d = Dog("Buddy")
print(a:speak())
print(d:speak())
print(Dog.info())
print(d instanceof Dog)
print(d instanceof Animal)
print(a instanceof Dog)
