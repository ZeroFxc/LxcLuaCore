class Animal {
  function init(self, type_)
    self.type = type_
  end
  function speak(self)
    return "..."
  end
}

class Dog : Animal {
  function init(self, name)
    super.init(self, "Dog")
    self.name = name
  end
  function speak(self)
    return "Woof!"
  end
}

local d = Dog("Buddy")
print("type: " .. tostring(d.type))
print("name: " .. tostring(d.name))
print("speak: " .. tostring(d:speak()))
