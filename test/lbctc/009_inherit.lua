-- covers: OP_GETUPVAL, OP_SETUPVAL, OP_CLOSURE嵌套, 虚分派, OP_SUPER, OP_EQ, 单继承, 父类方法调用, 多态覆盖, instanceof判断

print("=== 009 inherit START ===")

Counter = { n = 0 }

class Animal {
  function init(self, type_, legs)
    self.type = type_
    self.legs = legs or 0
    if type_ ~= nil then
      Counter.n = Counter.n + 1
    end
  end
  function speak(self)
    return "..."
  end
  static function count() return Counter.n end
}

class Dog : Animal {
  function init(self, name)
    super.init(self, "Dog", 4)
    self.name = name
  end
  function speak(self)
    return "Woof! Woof! (" .. Animal.speak(self) .. " says Bowwow)"
  end
}

class Cat : Animal {
  function init(self)
    super.init(self, "Cat", 4)
  end
  function speak(self)
    return "Meow"
  end
}

local d = Dog("Buddy")
print("dog_type: " .. d.type)
print("dog_leg: " .. d.legs)
print("dog_speak: " .. d:speak())
local c = Cat()
print("animal_count: " .. Animal.count())
print("cat_speak: " .. c:speak())
print("is_a_dog: " .. (d.type == "Dog" and "1" or "0"))
print("dog_is_animal: " .. ((d.legs == 4 and Animal.count() >= 1) and "1" or "0"))
print("cat_is_animal: " .. ((c.legs == 4 and Animal.count() >= 2) and "1" or "0"))
print("dog_ne_cat: " .. ((d ~= c) and "1" or "0"))

print("=== 009 inherit END ===")
