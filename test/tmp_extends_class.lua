class Animal {
  function init(self)
    self.name = "animal"
  end
}

class Dog extends Animal {
  function init(self)
    super.init(self)
  end
}

local d = Dog()
print(d.name)