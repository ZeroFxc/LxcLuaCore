class Animal {
  function init(self, name)
    self.name = name
  end
}

class Flyable {
  function fly(self)
    return "flying"
  end
}
class Swimmable {
  function swim(self)
    return "swimming"
  end
}
class Duck extends Animal, Flyable, Swimmable {
  function init(self, name)
    super.init(self, name)
  end
}

local duck = Duck("Donald")
print("fly:", duck:fly())
print("swim:", duck:swim())
print("name:", duck.name)
