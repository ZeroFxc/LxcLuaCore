print("=== START ===")

class Animal {
  function init(self, name)
    self.name = name
  end
  function speak(self)
    return "generic sound"
  end
}

class Dog extends Animal {
  override function speak(self)
    return "woof! my name is " .. self.name
  end
}

local d = Dog("Buddy")
print(d:speak())

print("=== END ===")