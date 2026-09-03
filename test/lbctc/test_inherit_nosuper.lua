class Animal {
  function init(self, type_)
    self.type = type_
  end
}

class Dog : Animal {
  function init(self, name)
    self.name = name
  end
}

local d = Dog("Buddy")
print("name: " .. tostring(d.name))
print("type: " .. tostring(d.type))
